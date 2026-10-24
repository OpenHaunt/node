#include "led.h"

#include <Arduino.h>
#include <driver/rmt.h>
#include <math.h>
#include <string.h>

#include "actions.h"  // motion control: asked every frame whether a PIR-controlled strip should be lit

constexpr uint32_t FRAME_MS = 20;            // 50 fps for animations
constexpr uint32_t STATIC_REFRESH_MS = 1000;  // static modes are resent now and then (hot-plugged strips)
constexpr rmt_channel_t CHANNELS[LED_STRIPS] = {RMT_CHANNEL_0, RMT_CHANNEL_1};

// WS2812B bit timing in ns; RMT runs at 40 MHz (APB 80 MHz / 2), 25 ns per tick
static uint32_t t0h, t0l, t1h, t1l;

static LedConfig configs[LED_STRIPS];
static volatile uint32_t configVersion = 0;
static uint8_t pixels[LED_STRIPS][LED_MAX_COUNT * 3];

// sparkle state per LED: current level (65535 = just lit, 16 bit so fades of many seconds still have
// a decay step per frame) and hue
static uint16_t sparkLevel[LED_STRIPS][LED_MAX_COUNT];
static uint16_t sparkHue[LED_STRIPS][LED_MAX_COUNT];
static float sparkCarry[LED_STRIPS];  // fractional sparks left over from previous frames

// RMT translator: one rmt item per data bit, MSB first. Runs in the RMT ISR, so it lives in IRAM.
static void IRAM_ATTR ws2812Translate(const void *src, rmt_item32_t *dest, size_t srcSize, size_t wanted,
                                      size_t *translated, size_t *items) {
  const rmt_item32_t bit0 = {{{t0h, 1, t0l, 0}}};
  const rmt_item32_t bit1 = {{{t1h, 1, t1l, 0}}};
  const uint8_t *in = static_cast<const uint8_t *>(src);
  size_t size = 0, num = 0;
  while (size < srcSize && num + 8 <= wanted) {
    for (int bit = 7; bit >= 0; bit--) dest[num++].val = (in[size] >> bit) & 1 ? bit1.val : bit0.val;
    size++;
  }
  *translated = size;
  *items = num;
}

// HSV with h 0..1535 (six 256-step sectors), full saturation and value
static uint32_t hue(uint16_t h) {
  h %= 1536;
  uint8_t x = h & 0xFF;
  switch (h >> 8) {
    case 0:  return 0xFF0000 | x << 8;          // red → yellow
    case 1:  return (255 - x) << 16 | 0xFF00;   // yellow → green
    case 2:  return 0x00FF00 | x;               // green → cyan
    case 3:  return (255 - x) << 8 | 0xFF;      // cyan → blue
    case 4:  return x << 16 | 0xFF;             // blue → magenta
    default: return 0xFF0000 | (255 - x);       // magenta → red
  }
}

// t = 0 → a, t = 255 → b
static uint32_t blend(uint32_t a, uint32_t b, uint8_t t) {
  uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    uint32_t x = (a >> shift) & 0xFF, y = (b >> shift) & 0xFF;
    out |= (x * (255 - t) + y * t) / 255 << shift;
  }
  return out;
}

static void setPixel(uint8_t *buf, uint16_t i, uint32_t rgb, uint16_t scale) {
  buf[i * 3] = ((rgb >> 8) & 0xFF) * scale >> 16;   // WS2812 order: G, R, B
  buf[i * 3 + 1] = ((rgb >> 16) & 0xFF) * scale >> 16;
  buf[i * 3 + 2] = (rgb & 0xFF) * scale >> 16;
}

