// OH_Node V0 – W5500 Ethernet + Wi-Fi test
//
// LEDs – yellow = reachable (Ethernet or Wi-Fi), red = fault:
//   yellow solid              at least one network online; flickers off on each HTTP request
//   yellow blinking (2 Hz)    connecting (DHCP running / Wi-Fi in its first 20 s), nothing online yet
//   red blinking slow (1 Hz)  cable plugged but DHCP failed, and no Wi-Fi
//   red solid                 no network at all
//   red blinking fast (4 Hz)  W5500 not answering (overrides the red states above)
//   red/yellow alternating    OTA update in progress
//
// Wi-Fi: credentials are entered on the status page and stored in NVS (survive reboot and OTA).
// When connected, also reachable as http://oh-node.local/
// Ethernet (W5500) runs on the ESP-IDF driver, so Ethernet and Wi-Fi share the lwIP stack: one HTTP
// server, the RTP receiver and mDNS serve both.
//
// OTA: curl --data-binary @firmware.bin http://<IP>/update  (or: pio run -e oh_node_ota -t upload)
//
// Relays (all off at boot, only switched on request):
//   /relay?n=1..4&s=1|0|t   on / off / toggle
//   /relay/all?s=1|0        all on / off  (/relay/off = all off)
//   /relay/test             K1..K4 one after another, 1 s each (non-blocking)
//
// Motion detectors (J8 / J10: 1 = 12 V via 200 mA polyfuse, 2 = GND, 3 = signal with 10K pull-up):
// LED strips (J7 / J9: 1 = data, 2 = GND; strip powered separately, 5 V WS2812 / 12 V WS2815):
//   /led?s=1..2[&m=off|color|rainbow|chase|test|sparkle][&c=RRGGBB][&b=0..100][&n=count][&t=ws2812|ws2815]
//         [&w=0..255 chase tail in LEDs][&v=1..500 chase speed in LEDs/s][&d=line|out|in chase direction]
//         [&r=1..500 sparkles per second][&f=100..30000 sparkle fade-out in ms]
//
//   /pir/polarity?n=1..2&h=1|0   which level means motion (stored, default low)   /pir/reset   clear counters
//   /pir/action?n=1..2&add=led1|led2|gong   /pir/action?n=..&del=idx   /pir/action?n=..&i=idx&h=0..3600 (hold s)
//   /pir/action?n=..&i=idx&e=off|color|rainbow|chase|sparkle|test   effect of an LED action
//   /pir/action?n=..&i=idx&fi=0..10000&fo=0..10000   fade-in / fade-out of an LED action in ms
//   /pir/action?n=..&i=idx&b=0..100   brightness of an LED action's effect (new actions copy the strip's)
//   An LED action switches its strip to the action's effect on motion for the hold time, then back to the
//   strip's own mode; gong plays on every activation.
//
// Audio (MAX98357A on J1 – passive 4..8 ohm speaker only, sleeve carries signal):
//   /audio/play?c=tone|gong|sweep&v=0..100[&f=Hz]   /audio/stop   /audio/volume?v=0..limit
//   /audio/freq?f=20..20000   test tone frequency, live while playing
//   /audio/limit?v=1..100     volume limit for all sources (stored, default 15)
//   Live stream: RTP on UDP port 5004, L16 48 kHz stereo. Evenly paced packets give the lowest latency
//   (tools/rtp_send.py, ~16-21 ms in the ESP). ffmpeg works too but sends in ~21 ms bursts (~32 ms) and
//   pushes the first ~0.5 s out at once, more than the W5500's 16 KB buffer holds, so the start is cut:
//     ffmpeg -re -i song.mp3 -af asetnsamples=n=240 -ar 48000 -ac 2 -acodec pcm_s16be \
//            -f rtp "rtp://<IP>:5004?pkt_size=1200"
//
//   /api/events             Server-Sent Events: the same JSON pushed on every change (+ heartbeat)
//   /api/status             JSON status; relay commands also answer with it when sent with Accept: application/json

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_mac.h>
#include <soc/gpio_reg.h>
#include <stdarg.h>

#include "audio.h"
#include "actions.h"
#include "eth.h"
#include "led.h"
#include "pir.h"
#include "rtp.h"
#include "sse.h"

constexpr uint8_t PIN_LED_R = 35;
constexpr uint8_t PIN_LED_Y = 36;

constexpr uint8_t PIN_ETH_SCLK = 9;
constexpr uint8_t PIN_ETH_MOSI = 10;
constexpr uint8_t PIN_ETH_MISO = 11;
constexpr uint8_t PIN_ETH_CS = 12;
constexpr uint8_t PIN_ETH_INT = 13;
constexpr uint8_t PIN_ETH_RST = 14;

// MAX98357A (U6): I2S in, ~SD_MODE driven directly by AMP_EN (3.3 V high = on, left slot only → mono mix)
constexpr uint8_t PIN_I2S_DIN = 40;
constexpr uint8_t PIN_I2S_BCLK = 41;
constexpr uint8_t PIN_I2S_LRCLK = 42;
constexpr uint8_t PIN_AMP_EN = 48;

// Motion detector signals: J8.3 → R34 → IO15, J10.3 → R36 → IO16
constexpr uint8_t PIN_PIR[PIR_COUNT] = {15, 16};

// WS2812 data: IO17 → U4 / IO18 → U5 (74AHCT1G125, 5 V level) → 150 R → J7.1 / J9.1
constexpr uint8_t PIN_LED_DATA[LED_STRIPS] = {17, 18};

// K1..K4 via Q1/Q2 (DMN2004DWK), active high; contacts on J2/J4/J5/J6 (1=COM 2=NO 3=NC)
constexpr uint8_t PIN_RELAY[4] = {4, 5, 6, 7};
static bool relayOn[4] = {};

constexpr const char *FW_BUILD = __DATE__ " " __TIME__;
constexpr const char *HOSTNAME = "oh-node";

static volatile uint32_t activityUntil = 0;
static volatile bool otaActive = false;

// Wi-Fi counts as "connecting" for this long after (re)connect starts, then as offline
constexpr uint32_t WIFI_CONNECT_GRACE_MS = 20000;
static volatile uint32_t wifiAttemptStart = 0;
static volatile bool wifiWasOnline = false;

// Crash-loop guard: counts boots that didn't survive SAFE_MODE_AFTER_MS. From the third one on, Ethernet
// and audio stay off, so Wi-Fi, the status page and OTA remain usable to fix the firmware.
constexpr uint32_t SAFE_MODE_AFTER_MS = 30000;
constexpr uint32_t BOOT_GUARD_MAGIC = 0x0A5E5AFE;
RTC_NOINIT_ATTR static uint32_t bootGuardMagic;  // RTC memory survives resets but starts out random
RTC_NOINIT_ATTR static uint32_t bootFailures;
static bool safeMode = false;

extern "C" volatile uint32_t oh_w5500_frames, oh_w5500_wakeups, oh_w5500_max_pending;

static WiFiServer httpServer(80);  // lwIP socket: answers on Ethernet and Wi-Fi
static Preferences prefs;
static uint8_t mac[6];

// Max lengths per 802.11: WPA2 passphrase 63 (SSID buffer declared above the LED task)
static char wifiPass[64] = "";

// USB-CDC write blocks when no host is reading; drop output instead
static void log(const char *fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (len > 0 && Serial.availableForWrite() >= len + 2) {
    Serial.println(buf);
  }
}

static char wifiSsid[33] = "";  // max SSID length per 802.11: 32

static bool wifiOnline() { return WiFi.status() == WL_CONNECTED; }

static bool wifiConnecting() {
  return wifiSsid[0] && !wifiOnline() && millis() - wifiAttemptStart < WIFI_CONNECT_GRACE_MS;
}

// 62 ms tick: tick%2 = 8 Hz toggle (4 Hz blink), tick/4%2 = 2 Hz, tick/8%2 = 1 Hz
static void ledTask(void *) {
  for (uint32_t tick = 0;; tick++) {
    bool r = false, y = false;
    if (otaActive) {
      r = tick % 2;
      y = !r;
    } else {
      EthState eth = ethState();
      bool online = eth == EthState::Online || wifiOnline();
      bool connecting = eth == EthState::Dhcp || wifiConnecting();
      if (online) y = millis() > activityUntil;
      else if (connecting) y = (tick / 4) % 2;
      else if (eth == EthState::DhcpFailed) r = (tick / 8) % 2;
      else r = true;
      if (eth == EthState::HwError) r = (tick / 2) % 2;
    }
    digitalWrite(PIN_LED_R, r);
    digitalWrite(PIN_LED_Y, y);
    vTaskDelay(pdMS_TO_TICKS(62));
  }
}

static void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      IPAddress ip = WiFi.localIP();
      log("[wifi] IP %u.%u.%u.%u  RSSI %d dBm", ip[0], ip[1], ip[2], ip[3], WiFi.RSSI());
      wifiWasOnline = true;
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // fires on every failed retry too; only a lost connection restarts the grace period
      if (wifiWasOnline) {
        wifiWasOnline = false;
        wifiAttemptStart = millis();
        log("[wifi] connection lost, reconnecting ...");
      }
      break;
    default:
      break;
  }
}

// (Re)connects with the current credentials; disconnects if none are set
static void wifiConnect() {
  static bool started = false;
  if (!started) {
    WiFi.persistent(false);  // credentials live in our own NVS namespace only
    WiFi.onEvent(onWiFiEvent);
    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem sleep adds 100+ ms latency; power draw doesn't matter here
    WiFi.setAutoReconnect(true);
    started = true;
  }
  WiFi.disconnect();
  wifiWasOnline = false;
  wifiAttemptStart = millis();
  if (!wifiSsid[0]) {
    log("[wifi] not configured");
    return;
  }
  WiFi.begin(wifiSsid, wifiPass[0] ? wifiPass : nullptr);
  log("[wifi] connecting to %s ...", wifiSsid);
}

static void loadWifiConfig() {
  prefs.begin("wifi", true);
  prefs.getString("ssid", wifiSsid, sizeof(wifiSsid));
  prefs.getString("pass", wifiPass, sizeof(wifiPass));
  prefs.end();
}

// NVS writes stall flash access on both cores; they run between two LED frames so the strips don't glitch
template <typename F>
static void flashWrite(F fn) {
  ledRunBetweenFrames([](void *arg) { (*static_cast<F *>(arg))(); }, &fn);
}

static void saveWifiConfig() {
  flashWrite([] {
    prefs.begin("wifi", false);
    prefs.putString("ssid", wifiSsid);
    prefs.putString("pass", wifiPass);
    prefs.end();
  });
  log("[wifi] config saved (SSID %s)", wifiSsid);
}

// Actions are stored as the raw ActionTable under "pir"/"act". The first layout had no effect per action
// (4 bytes each): an LED action showed the strip's own mode on motion and kept the strip dark otherwise.
struct ActionV1 {
  ActionType type;
  uint8_t strip;
  uint16_t holdS;
};
// Second layout (6 bytes): with effect, without fade times.
struct ActionV2 {
  ActionType type;
  uint8_t strip;
  uint16_t holdS;
  LedMode effect;
};
static_assert(sizeof(ActionV2) == 6, "ActionV2 must match the stored layout");
// Third layout (10 bytes): with fade times, without brightness.
struct ActionV3 {
  ActionType type;
  uint8_t strip;
  uint16_t holdS;
  LedMode effect;
  uint16_t fadeInMs;
  uint16_t fadeOutMs;
};
static_assert(sizeof(ActionV3) == 10, "ActionV3 must match the stored layout");
static_assert(sizeof(ActionTable) == PIR_COUNT * ACTION_MAX * 12, "ActionTable layout changed: migrate the stored table");

static void writeLedConfig();
static void saveActions();

