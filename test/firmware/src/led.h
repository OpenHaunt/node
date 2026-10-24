// WS2812 / WS2815 strips on J7 / J9 (data + GND only, the strip needs its own 5 V / 12 V supply).
// Both types speak the same protocol; the type only changes the supply voltage and current estimate.
// Signals come from the RMT peripheral, so Wi-Fi/audio interrupts can't disturb the bit timing;
// a background task renders the effects at 50 fps.
#pragma once

#include <stdint.h>

constexpr uint8_t LED_STRIPS = 2;
constexpr uint16_t LED_MAX_COUNT = 1000;
constexpr uint8_t LED_MAX_TAIL = 255;     // chase tail in LEDs (stored as one byte)
constexpr uint16_t LED_MAX_SPEED = 500;   // chase speed in LEDs per second
constexpr uint16_t LED_MAX_SPARKLE_RATE = 500;  // sparkles per second
constexpr uint16_t LED_MIN_SPARKLE_FADE = 100;  // ms
constexpr uint16_t LED_MAX_SPARKLE_FADE = 30000;

enum class LedMode : uint8_t { Off, Color, Rainbow, Chase, Test, Sparkle };  // stored: append only
enum class LedType : uint8_t { WS2812, WS2815 };
// Chase direction: Line runs start → end; Out runs from the middle to both ends, In from both ends
// to the middle (both mirrored around the center of the strip)
enum class LedChaseDir : uint8_t { Line, Out, In };

struct LedConfig {
  LedMode mode = LedMode::Off;
  uint32_t color = 0xFF8800;  // 0xRRGGBB
  uint8_t brightness = 20;    // 0..100, perceptual (squared) scale
  uint16_t count = 60;
  LedType type = LedType::WS2812;
  uint8_t tail = 6;     // chase: LEDs behind the head, fading out
  uint16_t speed = 50;  // chase: LEDs per second
  // New fields only at the end; bump LED_CONFIG_VERSION in main.cpp and reset the new field for older
  // versions in loadLedConfig() (the stored struct's old padding may overlap it).
  LedChaseDir dir = LedChaseDir::Line;
  uint16_t sparkleRate = 60;     // sparkle: new sparks per second (v4 stored ms between sparks here)
  uint16_t sparkleFadeMs = 900;  // sparkle: time constant of the fade-out
  // v6 had motion control here (now in actions.h); a new field may not reuse bytes 22..27 unless
  // loadLedConfig() resets it for version < 7
};

// field by field: the struct has padding bytes, so memcmp would see random differences
inline bool operator==(const LedConfig &a, const LedConfig &b) {
  return a.mode == b.mode && a.color == b.color && a.brightness == b.brightness && a.count == b.count &&
         a.type == b.type && a.tail == b.tail && a.speed == b.speed && a.dir == b.dir &&
         a.sparkleRate == b.sparkleRate && a.sparkleFadeMs == b.sparkleFadeMs;
}
inline bool operator!=(const LedConfig &a, const LedConfig &b) { return !(a == b); }

void ledBegin(const uint8_t pins[LED_STRIPS]);

LedConfig ledConfig(uint8_t strip);
void ledSetConfig(uint8_t strip, const LedConfig &config);

const char *ledModeName(LedMode mode);      // "off", "color", "rainbow", "chase", "test", "sparkle"
const char *ledChaseDirName(LedChaseDir dir);  // "line", "out", "in"
bool ledModeFromName(const char *name, LedMode &mode);

// estimated strip current at full white for the current brightness, in mA
// (WS2812: 60 mA per LED at 5 V, WS2815: ~25 mA per LED at 12 V, both at 100 %)
uint32_t ledCurrentMa(uint8_t strip);
uint8_t ledSupplyVolts(uint8_t strip);

// frames per second actually sent over the last second (0 for static modes)
float ledFps(uint8_t strip);
// largest gap between two frame starts in the last 5 s, in ms (20 = on time)
uint32_t ledMaxFrameGapMs();

// Runs fn between two LED frames, while no strip is being sent, and waits until it is done (at most one
// frame, 20 ms). For flash (NVS) writes: they stall code fetches on both cores, which starves the RMT
// refill interrupt mid-frame and leaves the strip with a broken or late frame. Before ledBegin() fn
// simply runs right away.
void ledRunBetweenFrames(void (*fn)(void *), void *arg);

// Stops sending frames (the strips keep showing the last one) or resumes. Pausing takes effect between
// two frames, so no frame is cut off. For OTA updates, whose long flash writes would garble frames.
void ledSetPaused(bool paused);

// Trigger overlay (see actions.h): while a PIR action for the strip is active, the strip shows the action's
// effect instead of its own mode, cross-fading with the action's fade-in/fade-out times. Its own mode stays untouched. Returns
// false when no trigger is active (or it asks for the mode the strip is in anyway), else the effect shown.
bool ledTriggered(uint8_t strip, LedMode &effect);