// Renders one frame; returns true if the mode is animated
// gate: motion control dimming 0..1 (squared here for an even-looking fade)
static bool render(const LedConfig &c, uint8_t strip, uint8_t *buf, uint32_t now, float gate) {
  uint16_t scale = static_cast<uint32_t>(c.brightness) * c.brightness * 65535 / 10000 * gate * gate;  // squared
  bool animated = true;
  switch (c.mode) {
    case LedMode::Off:
      memset(buf, 0, c.count * 3);
      animated = false;
      break;
    case LedMode::Color:
      for (uint16_t i = 0; i < c.count; i++) setPixel(buf, i, c.color, scale);
      animated = false;
      break;
    case LedMode::Rainbow: {
      uint16_t offset = now / 4;  // one full cycle in ~6 s
      for (uint16_t i = 0; i < c.count; i++) setPixel(buf, i, hue(offset + i * 1536 / max<uint16_t>(c.count, 1)), scale);
      break;
    }
    case LedMode::Chase: {
      // The chase runs on a virtual strip; the symmetric directions use half the length and mirror
      // each virtual LED to both sides (Out: from the center outward, In: from both ends inward).
      uint16_t n = c.count;
      bool mirrored = c.dir != LedChaseDir::Line;
      uint16_t vn = mirrored ? (n + 1) / 2 : n;
      auto plot = [&](uint16_t j, uint16_t s) {
        if (!mirrored) return setPixel(buf, j, c.color, s);
        bool out = c.dir == LedChaseDir::Out;
        setPixel(buf, out ? (n - 1) / 2 - j : j, c.color, s);
        setPixel(buf, out ? n / 2 + j : n - 1 - j, c.color, s);
      };
      // Head position in 1/256 LED steps, so the light glides between two LEDs instead of jumping.
      // An LED d LEDs behind the exact head gets (1 - d / (tail + 1))^2 of the brightness; the LED
      // ahead fades in as the head approaches it.
      uint32_t pos = static_cast<uint64_t>(now) * c.speed * 256 / 1000 % (vn * 256u);
      uint16_t head = pos / 256;
      float frac = (pos % 256) / 256.0f;
      float len = c.tail + 1.0f;
      memset(buf, 0, n * 3);
      for (uint16_t k = 0; k <= c.tail + 1u && k < vn; k++) {
        float f = 1.0f - (k + frac) / len;
        if (f <= 0) break;
        plot((head + vn - k) % vn, scale * f * f);
      }
      if (vn > c.tail + 2u) plot((head + 1) % vn, scale * frac * frac);
      break;
    }
    case LedMode::Sparkle: {
      // Random LEDs flash up in a random rainbow hue and fade out exponentially. A fresh spark starts
      // with white mixed in and turns fully saturated while it dims; brightness follows level^2.
      uint16_t *level = sparkLevel[strip];
      uint16_t *h = sparkHue[strip];
      uint32_t decay = 65536.0f * expf(-static_cast<float>(FRAME_MS) / c.sparkleFadeMs);  // per frame, in 1/65536
      for (uint16_t i = 0; i < c.count; i++) level[i] = level[i] * decay >> 16;
      sparkCarry[strip] += c.sparkleRate * FRAME_MS / 1000.0f;
      while (sparkCarry[strip] >= 1.0f) {
        sparkCarry[strip] -= 1.0f;
        uint16_t i = esp_random() % c.count;
        level[i] = 65535;
        h[i] = esp_random() % 1536;
      }
      for (uint16_t i = 0; i < c.count; i++) {
        uint8_t l = level[i] >> 8;
        if (!l) {
          buf[i * 3] = buf[i * 3 + 1] = buf[i * 3 + 2] = 0;
          continue;
        }
        uint32_t rgb = hue(h[i]);
        if (l > 200) rgb = blend(rgb, 0xFFFFFF, (l - 200) * 150 / 55);  // white core, up to ~60 %
        setPixel(buf, i, rgb, static_cast<uint32_t>(scale) * l * l / 65025);
      }
      break;
    }
    case LedMode::Test: {
      // whole strip red, green, blue, white, 1 s each: shows color order and every channel
      static const uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
      uint32_t rgb = colors[(now / 1000) % 4];
      for (uint16_t i = 0; i < c.count; i++) setPixel(buf, i, rgb, scale);
      break;
    }
  }
  return animated;
}

static volatile float fps[LED_STRIPS];
static volatile uint32_t maxGapMs;
static volatile bool paused = false;

// trigger overlay: the PIR action's effect is rendered into a second buffer and cross-faded over the
// strip's own mode, with the fade times stored in the action
static uint8_t overlay[LED_STRIPS][LED_MAX_COUNT * 3];
static uint16_t fadeOutMs[LED_STRIPS];
static uint8_t triggerBrightness[LED_STRIPS];  // of the action last active: applies after it ran out
static float mix[LED_STRIPS];  // 0 = own mode .. 1 = trigger effect
static LedMode triggerEffect[LED_STRIPS];
static volatile bool triggered[LED_STRIPS];

bool ledTriggered(uint8_t strip, LedMode &effect) {
  if (strip >= LED_STRIPS || !triggered[strip]) return false;
  effect = triggerEffect[strip];
  return true;
}