// Call after loadLedConfig(): converting the first layout changes strip modes
static void loadPirConfig() {
  prefs.begin("pir", true);
  uint8_t activeHigh = prefs.getUChar("pol", 0);  // bit n set = PIR n+1 active high; default active low
  ActionTable actions;
  size_t len = prefs.getBytesLength("act");
  bool converted = false;
  if (len == sizeof(actions)) {
    prefs.getBytes("act", actions, sizeof(actions));
    actionsSet(actions);
  } else if (len == sizeof(ActionV3) * PIR_COUNT * ACTION_MAX) {
    ActionV3 old[PIR_COUNT][ACTION_MAX];
    prefs.getBytes("act", old, sizeof(old));
    for (uint8_t p = 0; p < PIR_COUNT; p++) {
      for (uint8_t i = 0; i < ACTION_MAX; i++) {
        PirAction &a = actions[p][i];
        a = PirAction();
        a.type = old[p][i].type;
        a.strip = old[p][i].strip;
        a.holdS = old[p][i].holdS;
        a.effect = old[p][i].effect;
        a.fadeInMs = old[p][i].fadeInMs;
        a.fadeOutMs = old[p][i].fadeOutMs;
        if (a.type == ActionType::Led && a.strip < LED_STRIPS) a.brightness = ledConfig(a.strip).brightness;
      }
    }
    actionsSet(actions);
    converted = true;
  } else if (len == sizeof(ActionV2) * PIR_COUNT * ACTION_MAX) {
    ActionV2 old[PIR_COUNT][ACTION_MAX];
    prefs.getBytes("act", old, sizeof(old));
    for (uint8_t p = 0; p < PIR_COUNT; p++) {
      for (uint8_t i = 0; i < ACTION_MAX; i++) {
        actions[p][i] = PirAction();  // default fade times
        actions[p][i].type = old[p][i].type;
        actions[p][i].strip = old[p][i].strip;
        actions[p][i].holdS = old[p][i].holdS;
        actions[p][i].effect = old[p][i].effect;
        if (old[p][i].type == ActionType::Led && old[p][i].strip < LED_STRIPS) {
          actions[p][i].brightness = ledConfig(old[p][i].strip).brightness;
        }
      }
    }
    actionsSet(actions);
    converted = true;
  } else if (len == sizeof(ActionV1) * PIR_COUNT * ACTION_MAX) {
    // same look as before: the action shows the strip's mode, the strip itself goes to "off"
    ActionV1 old[PIR_COUNT][ACTION_MAX];
    prefs.getBytes("act", old, sizeof(old));
    for (uint8_t p = 0; p < PIR_COUNT; p++) {
      for (uint8_t i = 0; i < ACTION_MAX; i++) {
        PirAction &a = actions[p][i];
        a = PirAction();
        a.type = old[p][i].type;
        a.strip = old[p][i].strip;
        a.holdS = old[p][i].holdS;
        if (a.type != ActionType::Led || a.strip >= LED_STRIPS) continue;
        LedConfig c = ledConfig(a.strip);
        if (c.mode != LedMode::Off) a.effect = c.mode;
        a.brightness = c.brightness;
        c.mode = LedMode::Off;
        ledSetConfig(a.strip, c);
      }
    }
    actionsSet(actions);
    converted = true;
  }
  prefs.end();
  for (uint8_t i = 0; i < PIR_COUNT; i++) pirSetActiveHigh(i, activeHigh & (1 << i));
  if (converted) {
    saveActions();
    writeLedConfig();
    log("[pir] actions converted to per-action effects");
  }
}

static void saveActions() {
  ActionTable actions;
  actionsGet(actions);
  flashWrite([&] {
    prefs.begin("pir", false);
    prefs.putBytes("act", actions, sizeof(actions));
    prefs.end();
  });
  log("[pir] actions saved");
}

// Hold sliders send many values: written once the actions stayed unchanged for 2 s
static void saveActionsWhenIdle() {
  static uint32_t saved = UINT32_MAX, last = 0, changedAt = 0;
  uint32_t v = actionsVersion();
  if (saved == UINT32_MAX) {  // first call: what was loaded is what is stored
    saved = last = v;
    return;
  }
  if (v != last) {
    last = v;
    changedAt = millis();
    return;
  }
  if (v != saved && millis() - changedAt > 2000) {
    saveActions();
    saved = v;
  }
}

static void savePirConfig() {
  uint8_t activeHigh = 0;
  for (uint8_t i = 0; i < PIR_COUNT; i++) activeHigh |= pirActiveHigh(i) << i;
  flashWrite([&] {
    prefs.begin("pir", false);
    prefs.putUChar("pol", activeHigh);
    prefs.end();
  });
}

// Stored per strip under "c1"/"c2" as the raw struct, plus "ver" = LED_CONFIG_VERSION. LedConfig only
// grows at the end; an entry from older firmware is read into a default config and every field newer
// than its version is reset to the default (the old struct's padding may overlap the new field).
// Entries saved before "ver" existed (version 2) are told apart by size. "s1"/"s2" hold the very first
// layout, where tail/speed sat in padding as well.
//   2: dir (size 20), sparkle rate/fade (size 24)   3: sparkle fade-in (dropped again in 5)
//   4: sparkle rate slot held ms between sparks   5: back to sparks per second
//   6: motion control (motion, motionHoldS; size 28)   7: motion control moved to PIR actions (size 24)
constexpr uint8_t LED_CONFIG_VERSION = 7;
static_assert(sizeof(LedConfig) == 24, "LedConfig changed: bump LED_CONFIG_VERSION and handle it in loadLedConfig()");

static void loadLedConfig() {
  prefs.begin("led", true);
  uint8_t version = prefs.getUChar("ver", 2);
  bool migratedActions = false;
  for (uint8_t i = 0; i < LED_STRIPS; i++) {
    char key[3] = {'c', static_cast<char>('1' + i), '\0'};
    char oldKey[3] = {'s', static_cast<char>('1' + i), '\0'};
    LedConfig c;
    LedConfig defaults;
    size_t len = prefs.getBytesLength(key);
    uint8_t raw[64];  // stored entries may be longer than today's struct (v6: 28 bytes)
    if (len > 0 && len <= sizeof(raw)) {
      prefs.getBytes(key, raw, len);
      memcpy(&c, raw, min(len, sizeof(c)));
      if (version < 3) {
        if (len < 20) c.dir = defaults.dir;
        if (len < 24) {
          c.sparkleRate = defaults.sparkleRate;
          c.sparkleFadeMs = defaults.sparkleFadeMs;
        }
      }
      if (version == 4) c.sparkleRate = 1000 / max<uint16_t>(c.sparkleRate, 1);  // ms between sparks → sparks/s
      // v6: motion bits at byte 22 (bit 0 = PIR 1, bit 1 = PIR 2), hold seconds at 24..25 → PIR actions
      if (version == 6 && len >= 28 && raw[22]) {
        PirAction a;
        a.type = ActionType::Led;
        a.strip = i;
        a.holdS = raw[24] | raw[25] << 8;
        if (c.mode != LedMode::Off) a.effect = c.mode;  // v6 showed the own mode on motion, dark otherwise
        c.mode = LedMode::Off;
        for (uint8_t p = 0; p < PIR_COUNT; p++) {
          if (raw[22] & (1 << p)) migratedActions |= actionAdd(p, a);
        }
      }
    } else if ((len = prefs.getBytesLength(oldKey)) > 0 && len <= sizeof(c)) {
      prefs.getBytes(oldKey, &c, len);
      LedConfig defaults;
      c.tail = defaults.tail;
      c.speed = defaults.speed;
    }
    ledSetConfig(i, c);
  }
  prefs.end();
  if (version < LED_CONFIG_VERSION) {  // rewrite in today's layout, so a migration runs only once
    if (migratedActions) saveActions();
    writeLedConfig();
    log("[led] settings migrated from version %u", version);
  }
}

// All strips in today's layout
static void writeLedConfig() {
  flashWrite([&] {
    prefs.begin("led", false);
    for (uint8_t i = 0; i < LED_STRIPS; i++) {
      char key[3] = {'c', static_cast<char>('1' + i), '\0'};
      LedConfig c = ledConfig(i);
      prefs.putBytes(key, &c, sizeof(c));
    }
    prefs.putUChar("ver", LED_CONFIG_VERSION);
    prefs.end();
  });
}

// Like the audio settings: written once the strip settings have stayed unchanged for 2 s
static void saveLedConfigWhenIdle() {
  static LedConfig saved[LED_STRIPS], last[LED_STRIPS];
  static bool init = false;
  static uint32_t changedAt;
  LedConfig now[LED_STRIPS];
  for (uint8_t i = 0; i < LED_STRIPS; i++) now[i] = ledConfig(i);
  if (!init) {
    memcpy(saved, now, sizeof(now));
    memcpy(last, now, sizeof(now));
    init = true;
    return;
  }
  bool changed = false, unsaved = false;
  for (uint8_t i = 0; i < LED_STRIPS; i++) {
    changed |= now[i] != last[i];
    unsaved |= now[i] != saved[i];
  }
  if (changed) {
    memcpy(last, now, sizeof(now));
    changedAt = millis();
    return;
  }
  if (!unsaved || millis() - changedAt < 2000) return;
  flashWrite([&] {
    prefs.begin("led", false);
    for (uint8_t i = 0; i < LED_STRIPS; i++) {
      if (now[i] == saved[i]) continue;  // only the strip that changed
      char key[3] = {'c', static_cast<char>('1' + i), '\0'};
      prefs.putBytes(key, &now[i], sizeof(now[i]));
    }
    prefs.putUChar("ver", LED_CONFIG_VERSION);
    prefs.end();
  });
  memcpy(saved, now, sizeof(now));
  log("[led] settings saved");
}

static void loadAudioConfig() {
  prefs.begin("audio", true);
  audioSetVolumeLimit(prefs.getUChar("limit", 15));
  audioSetVolume(prefs.getUChar("vol", 10));
  prefs.end();
}

// Writes volume and limit once they have stayed unchanged for 2 s (sliders send many values)
static void saveAudioConfigWhenIdle() {
  static uint8_t savedVol = 255, savedLimit = 255, lastVol, lastLimit;
  static uint32_t changedAt;
  uint8_t vol = audioVolume(), limit = audioVolumeLimit();
  if (savedVol == 255) {  // first call: values were just loaded
    savedVol = lastVol = vol;
    savedLimit = lastLimit = limit;
    return;
  }
  if (vol != lastVol || limit != lastLimit) {
    lastVol = vol;
    lastLimit = limit;
    changedAt = millis();
    return;
  }
  if ((vol != savedVol || limit != savedLimit) && millis() - changedAt > 2000) {
    flashWrite([&] {
      prefs.begin("audio", false);
      prefs.putUChar("vol", vol);
      prefs.putUChar("limit", limit);
      prefs.end();
    });
    savedVol = vol;
    savedLimit = limit;
    log("[audio] saved volume %u %%, limit %u %%", vol, limit);
  }
}

// Reads one header line without CR/LF. Returns false on timeout or disconnect.
static bool readLine(Client &client, char *buf, size_t size, uint32_t deadline) {
  size_t len = 0;
  while (millis() < deadline) {
    if (!client.available()) {
      if (!client.connected()) return false;
      delay(1);
      continue;
    }
    char c = client.read();
    if (c == '\n') {
      buf[len] = '\0';
      return true;
    }
    if (c != '\r' && len < size - 1) buf[len++] = c;
  }
  return false;
}

// Reads a request body of contentLength bytes into buf (truncated to size-1). Returns false on timeout.
static bool readBody(Client &client, char *buf, size_t size, size_t contentLength, uint32_t deadline) {
  size_t got = 0;
  while (got < contentLength && millis() < deadline) {
    if (!client.available()) {
      if (!client.connected()) break;
      delay(1);
      continue;
    }
    char c = client.read();
    if (got < size - 1) buf[got] = c;
    got++;
  }
  buf[min(got, size - 1)] = '\0';
  return got == contentLength;
}

// In-place decode of application/x-www-form-urlencoded value
static void urlDecode(char *s) {
  char *out = s;
  for (; *s; s++) {
    if (*s == '+') {
      *out++ = ' ';
    } else if (*s == '%' && isxdigit(s[1]) && isxdigit(s[2])) {
      char hex[3] = {s[1], s[2], '\0'};
      *out++ = static_cast<char>(strtol(hex, nullptr, 16));
      s += 2;
    } else {
      *out++ = *s;
    }
  }
  *out = '\0';
}

// Copies the decoded value of `key` from a form body into out. Returns false if the key is missing.
static bool formValue(const char *body, const char *key, char *out, size_t size) {
  size_t keyLen = strlen(key);
  for (const char *p = body; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : nullptr) {
    if (strncmp(p, key, keyLen) != 0 || p[keyLen] != '=') continue;
    const char *v = p + keyLen + 1;
    size_t len = strcspn(v, "&");
    char raw[200];
    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, v, len);
    raw[len] = '\0';
    urlDecode(raw);
    strlcpy(out, raw, size);
    return true;
  }
  return false;
}

static void printHtmlEscaped(Client &client, const char *s) {
  for (; *s; s++) {
    switch (*s) {
      case '<':  client.print("&lt;"); break;
      case '>':  client.print("&gt;"); break;
      case '&':  client.print("&amp;"); break;
      case '"':  client.print("&quot;"); break;
      case '\'': client.print("&#39;"); break;
      default:   client.print(*s);
    }
  }
}

static void respond(Client &client, const char *status, const char *body) {
  client.printf("HTTP/1.1 %s\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n%s\n", status, body);
}

static void redirectHome(Client &client) {
  client.print("HTTP/1.1 303 See Other\r\nLocation: /\r\nConnection: close\r\n\r\n");
}

static void handleOta(Client &client, size_t contentLength) {
  if (contentLength == 0) {
    respond(client, "411 Length Required", "Content-Length missing");
    return;
  }
  otaActive = true;
  ledSetPaused(true);  // strips hold their last frame; the flash writes ahead would garble running frames
  log("[ota] receiving %u bytes", contentLength);

  if (!Update.begin(contentLength, U_FLASH)) {
    respond(client, "500 Internal Server Error", Update.errorString());
    otaActive = false;
    ledSetPaused(false);
    return;
  }

  static uint8_t buf[1460];
  size_t written = 0;
  uint32_t lastData = millis();
  while (written < contentLength && millis() - lastData < 5000) {
    int n = client.read(buf, min(sizeof(buf), contentLength - written));
    if (n > 0) {
      if (Update.write(buf, n) != static_cast<size_t>(n)) break;
      written += n;
      lastData = millis();
    } else if (!client.connected()) {
      break;
    } else {
      delay(1);
    }
  }

  if (written == contentLength && Update.end(true)) {
    log("[ota] ok, rebooting");
    respond(client, "200 OK", "OTA ok, rebooting");
    delay(10);
    client.stop();
    delay(200);
    bootFailures = 0;  // intended restart, not a crash
    sseCloseAll();
    delay(100);
    ESP.restart();
  }

  log("[ota] failed after %u/%u bytes: %s", written, contentLength, Update.errorString());
  char msg[96];
  snprintf(msg, sizeof(msg), "OTA failed after %u/%u bytes: %s", written, contentLength, Update.errorString());
  Update.abort();
  respond(client, "500 Internal Server Error", msg);
  otaActive = false;
  ledSetPaused(false);
}

// POST /wifi (ssid, pass) and POST /wifi/forget. Returns true if Wi-Fi needs to reconnect.
static bool handleWifiConfig(Client &client, const char *path, size_t contentLength, uint32_t deadline) {
  if (strcmp(path, "/wifi/forget") == 0) {
    wifiSsid[0] = '\0';
    wifiPass[0] = '\0';
    saveWifiConfig();
    redirectHome(client);
    return true;
  }

  char body[256];
  if (contentLength >= sizeof(body) || !readBody(client, body, sizeof(body), contentLength, deadline)) {
    respond(client, "400 Bad Request", "invalid form data");
    return false;
  }

  char ssid[sizeof(wifiSsid)] = "";
  char pass[sizeof(wifiPass)] = "";
  formValue(body, "ssid", ssid, sizeof(ssid));
  formValue(body, "pass", pass, sizeof(pass));
  if (!ssid[0]) {
    respond(client, "400 Bad Request", "SSID missing");
    return false;
  }
  if (pass[0] && strlen(pass) < 8) {
    respond(client, "400 Bad Request", "WPA password must be at least 8 characters (leave empty for an open network)");
    return false;
  }

  // empty password field on the same SSID keeps the stored password
  if (pass[0] || strcmp(ssid, wifiSsid) != 0) strlcpy(wifiPass, pass, sizeof(wifiPass));
  strlcpy(wifiSsid, ssid, sizeof(wifiSsid));
  saveWifiConfig();
  redirectHome(client);
  return true;
}

static void setRelay(uint8_t idx, bool on) {
  if (relayOn[idx] == on) return;
  relayOn[idx] = on;
  digitalWrite(PIN_RELAY[idx], on);
  log("[relay] K%u %s", idx + 1, on ? "ON" : "OFF");
}

// Switches K1..K4 in the same instant: one write to the GPIO set/clear register (IO4..IO7 share GPIO_OUT_REG)
static void setAllRelays(bool on) {
  static_assert(PIN_RELAY[0] < 32 && PIN_RELAY[1] < 32 && PIN_RELAY[2] < 32 && PIN_RELAY[3] < 32,
                "relay pins must share GPIO_OUT_REG");
  uint32_t mask = 0;
  for (uint8_t i = 0; i < 4; i++) {
    mask |= 1UL << PIN_RELAY[i];
    relayOn[i] = on;
  }
  REG_WRITE(on ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, mask);
  log("[relay] K1..K4 %s", on ? "ON" : "OFF");
}

// Non-blocking relay test, advanced from loop(): even step = relay step/2 on, odd step = off
static int8_t testStep = -1;
static uint32_t testNextAt = 0;

static void relayTestStart() {
  setAllRelays(false);
  testStep = 0;
  testNextAt = millis();
}

static void relayTestCancel() {
  if (testStep < 0) return;
  if (testStep % 2) setRelay(testStep / 2, false);  // the relay the test switched on
  testStep = -1;
}

static void relayTestLoop() {
  if (testStep < 0 || millis() < testNextAt) return;
  uint8_t idx = testStep / 2;
  bool on = testStep % 2 == 0;
  setRelay(idx, on);
  testNextAt = millis() + (on ? 1000 : 300);
  if (++testStep >= 8) testStep = -1;
}

// Handles /relay..., returns false if the path is not a relay command
static bool handleRelay(const char *path) {
  if (strcmp(path, "/relay/test") == 0) {
    relayTestStart();
    return true;
  }
  if (strcmp(path, "/relay/off") == 0 || strncmp(path, "/relay/all?", 11) == 0) {
    const char *s = strstr(path, "s=");
    bool on = s && s[2] == '1';
    relayTestCancel();
    setAllRelays(on);
    return true;
  }
  if (strncmp(path, "/relay?", 7) != 0) return false;

  const char *n = strstr(path, "n=");
  const char *s = strstr(path, "s=");
  if (!n || !s) return true;
  int idx = atoi(n + 2) - 1;
  if (idx < 0 || idx > 3) return true;
  relayTestCancel();
  switch (s[2]) {
    case '1': setRelay(idx, true); break;
    case '0': setRelay(idx, false); break;
    case 't': setRelay(idx, !relayOn[idx]); break;
  }
  return true;
}

// Value of query parameter `key` ("?key=" or "&key="), nullptr if missing
static const char *queryParam(const char *path, const char *key) {
  size_t len = strlen(key);
  for (const char *p = strchr(path, '?'); p; p = strchr(p + 1, '&')) {
    if (strncmp(p + 1, key, len) == 0 && p[1 + len] == '=') return p + 2 + len;
  }
  return nullptr;
}

// Handles /led?s=..., returns false if the path is not an LED command
static bool handleLed(const char *path) {
  if (strncmp(path, "/led?", 5) != 0) return false;
  const char *v = queryParam(path, "s");
  int strip = v ? atoi(v) - 1 : -1;
  if (strip < 0 || strip >= LED_STRIPS) return true;
  LedConfig c = ledConfig(strip);
  if ((v = queryParam(path, "m"))) ledModeFromName(v, c.mode);
  if ((v = queryParam(path, "c"))) c.color = strtoul(v, nullptr, 16);
  if ((v = queryParam(path, "b"))) c.brightness = constrain(atoi(v), 0, 100);
  if ((v = queryParam(path, "n"))) c.count = constrain(atoi(v), 1, LED_MAX_COUNT);
  if ((v = queryParam(path, "t"))) c.type = strncmp(v, "ws2815", 6) == 0 ? LedType::WS2815 : LedType::WS2812;
  if ((v = queryParam(path, "w"))) c.tail = constrain(atoi(v), 0, LED_MAX_TAIL);
  if ((v = queryParam(path, "v"))) c.speed = constrain(atoi(v), 1, LED_MAX_SPEED);
  if ((v = queryParam(path, "r"))) c.sparkleRate = constrain(atoi(v), 1, LED_MAX_SPARKLE_RATE);
  if ((v = queryParam(path, "f"))) c.sparkleFadeMs = constrain(atoi(v), LED_MIN_SPARKLE_FADE, LED_MAX_SPARKLE_FADE);
  if ((v = queryParam(path, "d"))) {
    c.dir = strncmp(v, "out", 3) == 0 ? LedChaseDir::Out : strncmp(v, "in", 2) == 0 ? LedChaseDir::In : LedChaseDir::Line;
  }
  ledSetConfig(strip, c);
  return true;
}

// effect a PIR action currently shows on the strip, "" if none
static const char *triggerName(uint8_t strip) {
  LedMode effect;
  return ledTriggered(strip, effect) ? ledModeName(effect) : "";
}

// "1", "2", "1,2" or "": which inputs have an LED action for this strip
static const char *controllingPirs(uint8_t strip) {
  static char buf[8];
  ActionTable actions;
  actionsGet(actions);
  buf[0] = '\0';
  for (uint8_t p = 0; p < PIR_COUNT; p++) {
    for (const PirAction &a : actions[p]) {
      if (a.type != ActionType::Led || a.strip != strip) continue;
      size_t len = strlen(buf);
      snprintf(buf + len, sizeof(buf) - len, "%s%u", len ? "," : "", p + 1);
    }
  }
  return buf;
}

// "ca. 1,4 A an 12 V" for the page and the JSON
static void ledCurrentText(uint8_t strip, char *buf, size_t size) {
  uint32_t ma = ledCurrentMa(strip);
  if (ma < 100) snprintf(buf, size, "ca. %u mA an %u V", ma, ledSupplyVolts(strip));
  else snprintf(buf, size, "ca. %u,%u A an %u V", ma / 1000, ma % 1000 / 100, ledSupplyVolts(strip));
}