// Updates the overlay mix of one strip; returns true while it is still cross-fading
static bool updateTrigger(uint8_t s, const LedConfig &c, uint32_t now) {
  PirAction a;
  // nothing to do if the strip already shows that effect at that brightness
  bool on = actionsStripAction(s, now, a) &&
            (a.effect != c.mode || (a.effect != LedMode::Off && a.brightness != c.brightness));
  if (on) {  // kept while fading back to the own mode
    triggerEffect[s] = a.effect;
    triggerBrightness[s] = a.brightness;
    fadeOutMs[s] = a.fadeOutMs;
  }
  triggered[s] = on;
  float target = on ? 1.0f : 0.0f;
  // 0 ms fade: the full step at once
  if (mix[s] < target) mix[s] = a.fadeInMs ? min(target, mix[s] + FRAME_MS / static_cast<float>(a.fadeInMs)) : target;
  else if (mix[s] > target) {
    mix[s] = fadeOutMs[s] ? max(target, mix[s] - FRAME_MS / static_cast<float>(fadeOutMs[s])) : target;
  }
  return mix[s] != target;
}

// job handed over by ledRunBetweenFrames(), run by the LED task right after a frame went out
static TaskHandle_t ledTaskHandle;
static SemaphoreHandle_t jobLock, jobDone;
static void (*volatile jobFn)(void *);
static void *volatile jobArg;

void ledSetPaused(bool pause) {
  if (pause) {
    ledRunBetweenFrames([](void *) { paused = true; }, nullptr);
  } else {
    paused = false;
  }
}

void ledRunBetweenFrames(void (*fn)(void *), void *arg) {
  if (!ledTaskHandle || xTaskGetCurrentTaskHandle() == ledTaskHandle) {
    fn(arg);
    return;
  }
  xSemaphoreTake(jobLock, portMAX_DELAY);
  jobArg = arg;
  jobFn = fn;
  xSemaphoreTake(jobDone, portMAX_DELAY);
  xSemaphoreGive(jobLock);
}

// Fixed 20 ms cadence (the transmission time counts into it instead of adding to it), both strips
// sent in parallel on their own RMT channels. A frame takes 30 us per LED, so up to ~650 LEDs per
// strip keep the full 50 fps; longer strips run as fast as their data allows.
static void ledTask(void *) {
  uint32_t seenVersion = UINT32_MAX;
  uint32_t lastStatic[LED_STRIPS] = {};
  uint16_t lastCount[LED_STRIPS] = {};
  uint32_t frames[LED_STRIPS] = {};
  float prevMix[LED_STRIPS] = {};
  uint32_t fpsSince = millis();
  TickType_t wake = xTaskGetTickCount();
  uint32_t lastFrame = millis(), gapWindowStart = lastFrame, gapMax = 0;
  for (;;) {
    uint32_t now = millis();
    gapMax = max(gapMax, now - lastFrame);
    lastFrame = now;
    if (now - gapWindowStart >= 5000) {
      maxGapMs = gapMax;
      gapMax = 0;
      gapWindowStart = now;
    }
    bool changed = seenVersion != configVersion;
    seenVersion = configVersion;
    bool sending[LED_STRIPS] = {};
    for (uint8_t s = 0; s < LED_STRIPS && !paused; s++) {
      LedConfig c = configs[s];
      // a shorter strip than before: blank the LEDs past the new end once
      uint16_t sendCount = max(c.count, lastCount[s]);
      if (sendCount > c.count) memset(pixels[s] + c.count * 3, 0, (sendCount - c.count) * 3);
      bool fading = updateTrigger(s, c, now);
      bool animated = false;
      if (mix[s] < 1) animated |= render(c, s, pixels[s], now, 1);
      if (mix[s] > 0) {
        LedConfig t = c;
        t.mode = triggerEffect[s];
        t.brightness = triggerBrightness[s];
        animated |= render(t, s, overlay[s], now, 1);
        int32_t m = mix[s] * 256;
        for (size_t i = 0; i < c.count * 3u; i++) {
          pixels[s][i] = m >= 256 ? overlay[s][i] : pixels[s][i] + ((overlay[s][i] - pixels[s][i]) * m >> 8);
        }
      }
      bool lastFadeFrame = prevMix[s] != mix[s];  // also sends the final frame of a cross-fade
      prevMix[s] = mix[s];
      if (!animated && !fading && !lastFadeFrame && !changed && now - lastStatic[s] < STATIC_REFRESH_MS) continue;
      lastStatic[s] = now;
      rmt_write_sample(CHANNELS[s], pixels[s], sendCount * 3, false);  // starts, ISR feeds the rest
      sending[s] = true;
      lastCount[s] = c.count;
      if (animated) frames[s]++;
    }
    for (uint8_t s = 0; s < LED_STRIPS; s++) {
      if (sending[s]) rmt_wait_tx_done(CHANNELS[s], portMAX_DELAY);  // buffers are re-rendered next frame
    }
    if (jobFn) {  // nothing is being sent right now: safe moment for a flash write
      void (*fn)(void *) = jobFn;
      jobFn = nullptr;
      fn(jobArg);
      xSemaphoreGive(jobDone);
    }
    if (now - fpsSince >= 1000) {
      for (uint8_t s = 0; s < LED_STRIPS; s++) {
        fps[s] = frames[s] * 1000.0f / (now - fpsSince);
        frames[s] = 0;
      }
      fpsSince = now;
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS));
  }
}