// Handles /pir/..., returns false if the path is not a PIR command
static bool handlePir(const char *path) {
  if (strcmp(path, "/pir/reset") == 0) {
    pirResetCounts();
    return true;
  }
  if (strncmp(path, "/pir/action?", 12) == 0) {
    const char *v = queryParam(path, "n");
    int pir = v ? atoi(v) - 1 : -1;
    if (pir < 0 || pir >= PIR_COUNT) return true;
    PirAction a;
    if ((v = queryParam(path, "add")) && actionFromName(v, a)) {
      if (a.type == ActionType::Led) a.brightness = ledConfig(a.strip).brightness;  // starts like the strip
      actionAdd(pir, a);
    }
    if ((v = queryParam(path, "del"))) actionRemove(pir, atoi(v));
    const char *idx = queryParam(path, "i");
    if (idx && (v = queryParam(path, "h"))) actionSetHold(pir, atoi(idx), constrain(atoi(v), 0, ACTION_MAX_HOLD));
    LedMode effect;
    if (idx && (v = queryParam(path, "e")) && ledModeFromName(v, effect)) actionSetEffect(pir, atoi(idx), effect);
    if (idx && (v = queryParam(path, "b"))) actionSetBrightness(pir, atoi(idx), constrain(atoi(v), 0, 100));
    const char *fi = queryParam(path, "fi"), *fo = queryParam(path, "fo");
    if (idx && (fi || fo)) {
      actionSetFades(pir, atoi(idx), fi ? constrain(atoi(fi), 0, ACTION_MAX_FADE) : -1,
                     fo ? constrain(atoi(fo), 0, ACTION_MAX_FADE) : -1);
    }
    return true;
  }
  if (strncmp(path, "/pir/polarity?", 14) != 0) return false;
  const char *n = strstr(path, "n=");
  const char *h = strstr(path, "h=");
  int idx = n ? atoi(n + 2) - 1 : -1;
  if (idx >= 0 && idx < PIR_COUNT && h) {
    pirSetActiveHigh(idx, h[2] == '1');
    savePirConfig();
  }
  return true;
}

static uint8_t queryVolume(const char *path) {
  const char *v = strstr(path, "v=");
  return v ? constrain(atoi(v + 2), 0, 100) : audioVolume();  // the audio module clamps to the limit
}

// Handles /audio/..., returns false if the path is not an audio command
static bool handleAudio(const char *path) {
  const char *f = strstr(path, "f=");
  if (f && (strncmp(path, "/audio/play?", 12) == 0 || strncmp(path, "/audio/freq?", 12) == 0)) {
    audioSetFrequency(atof(f + 2));
  }
  if (strncmp(path, "/audio/freq?", 12) == 0) return true;
  if (strncmp(path, "/audio/play?", 12) == 0) {
    const char *c = strstr(path, "c=");
    AudioClip clip = c ? audioClipFromName(c + 2) : AudioClip::None;
    if (clip != AudioClip::None) audioPlay(clip, queryVolume(path));
    return true;
  }
  if (strcmp(path, "/audio/stop") == 0) {
    audioStop();
    return true;
  }
  if (strncmp(path, "/audio/volume?", 14) == 0) {
    audioSetVolume(queryVolume(path));
    return true;
  }
  if (strncmp(path, "/audio/limit?", 13) == 0) {
    audioSetVolumeLimit(queryVolume(path));
    return true;
  }
  return false;
}

// Human-readable status, shared by the HTML page and /api/status
struct StatusText {
  bool online;
  char eth[56];
  bool ethWarn;
  char wifi[72];
  bool wifiWarn;
  char wifiSig[24];
  char uptime[24];
  char stream[96];
  bool streamOn;
};

static void buildStatusText(StatusText &t) {
  bool wifiUp = wifiOnline();
  EthState eth = ethState();
  t.online = eth == EthState::Online || wifiUp;

  t.ethWarn = false;
  switch (eth) {
    case EthState::Online: {
      IPAddress ip = ethLocalIP();
      int mbps;
      bool full;
      if (ethLinkInfo(mbps, full)) {
        snprintf(t.eth, sizeof(t.eth), "%u.%u.%u.%u, %d Mbit/s %s", ip[0], ip[1], ip[2], ip[3], mbps,
                 full ? "Vollduplex" : "Halbduplex");
      } else {
        snprintf(t.eth, sizeof(t.eth), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      }
      break;
    }
    case EthState::NoLink:     strlcpy(t.eth, "Kein Kabel", sizeof(t.eth)); t.ethWarn = true; break;
    case EthState::Dhcp:       strlcpy(t.eth, "Wartet auf IP-Adresse …", sizeof(t.eth)); break;
    case EthState::DhcpFailed: strlcpy(t.eth, "Keine IP-Adresse vom Router", sizeof(t.eth)); t.ethWarn = true; break;
    case EthState::HwError:    strlcpy(t.eth, "W5500 antwortet nicht", sizeof(t.eth)); t.ethWarn = true; break;
  }

  t.wifiWarn = false;
  t.wifiSig[0] = '\0';
  if (!wifiSsid[0]) {
    strlcpy(t.wifi, "Nicht eingerichtet", sizeof(t.wifi));
  } else if (wifiUp) {
    IPAddress ip = WiFi.localIP();
    snprintf(t.wifi, sizeof(t.wifi), "%u.%u.%u.%u in %s", ip[0], ip[1], ip[2], ip[3], wifiSsid);
    snprintf(t.wifiSig, sizeof(t.wifiSig), "Signal −%d dBm", -WiFi.RSSI());
  } else if (wifiConnecting()) {
    snprintf(t.wifi, sizeof(t.wifi), "Verbindet mit %s …", wifiSsid);
  } else {
    snprintf(t.wifi, sizeof(t.wifi), "Keine Verbindung zu %s", wifiSsid);
    t.wifiWarn = true;
  }

  uint32_t s = millis() / 1000;
  uint32_t d = s / 86400, h = s / 3600 % 24, m = s / 60 % 60;
  if (d) snprintf(t.uptime, sizeof(t.uptime), "%lu d %lu h", d, h);
  else if (h) snprintf(t.uptime, sizeof(t.uptime), "%lu h %lu min", h, m);
  else if (m) snprintf(t.uptime, sizeof(t.uptime), "%lu min %lu s", m, s % 60);
  else snprintf(t.uptime, sizeof(t.uptime), "%lu s", s);

  AudioStreamStats as = audioStreamStats();
  RtpStats rs = rtpStats();
  t.streamOn = as.active;
  if (as.active) {
    snprintf(t.stream, sizeof(t.stream), "Läuft von %u.%u.%u.%u, Puffer %.0f ms + Ausgabe %.0f ms",
             rs.sender[0], rs.sender[1], rs.sender[2], rs.sender[3], as.bufferMs, as.outputMs);
  } else {
    snprintf(t.stream, sizeof(t.stream), "Kein Stream, wartet auf UDP-Port %u", rtpPort());
  }
}

// Appends to a fixed buffer; output is truncated, never overflows
class TextBuf {
 public:
  TextBuf(char *buf, size_t cap) : buf_(buf), cap_(cap) { buf_[0] = '\0'; }
  void add(const char *fmt, ...) {
    if (len_ >= cap_ - 1) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf_ + len_, cap_ - len_, fmt, args);
    va_end(args);
    if (n > 0) len_ = min(len_ + n, cap_ - 1);
  }
  void addJsonString(const char *s) {
    add("\"");
    for (; *s; s++) {
      if (*s == '"' || *s == '\\') add("\\%c", *s);
      else if (static_cast<uint8_t>(*s) < 0x20) add("\\u%04x", *s);
      else add("%c", *s);
    }
    add("\"");
  }
  const char *c_str() const { return buf_; }

 private:
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
};

static void buildJsonStatus(TextBuf &j) {
  StatusText t;
  buildStatusText(t);
  j.add("{\"online\":%s,\"eth\":", t.online ? "true" : "false");
  j.addJsonString(t.eth);
  j.add(",\"ethWarn\":%s,\"wifi\":", t.ethWarn ? "true" : "false");
  j.addJsonString(t.wifi);
  j.add(",\"wifiWarn\":%s,\"wifiSig\":", t.wifiWarn ? "true" : "false");
  j.addJsonString(t.wifiSig);
  j.add(",\"relays\":[%d,%d,%d,%d],\"test\":%s,\"uptime\":", relayOn[0], relayOn[1], relayOn[2], relayOn[3],
        testStep >= 0 ? "true" : "false");
  j.addJsonString(t.uptime);
  j.add(",\"heap\":\"%u KB\",\"audio\":\"%s\",\"volume\":%u,\"limit\":%u,\"freq\":%.1f,\"streamText\":",
        ESP.getFreeHeap() / 1024, audioClipName(audioPlaying()), audioVolume(), audioVolumeLimit(), audioFrequency());
  j.addJsonString(t.stream);
  AudioStreamStats as = audioStreamStats();
  RtpStats rs = rtpStats();
  j.add(",\"stream\":{\"active\":%s,\"bufferMs\":%.1f,\"targetMs\":%.1f,\"outputMs\":%.1f,\"jitterMs\":%.2f,"
        "\"packets\":%u,\"lost\":%u,\"late\":%u,\"underruns\":%u,\"overflows\":%u,\"framesPerPacket\":%u}",
        as.active ? "true" : "false", as.bufferMs, as.targetMs, as.outputMs, rs.jitterMs, rs.packets, rs.lost, rs.late,
        as.underruns, as.overflows, rs.frames);
  j.add(",\"pir\":[");
  for (uint8_t i = 0; i < PIR_COUNT; i++) {
    uint32_t last = pirLastMs(i);
    j.add("%s{\"active\":%s,\"level\":%d,\"activeHigh\":%s,\"count\":%u,\"agoS\":%d,\"actions\":[", i ? "," : "",
          pirActive(i) ? "true" : "false", pirLevel(i), pirActiveHigh(i) ? "true" : "false", pirCount(i),
          last ? static_cast<int>((millis() - last) / 1000) : -1);
    ActionTable actions;
    actionsGet(actions);
    for (uint8_t k = 0; k < ACTION_MAX && actions[i][k].type != ActionType::None; k++) {
      const PirAction &a = actions[i][k];
      if (a.type == ActionType::Led) {
        j.add("%s{\"t\":\"%s\",\"h\":%u,\"e\":\"%s\",\"b\":%u,\"fi\":%u,\"fo\":%u,\"on\":%s}", k ? "," : "",
              actionName(a), a.holdS, ledModeName(a.effect), a.brightness, a.fadeInMs, a.fadeOutMs,
              actionActive(i, k, millis()) ? "true" : "false");
      } else {
        j.add("%s{\"t\":\"%s\"}", k ? "," : "", actionName(a));
      }
    }
    j.add("]}");
  }
  j.add("]");
  j.add(",\"leds\":[");
  for (uint8_t i = 0; i < LED_STRIPS; i++) {
    LedConfig c = ledConfig(i);
    char current[32];
    ledCurrentText(i, current, sizeof(current));
    j.add("%s{\"mode\":\"%s\",\"color\":\"%06x\",\"brightness\":%u,\"count\":%u,\"type\":\"%s\",\"tail\":%u,"
          "\"speed\":%u,\"dir\":\"%s\",\"rate\":%u,\"fade\":%u,\"trig\":\"%s\",\"pirs\":\"%s\","
          "\"current\":\"%s\",\"fps\":%.0f}",
          i ? "," : "", ledModeName(c.mode), c.color, c.brightness, c.count,
          c.type == LedType::WS2815 ? "ws2815" : "ws2812", c.tail, c.speed, ledChaseDirName(c.dir),
          c.sparkleRate, c.sparkleFadeMs, triggerName(i), controllingPirs(i), current, ledFps(i));
  }
  j.add("],\"ledMaxFrameGapMs\":%u", ledMaxFrameGapMs());
  j.add(",\"w5500\":{\"frames\":%u,\"wakeups\":%u,\"maxPending\":%u}", oh_w5500_frames, oh_w5500_wakeups,
        oh_w5500_max_pending);
  j.add("}");
}

static void sendJsonStatus(Client &client) {
  char buf[3072];
  TextBuf j(buf, sizeof(buf));
  buildJsonStatus(j);
  client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  client.print(j.c_str());
}

// What the page shows live. Pushed to SSE clients as soon as any of it changes; everything else
// (uptime, heap, stream buffer levels) rides along with the heartbeat.
struct LiveState {
  uint8_t relays;
  int8_t testStep;
  uint8_t pirActive;
  uint8_t pirActiveHigh;
  uint32_t pirCounts;
  uint8_t eth;
  uint8_t wifi;
  uint8_t audio;
  uint8_t volume;
  uint8_t limit;
  uint8_t streamActive;
  struct {
    uint32_t color;
    uint16_t count, speed, rate, fade;
    uint8_t mode, brightness, type, tail, dir, trig;
  } leds[LED_STRIPS];
  uint32_t actions;  // actionsVersion()
};

static void readLiveState(LiveState &s) {
  memset(&s, 0, sizeof(s));  // padding too, the struct is compared with memcmp
  for (uint8_t i = 0; i < 4; i++) s.relays |= relayOn[i] << i;
  s.testStep = testStep;
  for (uint8_t i = 0; i < PIR_COUNT; i++) {
    s.pirActive |= pirActive(i) << i;
    s.pirActiveHigh |= pirActiveHigh(i) << i;
    s.pirCounts += pirCount(i) << (16 * i);
  }
  s.eth = static_cast<uint8_t>(ethState());
  s.wifi = wifiOnline() ? 2 : wifiConnecting() ? 1 : 0;
  s.audio = static_cast<uint8_t>(audioPlaying());
  s.volume = audioVolume();
  s.limit = audioVolumeLimit();
  s.streamActive = audioStreamStats().active;
  for (uint8_t i = 0; i < LED_STRIPS; i++) {  // field by field, so the zeroed padding stays zero
    LedConfig c = ledConfig(i);
    s.leds[i].color = c.color;
    s.leds[i].count = c.count;
    s.leds[i].mode = static_cast<uint8_t>(c.mode);
    s.leds[i].brightness = c.brightness;
    s.leds[i].type = static_cast<uint8_t>(c.type);
    s.leds[i].tail = c.tail;
    s.leds[i].speed = c.speed;
    s.leds[i].dir = static_cast<uint8_t>(c.dir);
    s.leds[i].rate = c.sparkleRate;
    s.leds[i].fade = c.sparkleFadeMs;
    LedMode effect;
    s.leds[i].trig = ledTriggered(i, effect) ? 1 + static_cast<uint8_t>(effect) : 0;
  }
  s.actions = actionsVersion();
}

static bool forceEventPush = false;

static void pushEvents() {
  static LiveState last;
  static uint32_t lastPush = 0;
  if (!sseClients()) return;
  LiveState now;
  readLiveState(now);
  uint32_t heartbeatMs = now.streamActive ? 1000 : 5000;
  if (!forceEventPush && memcmp(&now, &last, sizeof(now)) == 0 && millis() - lastPush < heartbeatMs) return;
  forceEventPush = false;
  last = now;
  lastPush = millis();
  char buf[3072];
  TextBuf j(buf, sizeof(buf));
  buildJsonStatus(j);
  sseBroadcast(j.c_str());
}