uint32_t ledMaxFrameGapMs() { return maxGapMs; }

float ledFps(uint8_t strip) { return strip < LED_STRIPS ? fps[strip] : 0; }

void ledBegin(const uint8_t pins[LED_STRIPS]) {
  for (uint8_t s = 0; s < LED_STRIPS; s++) {
    rmt_config_t cfg = {};
    cfg.rmt_mode = RMT_MODE_TX;
    cfg.channel = CHANNELS[s];
    cfg.gpio_num = static_cast<gpio_num_t>(pins[s]);
    cfg.clk_div = 2;
    cfg.mem_block_num = 1;
    cfg.tx_config.idle_output_en = true;
    cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    rmt_config(&cfg);
    rmt_driver_install(cfg.channel, 0, 0);
    rmt_translator_init(cfg.channel, ws2812Translate);
  }
  uint32_t clock;
  rmt_get_counter_clock(CHANNELS[0], &clock);
  auto ticks = [clock](uint32_t ns) { return static_cast<uint32_t>(static_cast<uint64_t>(clock) * ns / 1000000000ULL); };
  t0h = ticks(400);
  t0l = ticks(850);
  t1h = ticks(800);
  t1l = ticks(450);
  jobLock = xSemaphoreCreateMutex();
  jobDone = xSemaphoreCreateBinary();
  // below the audio (20) and RTP (19) tasks, above the HTTP loop; stack also covers NVS writes run here
  xTaskCreatePinnedToCore(ledTask, "leds", 6144, nullptr, 3, &ledTaskHandle, 1);
}

LedConfig ledConfig(uint8_t strip) { return strip < LED_STRIPS ? configs[strip] : LedConfig(); }

void ledSetConfig(uint8_t strip, const LedConfig &config) {
  if (strip >= LED_STRIPS) return;
  LedConfig c = config;
  c.count = constrain(c.count, 1, LED_MAX_COUNT);
  c.brightness = min<uint8_t>(c.brightness, 100);
  c.color &= 0xFFFFFF;
  c.tail = min(c.tail, LED_MAX_TAIL);
  c.speed = constrain(c.speed, 1, LED_MAX_SPEED);
  if (static_cast<uint8_t>(c.dir) > static_cast<uint8_t>(LedChaseDir::In)) c.dir = LedChaseDir::Line;
  if (static_cast<uint8_t>(c.mode) > static_cast<uint8_t>(LedMode::Sparkle)) c.mode = LedMode::Off;
  c.sparkleRate = constrain(c.sparkleRate, 1, LED_MAX_SPARKLE_RATE);
  c.sparkleFadeMs = constrain(c.sparkleFadeMs, LED_MIN_SPARKLE_FADE, LED_MAX_SPARKLE_FADE);
  configs[strip] = c;
  configVersion++;
}

const char *ledModeName(LedMode mode) {
  switch (mode) {
    case LedMode::Color:   return "color";
    case LedMode::Rainbow: return "rainbow";
    case LedMode::Chase:   return "chase";
    case LedMode::Test:    return "test";
    case LedMode::Sparkle: return "sparkle";
    default:               return "off";
  }
}

const char *ledChaseDirName(LedChaseDir dir) {
  switch (dir) {
    case LedChaseDir::Out: return "out";
    case LedChaseDir::In:  return "in";
    default:               return "line";
  }
}

bool ledModeFromName(const char *name, LedMode &mode) {
  static const LedMode modes[] = {LedMode::Off, LedMode::Color, LedMode::Rainbow, LedMode::Chase, LedMode::Test,
                                  LedMode::Sparkle};
  for (LedMode m : modes) {
    const char *n = ledModeName(m);
    size_t len = strlen(n);
    if (strncmp(name, n, len) == 0 && (name[len] == '\0' || name[len] == '&')) {
      mode = m;
      return true;
    }
  }
  return false;
}

uint32_t ledCurrentMa(uint8_t strip) {
  if (strip >= LED_STRIPS) return 0;
  const LedConfig &c = configs[strip];
  uint32_t perLed = c.type == LedType::WS2815 ? 25 : 60;
  return static_cast<uint32_t>(c.count) * perLed * c.brightness * c.brightness / 10000;
}

uint8_t ledSupplyVolts(uint8_t strip) {
  return strip < LED_STRIPS && configs[strip].type == LedType::WS2815 ? 12 : 5;
}