// Board look: DIN-rail grey panel, amber = on/online (yellow board LED), red = fault (red board LED)
static const char PAGE_HEAD[] = R"html(<!doctype html><html lang=de><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<meta name=theme-color content="#E4E6E1" media="(prefers-color-scheme: light)">
<meta name=theme-color content="#1C1F22" media="(prefers-color-scheme: dark)">
<title>OH Node V0</title>
<style>
:root{--bg:#E4E6E1;--face:#F7F8F5;--ink:#22262A;--mute:#5B615C;--line:#B4B9B0;--amber:#F2A900;--amber-edge:#A87500;--red:#C8322B;color-scheme:light dark}
@media (prefers-color-scheme:dark){:root{--bg:#1C1F22;--face:#272B2F;--ink:#E9EBE6;--mute:#9BA29C;--line:#474D52;--amber-edge:#FFC94D;--red:#F06A5F}}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:17px/1.45 ui-rounded,"SF Pro Rounded",system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;font-variant-numeric:tabular-nums;-webkit-text-size-adjust:100%}
main{max-width:46rem;margin:0 auto;padding:1.25rem 1rem 3rem}
header{display:flex;align-items:center;gap:.65rem}
h1{font-size:1.6rem;line-height:1.2;margin:0}
h2{font-size:1.15rem;margin:2.25rem 0 .8rem}
.led{flex:none;width:.85rem;height:.85rem;border-radius:50%;background:var(--red);box-shadow:0 0 0 4px color-mix(in srgb,var(--red) 22%,transparent)}
.led.on{background:var(--amber);box-shadow:0 0 0 4px color-mix(in srgb,var(--amber) 30%,transparent)}
.lost{margin:.9rem 0 0;padding:.7rem .9rem;color:var(--red);font-weight:700;border:2px solid var(--red);border-radius:8px}
dl{display:grid;grid-template-columns:auto 1fr;gap:.3rem 1rem;margin:.9rem 0 0}
dt{color:var(--mute)}
dd{margin:0;overflow-wrap:anywhere}
.sub{display:block;color:var(--mute);font-size:.88rem}
.sub:empty{display:none}
.warn{color:var(--red)}
form{margin:0}
.relays{display:grid;grid-template-columns:1fr 1fr;gap:.75rem}
@media (min-width:38rem){.relays{grid-template-columns:repeat(4,1fr)}}
.relay{width:100%;min-height:8.5rem;display:grid;grid-template:"name lever" 1fr "term lever" auto "state lever" auto/1fr auto;align-items:end;gap:.1rem .5rem;padding:.9rem;text-align:left;font:inherit;color:var(--ink);background:var(--face);border:2px solid var(--line);border-radius:10px;cursor:pointer;-webkit-tap-highlight-color:transparent;touch-action:manipulation;user-select:none}
.relay b{grid-area:name;align-self:start;font-size:2.1rem;line-height:1}
.relay small{grid-area:term;color:var(--mute);font-size:.85rem}
.relay span{grid-area:state;font-weight:700}
.relay i{grid-area:lever;align-self:center;position:relative;width:1.2rem;height:3.8rem;border:2px solid var(--line);border-radius:.6rem;background:var(--bg)}
.relay i::after{content:"";position:absolute;left:2px;right:2px;bottom:2px;height:42%;border-radius:.4rem;background:var(--mute)}
.relay.on{background:var(--amber);border-color:var(--amber-edge);color:#22262A}
.relay.on small{color:#3D3410}
.relay.on i{background:#FFF3CC;border-color:#22262A}
.relay.on i::after{bottom:auto;top:2px;background:#22262A}
.relay:active{transform:translateY(1px)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:.75rem;margin-top:.75rem}
.row form{display:flex}
.btn{width:100%;min-height:3.1rem;padding:.55rem 1rem;font:inherit;font-weight:700;line-height:1.2;color:var(--ink);background:transparent;border:2px solid var(--ink);border-radius:8px;cursor:pointer;touch-action:manipulation}
.btn.primary{color:var(--face);background:var(--ink)}
.btn.danger{color:var(--red);border-color:var(--red)}
.btn:disabled{opacity:.55;cursor:progress}
.stack{display:grid;gap:.75rem;margin-top:1.25rem}
label{display:block;font-weight:700;margin:1rem 0 .35rem}
input{width:100%;min-height:3.1rem;padding:.5rem .8rem;font:inherit;font-size:max(16px,1rem);color:var(--ink);background:var(--face);border:2px solid var(--line);border-radius:8px}
.hint{margin:.4rem 0 0;color:var(--mute);font-size:.88rem}
:focus-visible{outline:3px solid var(--amber-edge);outline-offset:2px}
.dev{font-size:.9rem}
.sensor{min-height:8.5rem;display:grid;grid-template-rows:1fr auto auto auto;gap:.1rem;padding:.9rem;background:var(--face);border:2px solid var(--line);border-radius:10px}
.sensor b{align-self:start;font-size:2.1rem;line-height:1}
.sensor small{color:var(--mute);font-size:.85rem}
.sensor span{font-weight:700}
.sensor em{font-style:normal;color:var(--mute);font-size:.85rem}
.sensor.on{background:var(--amber);border-color:var(--amber-edge);color:#22262A}
.sensor.on small,.sensor.on em{color:#3D3410}
.pirs{grid-template-columns:1fr 1fr}
.strip{padding:.9rem;background:var(--face);border:2px solid var(--line);border-radius:10px}
.strip+.strip{margin-top:.75rem}
h3{font-size:1.05rem;margin:0 0 .75rem;display:flex;justify-content:space-between;gap:.5rem;flex-wrap:wrap}
h3 small{font-weight:400;color:var(--mute);font-size:.88rem}
.modes{display:grid;grid-template-columns:repeat(3,1fr);gap:.5rem}
.modes .btn,.types .btn{padding:.4rem .3rem;min-height:2.9rem}
.fields{display:grid;grid-template-columns:1fr 1fr;gap:.75rem}
.fields label{margin-top:.9rem}
input[type=color]{padding:.25rem;height:3.1rem;cursor:pointer}
.types{display:grid;grid-template-columns:1fr 1fr;gap:.5rem;margin-top:.9rem}
.strip:not(.chase) .chaseonly,.strip:not(.sparkle) .sparkleonly{display:none}
.hint:empty{display:none}
.pacts{margin-top:1.4rem}
.act{margin-top:.75rem;padding:.75rem .9rem;background:var(--face);border:2px solid var(--line);border-radius:10px}
.act.on{border-color:var(--amber-edge)}
.acthead{display:flex;justify-content:space-between;align-items:center;gap:.75rem}
.acthead .btn{width:auto;min-height:2.6rem;padding:.3rem .8rem;font-size:.9rem}
.act .vol{margin-top:.6rem}
.effects{display:grid;grid-template-columns:repeat(3,1fr);gap:.5rem;margin-top:.6rem}
.effects .btn{padding:.35rem .25rem;min-height:2.7rem;font-size:.92rem}
.choices{display:grid;grid-template-columns:repeat(3,1fr);gap:.5rem;margin-top:.5rem}
.choices .btn{padding:.4rem .3rem;min-height:2.9rem}
.addrow{margin-top:.75rem}
.dirs{display:grid;grid-template-columns:repeat(3,1fr);gap:.5rem;margin-top:.5rem}
.dirs .btn{padding:.4rem .3rem;min-height:2.9rem}
.sounds{display:grid;grid-template-columns:1fr 1fr;gap:.75rem;margin-top:1rem}
@media (min-width:38rem){.sounds{grid-template-columns:repeat(4,1fr)}}
.sounds form{display:flex}
.btn.on{color:#22262A;background:var(--amber);border-color:var(--amber-edge)}
.vol{display:flex;justify-content:space-between;margin-top:0}
.vol+.vol,.fine+.vol,.hint+.vol{margin-top:1rem}
.hint+.vol{margin-top:1.4rem}
.fine{display:grid;grid-template-columns:3.1rem 1fr 3.1rem;gap:.75rem;align-items:center}
.fine .btn{padding:0;font-size:1.4rem}
input[type=range]{min-height:2.75rem;padding:0;border:0;background:none;accent-color:var(--amber-edge)}
</style>
<main>)html";

// Relay/test/all buttons via fetch without page reload. Commands run strictly one after another
// and carry the target state (not "toggle"), so fast repeated taps can't get out of sync.
// Live state arrives via Server-Sent Events (/api/events); polling /api/status is only the fallback.
static const char PAGE_SCRIPT[] = R"js(<script>
(()=>{const $=s=>document.querySelector(s),R=[...document.querySelectorAll('button.relay')],T=$('#test button'),A=$('#all button'),
V=$('#vol'),F=$('#freq'),L=$('#limit'),ST=[...document.querySelectorAll('.strip')],PB=[...document.querySelectorAll('button[data-p]')],P=[...document.querySelectorAll('#audio button[data-c]')];
let queue=Promise.resolve(),pending=0,gen=0,timer,es=null;
const anyOn=()=>R.some(b=>b.classList.contains('on'));
const tile=(b,on)=>{b.classList.toggle('on',on);b.setAttribute('aria-pressed',on);b.querySelector('span').textContent=on?'Ein':'Aus';A.textContent=anyOn()?'Alle aus':'Alle ein'};
const txt=(id,t,warn)=>{const e=$(id);e.textContent=t;if(warn!==undefined)e.classList.toggle('warn',warn)};
const schedule=ms=>{clearTimeout(timer);timer=setTimeout(poll,ms)};
function render(s,g){$('#lost').hidden=true;$('#led').classList.toggle('on',s.online);
txt('#eth',s.eth,s.ethWarn);txt('#stream',s.streamText);txt('#wifi',s.wifi,s.wifiWarn);txt('#wsig',s.wifiSig);txt('#up',s.uptime);txt('#heap',s.heap);
if(!pending&&g===gen)R.forEach((b,i)=>tile(b,!!s.relays[i]));
T.disabled=s.test;T.textContent=s.test?'Test läuft …':'Alle testen';
P.forEach(b=>b.classList.toggle('on',b.dataset.c===s.audio));
// sliders follow changes made elsewhere, except the one being dragged right now
if(document.activeElement!==L){L.value=s.limit;$('#limitv').textContent=s.limit+' %';V.max=s.limit}
if(document.activeElement!==V){V.value=s.volume;$('#volv').textContent=s.volume+' %'}
s.leds.forEach((l,i)=>{const st=ST[i],q=k=>st.querySelector('[data-k='+k+']');
st.querySelectorAll('[data-m]').forEach(b=>b.classList.toggle('on',b.dataset.m===l.mode));st.classList.toggle('chase',l.mode==='chase');st.classList.toggle('sparkle',l.mode==='sparkle');
q('lit').textContent=l.pirs?'Bewegungsmelder: PIR '+l.pirs.replace(',',' und ')+(l.trig?', zeigt gerade „'+EFF[l.trig]+'“':''):'';
if(document.activeElement!==q('r')){q('r').value=speedPos(l.rate);st.querySelector('[data-o=r]').textContent=l.rate+' Funken/s'}
if(document.activeElement!==q('f')){q('f').value=fadePos(l.fade);st.querySelector('[data-o=f]').textContent=fadeText(l.fade)}
if(document.activeElement!==q('w')){q('w').value=tailPos(l.tail);st.querySelector('[data-o=w]').textContent=l.tail+' LEDs'}
if(document.activeElement!==q('v')){q('v').value=speedPos(l.speed);st.querySelector('[data-o=v]').textContent=l.speed+' LEDs/s'}
st.querySelectorAll('[data-t]').forEach(b=>b.classList.toggle('on',b.dataset.t===l.type));
st.querySelectorAll('[data-d]').forEach(b=>b.classList.toggle('on',b.dataset.d===l.dir));
if(document.activeElement!==q('c'))q('c').value='#'+l.color;
if(document.activeElement!==q('n'))q('n').value=l.count;
if(document.activeElement!==q('b')){q('b').value=l.brightness;st.querySelector('output').textContent=l.brightness+' %'}
q('current').textContent=l.current;q('fps').textContent=l.fps>0?l.fps+' Bilder/s':'statisch'});
s.pir.forEach((p,i)=>{const t=$('#pir'+(i+1));t.classList.toggle('on',p.active);
t.querySelector('small').textContent=t.querySelector('small').textContent.replace(/(High|Low)$/,p.level?'High':'Low');
t.querySelector('span').textContent=p.active?'Bewegung':'Ruhe';t.querySelector('em').textContent=pirInfo(p);
PB[i].textContent='Aktiv bei '+(p.activeHigh?'High':'Low');PB[i].dataset.h=p.activeHigh?1:0;renderActs(i,p)});
if(!es)schedule(1000)}
function pirInfo(p){if(!p.count||p.agoS<0)return 'Noch nicht ausgelöst';const a=p.agoS;
return p.count+'× ausgelöst, zuletzt vor '+(a<60?a+' s':a<3600?Math.floor(a/60)+' min':Math.floor(a/3600)+' h')}
function lost(){$('#lost').hidden=false;$('#led').classList.remove('on');if(!es)schedule(3000)}
function poll(){const g=gen;fetch('/api/status',{cache:'no-store'}).then(r=>r.json()).then(s=>render(s,g),lost)}
function send(url){pending++;const g=++gen;queue=queue.then(()=>fetch(url,{method:'POST',headers:{Accept:'application/json'}}).then(r=>r.json()))
.then(s=>{pending--;render(s,g)},()=>{pending--;lost()})}
R.forEach((b,i)=>b.form.addEventListener('submit',e=>{e.preventDefault();const on=b.getAttribute('aria-pressed')!=='true';tile(b,on);send('/relay?n='+(i+1)+'&s='+(on?1:0))}));
$('#test').addEventListener('submit',e=>{e.preventDefault();T.disabled=true;T.textContent='Test läuft …';send('/relay/test')});
$('#all').addEventListener('submit',e=>{e.preventDefault();const on=!anyOn();R.forEach(b=>tile(b,on));send('/relay/all?s='+(on?1:0))});
// slider values: at most one request in flight, the newest value wins
const live=url=>{let busy=false,next=null;const go=v=>{busy=true;fetch(url+v,{method:'POST',headers:{Accept:'application/json'}}).catch(()=>{}).finally(()=>{busy=false;if(next!==null){const n=next;next=null;go(n)}})};return v=>{if(busy)next=v;else go(v)}};
const setVol=live('/audio/volume?v='),setFreq=live('/audio/freq?f='),setLimit=live('/audio/limit?v=');
// tail slider is quadratic (fine steps for short tails), speed slider logarithmic
// PIR actions: hold slider far left = 0 s, then logarithmic 1 s .. 1 h. The list is rebuilt only when
// its entries change, so a slider being dragged keeps its focus.
const holdVal=p=>+p?Math.max(1,Math.round(Math.pow(3600,p/1000))):0,holdPos=h=>h?Math.max(1,Math.round(1000*Math.log(h)/Math.log(3600))):0,
holdText=h=>h<60?h+' s':h<3600?Math.round(h/60)+' min':'1 h',
ACT={led1:'LED-Strip 1',led2:'LED-Strip 2',gong:'Gong'},
EFF={off:'Aus',color:'Farbe',rainbow:'Regenbogen',chase:'Lauflicht',sparkle:'Funkeln',test:'Test'},
// fade sliders: quadratic 0 .. 10 s, fine steps below one second
fadeVal2=p=>Math.round(10000*(p/1000)**2/50)*50,fadePos2=ms=>Math.round(1000*Math.sqrt(ms/10000)),
msText=ms=>ms?(ms/1000).toFixed(ms<1000?2:1).replace('.',',')+' s':'sofort',PA=[...document.querySelectorAll('.pacts')],holdSenders={};
function renderActs(i,p){const box=PA[i].querySelector('.actlist'),sig=p.actions.map(a=>a.t).join();
if(box.dataset.sig!==sig){box.dataset.sig=sig;box.innerHTML=p.actions.map((a,k)=>'<div class=act data-k='+k+'><div class=acthead><b>'+ACT[a.t]+
'</b><button type=button class=btn data-del='+k+'>Entfernen</button></div>'+(a.t==='gong'?'<p class=hint>Spielt bei jeder Auslösung, mit der eingestellten Lautstärke.</p>':
'<div class=effects>'+Object.entries(EFF).map(([m,t])=>'<button type=button class=btn data-e='+m+'>'+t+'</button>').join('')+'</div>'+
'<label class=vol>Helligkeit <output data-o=b></output></label><input type=range min=0 max=100 data-ab>'+
'<label class=vol>Einblenden <output data-o=fi></output></label><input type=range min=0 max=1000 data-f=fi>'+
'<label class=vol>Nachlaufzeit <output data-o=h></output></label><input type=range min=0 max=1000 data-h='+k+'>'+
'<label class=vol>Ausblenden <output data-o=fo></output></label><input type=range min=0 max=1000 data-f=fo>'+
'<p class=hint data-on></p>')+'</div>').join('')||
'<p class=hint>Noch keine Aktion.</p>'}
p.actions.forEach((a,k)=>{if(a.t==='gong')return;const row=box.querySelector('.act[data-k="'+k+'"]'),r=row.querySelector('[data-h]');
if(document.activeElement!==r){r.value=holdPos(a.h);row.querySelector('[data-o=h]').textContent=holdText(a.h)}
const ab=row.querySelector('[data-ab]');if(document.activeElement!==ab){ab.value=a.b;row.querySelector('[data-o=b]').textContent=a.b+' %'}
['fi','fo'].forEach(f=>{const x=row.querySelector('[data-f='+f+']');if(document.activeElement!==x){x.value=fadePos2(a[f]);row.querySelector('[data-o='+f+']').textContent=msText(a[f])}});
row.querySelectorAll('[data-e]').forEach(b=>b.classList.toggle('on',b.dataset.e===a.e));
row.classList.toggle('on',a.on);row.querySelector('[data-on]').textContent=a.on?'Ausgelöst: Strip zeigt „'+EFF[a.e]+'“':'Wartet auf Bewegung'});
PA[i].querySelectorAll('.choices [data-a]').forEach(b=>b.hidden=p.actions.some(a=>a.t===b.dataset.a));
PA[i].querySelector('[data-add]').hidden=p.actions.length>=3}
PA.forEach((pa,i)=>{const n=i+1,ch=pa.querySelector('.choices');
pa.querySelector('[data-add]').addEventListener('click',()=>ch.hidden=!ch.hidden);
ch.addEventListener('click',e=>{const b=e.target.closest('[data-a]');if(!b)return;ch.hidden=true;send('/pir/action?n='+n+'&add='+b.dataset.a)});
pa.querySelector('.actlist').addEventListener('click',e=>{const b=e.target.closest('[data-del]');if(b)return send('/pir/action?n='+n+'&del='+b.dataset.del);
const f=e.target.closest('[data-e]');if(!f)return;const row=f.closest('.act');row.querySelectorAll('[data-e]').forEach(x=>x.classList.toggle('on',x===f));
send('/pir/action?n='+n+'&i='+row.dataset.k+'&e='+f.dataset.e)});
pa.querySelector('.actlist').addEventListener('input',e=>{const r=e.target.closest('[data-h]');if(!r)return;const h=holdVal(r.value),k=r.dataset.h;
r.parentElement.querySelector('[data-o=h]').textContent=holdText(h);(holdSenders[n+'/'+k]||=live('/pir/action?n='+n+'&i='+k+'&h='))(h)});
pa.querySelector('.actlist').addEventListener('input',e=>{const r=e.target.closest('[data-ab]');if(!r)return;const k=r.closest('.act').dataset.k;
r.closest('.act').querySelector('[data-o=b]').textContent=r.value+' %';(holdSenders[n+'/'+k+'b']||=live('/pir/action?n='+n+'&i='+k+'&b='))(r.value)});
pa.querySelector('.actlist').addEventListener('input',e=>{const r=e.target.closest('[data-f]');if(!r)return;const f=r.dataset.f,ms=fadeVal2(r.value),
k=r.closest('.act').dataset.k;r.closest('.act').querySelector('[data-o='+f+']').textContent=msText(ms);
(holdSenders[n+'/'+k+f]||=live('/pir/action?n='+n+'&i='+k+'&'+f+'='))(ms)})});
// sparkle fade slider: logarithmic 0.1 .. 30 s
const fadeVal=p=>Math.round(100*Math.pow(300,p/1000)),fadePos=f=>Math.round(1000*Math.log(f/100)/Math.log(300)),
fadeText=f=>(f/1000).toFixed(1).replace('.',',')+' s';
const tailVal=p=>Math.round(255*(p/1000)**2),tailPos=w=>Math.round(1000*Math.sqrt(w/255));
const speedVal=p=>Math.max(1,Math.round(Math.pow(500,p/1000))),speedPos=v=>Math.round(1000*Math.log(v)/Math.log(500));
let hz=+F.dataset.hz;
const fmt=f=>f<1000?Math.round(f)+' Hz':(f/1000).toFixed(f<10000?2:1).replace('.',',')+' kHz';
const showHz=()=>{$('#freqv').textContent=fmt(hz)};
const useHz=f=>{hz=Math.min(20000,Math.max(20,f));F.value=Math.round(1000*Math.log(hz/20)/Math.log(1000));showHz();setFreq(hz.toFixed(1))};
showHz();
F.addEventListener('input',()=>useHz(20*Math.pow(1000,F.value/1000)));
$('#fdn').addEventListener('click',()=>useHz(hz/1.01));
$('#fup').addEventListener('click',()=>useHz(hz*1.01));
V.addEventListener('input',()=>{$('#volv').textContent=V.value+' %';setVol(V.value)});
L.addEventListener('input',()=>{$('#limitv').textContent=L.value+' %';V.max=L.value;$('#volv').textContent=V.value+' %';setLimit(L.value)});
P.forEach(b=>b.form.addEventListener('submit',e=>{e.preventDefault();P.forEach(x=>x.classList.toggle('on',x===b));send('/audio/play?c='+b.dataset.c+'&v='+V.value+'&f='+hz.toFixed(1))}));
$('#astop').addEventListener('submit',e=>{e.preventDefault();P.forEach(x=>x.classList.remove('on'));send('/audio/stop')});
PB.forEach((b,i)=>{b.dataset.h=/High$/.test(b.textContent)?1:0;b.form.addEventListener('submit',e=>{e.preventDefault();
const h=b.dataset.h==='1'?0:1;b.textContent='Aktiv bei '+(h?'High':'Low');b.dataset.h=h;send('/pir/polarity?n='+(i+1)+'&h='+h)})});
ST.forEach(st=>{const base='/led?s='+st.dataset.s,q=k=>st.querySelector('[data-k='+k+']');
const setB=live(base+'&b='),setC=live(base+'&c=');
st.querySelectorAll('[data-m]').forEach(b=>b.addEventListener('click',()=>{st.classList.toggle('chase',b.dataset.m==='chase');
st.classList.toggle('sparkle',b.dataset.m==='sparkle');
st.querySelectorAll('[data-m]').forEach(x=>x.classList.toggle('on',x===b));send(base+'&m='+b.dataset.m)}));
const setW=live(base+'&w='),setV=live(base+'&v='),setR=live(base+'&r='),setF=live(base+'&f=');
q('r').addEventListener('input',()=>{const r=speedVal(q('r').value);st.querySelector('[data-o=r]').textContent=r+' Funken/s';setR(r)});
q('f').addEventListener('input',()=>{const f=fadeVal(q('f').value);st.querySelector('[data-o=f]').textContent=fadeText(f);setF(f)});
q('w').addEventListener('input',()=>{const w=tailVal(q('w').value);st.querySelector('[data-o=w]').textContent=w+' LEDs';setW(w)});
q('v').addEventListener('input',()=>{const v=speedVal(q('v').value);st.querySelector('[data-o=v]').textContent=v+' LEDs/s';setV(v)});
st.querySelectorAll('[data-t]').forEach(b=>b.addEventListener('click',()=>{
st.querySelectorAll('[data-t]').forEach(x=>x.classList.toggle('on',x===b));send(base+'&t='+b.dataset.t)}));
st.querySelectorAll('[data-d]').forEach(b=>b.addEventListener('click',()=>{
st.querySelectorAll('[data-d]').forEach(x=>x.classList.toggle('on',x===b));send(base+'&d='+b.dataset.d)}));
q('b').addEventListener('input',()=>{st.querySelector('output').textContent=q('b').value+' %';setB(q('b').value)});
q('c').addEventListener('input',()=>setC(q('c').value.slice(1)));
q('n').addEventListener('change',()=>send(base+'&n='+q('n').value))});
$('#pirreset').addEventListener('submit',e=>{e.preventDefault();send('/pir/reset')});
// live updates: Server-Sent Events, polling only where EventSource is missing.
// The ESP sends at least every 5 s; 12 s of silence means a dead connection (power loss, pulled
// cable), which the browser itself would only notice after a long TCP timeout.
let seen=Date.now();
function connect(){es=new EventSource('/api/events');es.onmessage=e=>{seen=Date.now();render(JSON.parse(e.data),gen)};es.onerror=lost}
if(window.EventSource){connect();setInterval(()=>{if(Date.now()-seen>12000){es.close();lost();seen=Date.now();connect()}},2000)}
else schedule(1000)})();
</script>)js";

// "3× ausgelöst, zuletzt vor 12 s" / "Noch nicht ausgelöst"
static void pirInfoText(uint8_t idx, char *buf, size_t size) {
  uint32_t count = pirCount(idx), last = pirLastMs(idx);
  if (!count || !last) {
    strlcpy(buf, "Noch nicht ausgelöst", size);
    return;
  }
  uint32_t ago = (millis() - last) / 1000;
  if (ago < 60) snprintf(buf, size, "%u× ausgelöst, zuletzt vor %u s", count, ago);
  else if (ago < 3600) snprintf(buf, size, "%u× ausgelöst, zuletzt vor %u min", count, ago / 60);
  else snprintf(buf, size, "%u× ausgelöst, zuletzt vor %u h", count, ago / 3600);
}

static bool anyRelayOn() {
  for (bool on : relayOn) if (on) return true;
  return false;
}

static void sendStatus(Client &client) {
  StatusText t;
  buildStatusText(t);

  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  client.print(PAGE_HEAD);
  if (safeMode) {
    client.print("<p class=lost role=alert>Sicherer Modus nach wiederholten Abstürzen beim Start: Ethernet und Audio "
                 "sind aus. Neue Firmware per OTA einspielen.</p>");
  }
  client.printf("<header><span id=led class='led%s'></span><h1>OH Node V0</h1></header>"
                "<p id=lost class=lost role=alert hidden>Keine Verbindung zum Gerät. Verbinde neu …</p>"
                "<dl><dt>Ethernet<dd id=eth class='%s'>%s<dt>WLAN<dd><span id=wifi class='%s'>",
                t.online ? " on" : "", t.ethWarn ? "warn" : "", t.eth, t.wifiWarn ? "warn" : "");
  printHtmlEscaped(client, t.wifi);
  client.printf("</span><span id=wsig class=sub>%s</span></dl><h2>Relais</h2><div class=relays>", t.wifiSig);

  static const char *jacks[] = {"J2", "J4", "J5", "J6"};
  for (uint8_t i = 0; i < 4; i++) {
    client.printf("<form method=post action='/relay?n=%u&amp;s=t'><button class='relay%s' aria-pressed=%s>"
                  "<b>K%u</b><small>Klemme %s</small><span>%s</span><i></i></button></form>",
                  i + 1, relayOn[i] ? " on" : "", relayOn[i] ? "true" : "false", i + 1, jacks[i], relayOn[i] ? "Ein" : "Aus");
  }
  client.printf("</div><div class=row>"
                "<form id=test method=post action=/relay/test><button class=btn%s>%s</button></form>"
                "<form id=all method=post action='/relay/all?s=%d'><button class=btn>%s</button></form>"
                "</div><p class=hint>Der Test schaltet K1 bis K4 nacheinander für je eine Sekunde ein.</p>",
                testStep >= 0 ? " disabled" : "", testStep >= 0 ? "Test läuft …" : "Alle testen",
                anyRelayOn() ? 0 : 1, anyRelayOn() ? "Alle aus" : "Alle ein");

  static const char *pirJacks[] = {"J8", "J10"};
  client.print("<h2>Bewegungsmelder</h2><div class='relays pirs'>");
  for (uint8_t i = 0; i < PIR_COUNT; i++) {
    char info[48];
    pirInfoText(i, info, sizeof(info));
    client.printf("<div class='sensor%s' id=pir%u><b>PIR %u</b><small>Klemme %s, IO%u, Pegel %s</small>"
                  "<span>%s</span><em>%s</em></div>",
                  pirActive(i) ? " on" : "", i + 1, i + 1, pirJacks[i], PIN_PIR[i], pirLevel(i) ? "High" : "Low",
                  pirActive(i) ? "Bewegung" : "Ruhe", info);
  }
  client.print("</div><div class=row>");
  for (uint8_t i = 0; i < PIR_COUNT; i++) {
    client.printf("<form method=post action='/pir/polarity?n=%u&amp;h=%d'><button class=btn data-p=%u>"
                  "Aktiv bei %s</button></form>",
                  i + 1, pirActiveHigh(i) ? 0 : 1, i + 1, pirActiveHigh(i) ? "High" : "Low");
  }
  client.print("</div><div class=stack><form id=pirreset method=post action=/pir/reset>"
               "<button class=btn>Zähler zurücksetzen</button></form></div>"
               "<p class=hint>Klemme: 1 = 12 V (höchstens 200 mA), 2 = GND, 3 = Signal mit 10-kΩ-Pull-up auf 3,3 V. "
               "Melder mit Relais- oder Open-Collector-Ausgang direkt anschließen. "
               "Melder, deren Ausgang selbst 12 V liefert, nicht.</p>"
               "<p class=hint>„Aktiv bei Low“ (Standard): Schließer oder Open-Collector gegen GND. "
               "„Aktiv bei High“: Öffner gegen GND (meldet auch Kabelbruch) oder Module wie der HC-SR501.</p>");
  // action lists are filled in by the page script from the status JSON
  for (uint8_t i = 0; i < PIR_COUNT; i++) {
    client.printf("<div class=pacts data-pir=%u><h3>PIR %u löst aus</h3><div class=actlist></div>"
                  "<div class=addrow><button type=button class=btn data-add>+ Aktion hinzufügen</button>"
                  "<div class=choices hidden><button type=button class=btn data-a=led1>LED-Strip 1</button>"
                  "<button type=button class=btn data-a=led2>LED-Strip 2</button>"
                  "<button type=button class=btn data-a=gong>Gong</button></div></div></div>",
                  i + 1, i + 1);
  }

  static const char *ledJacks[] = {"J7", "J9"};
  static const struct { LedMode mode; const char *label; } ledModes[] = {
      {LedMode::Off, "Aus"}, {LedMode::Color, "Farbe"}, {LedMode::Rainbow, "Regenbogen"},
      {LedMode::Chase, "Lauflicht"}, {LedMode::Sparkle, "Funkeln"}, {LedMode::Test, "Test"}};
  client.print("<h2>LED-Strips</h2>");
  for (uint8_t i = 0; i < LED_STRIPS; i++) {
    LedConfig c = ledConfig(i);
    char current[32], fpsText[24];
    ledCurrentText(i, current, sizeof(current));
    if (ledFps(i) > 0) snprintf(fpsText, sizeof(fpsText), "%.0f Bilder/s", ledFps(i));
    else strlcpy(fpsText, "statisch", sizeof(fpsText));
    client.printf("<div class='strip%s' data-s=%u><h3>Strip %u <small>Klemme %s, IO%u</small></h3>"
                  "<p class=hint data-k=lit></p><div class=modes>",
                  c.mode == LedMode::Chase ? " chase" : c.mode == LedMode::Sparkle ? " sparkle" : "", i + 1, i + 1,
                  ledJacks[i], PIN_LED_DATA[i]);
    for (const auto &m : ledModes) {
      client.printf("<button type=button class='btn%s' data-m=%s>%s</button>", c.mode == m.mode ? " on" : "",
                    ledModeName(m.mode), m.label);
    }
    client.printf("</div><div class=fields><label>Farbe<input type=color data-k=c value='#%06x'></label>"
                  "<label>Anzahl LEDs<input type=number data-k=n min=1 max=%u value=%u inputmode=numeric></label></div>"
                  "<label class=vol>Helligkeit <output>%u %%</output></label>"
                  "<input type=range data-k=b min=0 max=100 value=%u>"
                  "<div class=chaseonly><label class=vol>Schweif <output data-o=w>%u LEDs</output></label>"
                  "<input type=range data-k=w min=0 max=1000 value=%ld>"
                  "<label class=vol>Geschwindigkeit <output data-o=v>%u LEDs/s</output></label>"
                  "<input type=range data-k=v min=0 max=1000 value=%ld>"
                  "<label class=vol>Richtung</label><div class=dirs>"
                  "<button type=button class='btn%s' data-d=line>Einseitig</button>"
                  "<button type=button class='btn%s' data-d=out>Innen → außen</button>"
                  "<button type=button class='btn%s' data-d=in>Außen → innen</button></div></div>"
                  "<div class=sparkleonly><label class=vol>Dichte <output data-o=r>%u Funken/s</output></label>"
                  "<input type=range data-k=r min=0 max=1000 value=%ld>"
                  "<label class=vol>Ausklingen <output data-o=f>%u,%u s</output></label>"
                  "<input type=range data-k=f min=0 max=1000 value=%ld></div>"
                  "<div class=types><button type=button class='btn%s' data-t=ws2812>WS2812, 5 V</button>"
                  "<button type=button class='btn%s' data-t=ws2815>WS2815, 12 V</button></div>"
                  "<p class=hint>Strom bei Weiß und dieser Helligkeit: <span data-k=current>%s</span><br>"
                  "Bildrate: <span data-k=fps>%s</span></p></div>",
                  c.color, LED_MAX_COUNT, c.count, c.brightness, c.brightness, c.tail,
                  lroundf(1000.0f * sqrtf(static_cast<float>(c.tail) / LED_MAX_TAIL)),
                  c.speed, lroundf(1000.0f * logf(c.speed) / logf(LED_MAX_SPEED)),
                  c.dir == LedChaseDir::Line ? " on" : "", c.dir == LedChaseDir::Out ? " on" : "",
                  c.dir == LedChaseDir::In ? " on" : "",
                  c.sparkleRate, lroundf(1000.0f * logf(c.sparkleRate) / logf(LED_MAX_SPARKLE_RATE)),
                  c.sparkleFadeMs / 1000, c.sparkleFadeMs % 1000 / 100,
                  lroundf(1000.0f * logf(c.sparkleFadeMs / 100.0f) / logf(300.0f)),
                  c.type == LedType::WS2812 ? " on" : "", c.type == LedType::WS2815 ? " on" : "", current, fpsText);
  }
  client.print("<p class=hint>J7 und J9 führen nur Daten (Pin 1) und GND (Pin 2). Den Strip über ein eigenes Netzteil "
               "versorgen (5 V bei WS2812, 12 V bei WS2815) und dessen GND mit Pin 2 verbinden. "
               "Beim WS2815 den Backup-Eingang BI am Strip-Anfang auf GND legen.</p>");

  AudioClip clip = audioPlaying();
  float hz = audioFrequency();
  // frequency slider is logarithmic: position 0..1000 maps to 20 Hz..20 kHz
  int freqPos = lroundf(1000.0f * logf(hz / 20.0f) / logf(1000.0f));
  client.printf("<h2>Audio</h2><p class=hint>Stream: <span id=stream>%s</span></p>"
                "<label class=vol for=vol>Lautstärke <output id=volv>%u %%</output></label>"
                "<input id=vol type=range min=0 max=%u step=1 value=%u>"
                "<label class=vol for=freq>Frequenz Testton <output id=freqv>%.0f Hz</output></label>"
                "<div class=fine><button type=button class=btn id=fdn aria-label='Frequenz verringern'>−</button>"
                "<input id=freq type=range min=0 max=1000 value=%d data-hz=%.1f>"
                "<button type=button class=btn id=fup aria-label='Frequenz erhöhen'>+</button></div>"
                "<div id=audio class=sounds>",
                t.stream, audioVolume(), audioVolumeLimit(), audioVolume(), hz, freqPos, hz);
  static const struct { AudioClip clip; const char *label; } sounds[] = {
      {AudioClip::Tone, "Testton"}, {AudioClip::Gong, "Gong"}, {AudioClip::Sweep, "Sweep"}};
  for (const auto &snd : sounds) {
    client.printf("<form method=post action='/audio/play?c=%s'><button class='btn%s' data-c=%s>%s</button></form>",
                  audioClipName(snd.clip), clip == snd.clip ? " on" : "", audioClipName(snd.clip), snd.label);
  }
  client.print("<form id=astop method=post action=/audio/stop><button class=btn>Stopp</button></form></div>"
               "<p class=hint>Testton: Dauerton mit der eingestellten Frequenz, läuft bis Stopp. "
               "Regler und − / + ändern die Frequenz auch während er läuft, − / + in Schritten von 1 %. "
               "Sweep: 100 Hz bis 8 kHz in 4 Sekunden.</p>"
               "<p class=hint>An J1 nur einen passiven Lautsprecher mit 4–8 Ω anschließen. "
               "Keine Kopfhörer und keinen Aux-Eingang: Auch an der Hülse liegt das Audiosignal, nicht Masse.</p>");
  client.printf("<label class=vol for=limit>Obergrenze Lautstärke <output id=limitv>%u %%</output></label>"
                "<input id=limit type=range min=1 max=100 step=1 value=%u>"
                "<p class=hint>Gilt für Testklänge und Stream. Darüber übersteuert der MAX98357A den Eingang der Aktivbox.</p>"
                "<p class=hint>Live-Stream: RTP an UDP-Port %u, unkomprimiert (L16), 48 kHz, Stereo.</p>",
                audioVolumeLimit(), audioVolumeLimit(), rtpPort());

  client.print("<h2>WLAN</h2><form method=post action=/wifi>"
               "<label for=ssid>Netzwerkname (SSID)</label>"
               "<input id=ssid name=ssid maxlength=32 required autocomplete=off autocapitalize=none spellcheck=false value='");
  printHtmlEscaped(client, wifiSsid);
  client.printf("'><label for=pass>Passwort</label>"
                "<input id=pass name=pass type=password minlength=8 maxlength=63 autocomplete=new-password%s>"
                "<p class=hint>Nur 2,4-GHz-Netze. %s</p>"
                "<div class=stack><button class='btn primary'>Speichern und verbinden</button></div></form>",
                wifiSsid[0] ? " placeholder='Unverändert'" : "",
                wifiSsid[0] ? "Leer lassen, um das gespeicherte Passwort zu behalten." : "Leer lassen bei einem offenen Netz.");
  if (wifiSsid[0]) {
    client.print("<div class=stack><form method=post action=/wifi/forget onsubmit=\"return confirm('WLAN-Zugangsdaten löschen?')\">"
                 "<button class='btn danger'>Netzwerk vergessen</button></form></div>");
  }

  client.printf("<h2>Gerät</h2><dl class=dev><dt>Firmware<dd>%s<dt>Laufzeit<dd id=up>%s<dt>Freier RAM<dd id=heap>%u KB"
                "<dt>PSRAM<dd>%u KB<dt>MAC Ethernet<dd>%02X:%02X:%02X:%02X:%02X:%02X<dt>MAC WLAN<dd>%s</dl></main>",
                FW_BUILD, t.uptime, ESP.getFreeHeap() / 1024, ESP.getPsramSize() / 1024,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], WiFi.macAddress().c_str());
  client.print(PAGE_SCRIPT);
}

// Returns true for GET /api/events: the caller hands the open connection to the SSE module.
static bool handleClient(Client &client) {
  activityUntil = millis() + 150;
  uint32_t deadline = millis() + 2000;

  char line[128];
  if (!readLine(client, line, sizeof(line), deadline)) {
    client.stop();
    return false;
  }
  bool isPost = strncmp(line, "POST ", 5) == 0;

  // request line: "<METHOD> <path> HTTP/1.1"
  char path[64] = "";
  if (const char *start = strchr(line, ' ')) {
    start++;
    size_t len = strcspn(start, " ");
    if (len >= sizeof(path)) len = sizeof(path) - 1;
    memcpy(path, start, len);
    path[len] = '\0';
  }

  size_t contentLength = 0;
  bool expectContinue = false;
  bool wantsJson = false;
  while (readLine(client, line, sizeof(line), deadline) && line[0] != '\0') {
    if (strncasecmp(line, "Content-Length:", 15) == 0) contentLength = strtoul(line + 15, nullptr, 10);
    if (strncasecmp(line, "Expect: 100-continue", 20) == 0) expectContinue = true;
    if (strncasecmp(line, "Accept:", 7) == 0 && strstr(line, "application/json")) wantsJson = true;
  }
  if (isPost && expectContinue) client.print("HTTP/1.1 100 Continue\r\n\r\n");
  if (!isPost && strcmp(path, "/api/events") == 0) return true;

  bool reconnectWifi = false;
  if (isPost && strcmp(path, "/update") == 0) {
    handleOta(client, contentLength);
  } else if (isPost && strncmp(path, "/wifi", 5) == 0) {
    reconnectWifi = handleWifiConfig(client, path, contentLength, deadline);
  } else if (handleRelay(path) || handleAudio(path) || handlePir(path) || handleLed(path)) {
    // fetch() gets the new state; plain forms go back to the page so a reload doesn't repeat the command
    if (wantsJson) sendJsonStatus(client);
    else redirectHome(client);
  } else if (strcmp(path, "/api/status") == 0) {
    sendJsonStatus(client);
  } else if (strcmp(path, "/") == 0) {
    sendStatus(client);
  } else {
    respond(client, "404 Not Found", "not found");
  }
  delay(1);
  client.stop();

  // after the response is out: the request may have come in over Wi-Fi itself
  if (reconnectWifi) wifiConnect();
  return false;
}

void setup() {
  for (uint8_t pin : PIN_RELAY) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  Serial.begin(115200);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_Y, OUTPUT);
  xTaskCreate(ledTask, "led", 2048, nullptr, 1, nullptr);

  if (bootGuardMagic != BOOT_GUARD_MAGIC || esp_reset_reason() == ESP_RST_POWERON) {
    bootGuardMagic = BOOT_GUARD_MAGIC;
    bootFailures = 0;
  }
  safeMode = ++bootFailures >= 3;
  if (safeMode) log("[boot] %u failed boots in a row: safe mode", bootFailures);

  pirBegin(PIN_PIR);
  loadLedConfig();
  loadPirConfig();  // after the strips: converting old actions changes strip modes
  ledBegin(PIN_LED_DATA);
  actionsBegin();

  if (!safeMode) audioBegin(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DIN, PIN_AMP_EN, true);
  loadAudioConfig();

  // Wi-Fi first: it brings up esp_netif and the default event loop, which Ethernet attaches to
  loadWifiConfig();
  wifiConnect();

  esp_read_mac(mac, ESP_MAC_ETH);
  EthPins ethPins = {PIN_ETH_SCLK, PIN_ETH_MOSI, PIN_ETH_MISO, PIN_ETH_CS, PIN_ETH_INT, PIN_ETH_RST};
  if (safeMode) {
    log("[eth] skipped (safe mode)");
  } else if (ethBegin(ethPins, mac, HOSTNAME)) {
    log("[eth] W5500 ok, MAC %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    log("[eth] W5500 not answering");
  }

  httpServer.begin();
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  if (!safeMode) rtpBegin(RTP_DEFAULT_PORT);
}

void loop() {
  if (bootFailures && millis() > SAFE_MODE_AFTER_MS) bootFailures = 0;  // this boot survived
  relayTestLoop();
  saveAudioConfigWhenIdle();
  saveLedConfigWhenIdle();
  saveActionsWhenIdle();

  static EthState lastEth = EthState::NoLink;
  EthState eth = ethState();
  if (eth != lastEth) {
    lastEth = eth;
    IPAddress ip = ethLocalIP();
    log("[eth] state %u, IP %u.%u.%u.%u", static_cast<uint8_t>(eth), ip[0], ip[1], ip[2], ip[3]);
  }

  WiFiClient client = httpServer.available();
  if (client && handleClient(client)) {
    sseAdd(client);
    forceEventPush = true;  // new subscriber gets the full state right away
  }
  pushEvents();

  delay(5);
}
