// What the motion detector inputs trigger. Each input has a list of actions:
//   LED strip: while the input reports motion plus the action's hold time, the strip shows the action's
//              effect (also "off") instead of its own mode, then returns to its own mode; nothing changes
//              if the strip is in that mode anyway. Several actions on one strip: the most recent motion wins.
//   Gong:      plays the door chime on every new activation of the input
// A task polls the inputs every 10 ms, independent of the HTTP loop; the LED task asks it per frame.
#pragma once

#include <stdint.h>

#include "led.h"
#include "pir.h"

constexpr uint8_t ACTION_MAX = 4;  // per input
constexpr uint16_t ACTION_MAX_HOLD = 3600;  // s
constexpr uint16_t ACTION_MAX_FADE = 10000;  // ms

enum class ActionType : uint8_t { None, Led, Gong };

struct PirAction {
  ActionType type = ActionType::None;
  uint8_t strip = 0;    // Led: 0 = strip 1, 1 = strip 2
  uint16_t holdS = 60;  // Led: stays active this long after the last motion, 0 = only while the input is
  LedMode effect = LedMode::Color;  // Led: what the strip shows while the action is active
  uint16_t fadeInMs = 300;          // Led: cross-fade from the strip's own mode to the effect, 0 = cut
  uint16_t fadeOutMs = 2000;        // Led: cross-fade back after the hold time
  uint8_t brightness = 20;          // Led: brightness of the effect, 0..100 like LedConfig
};

typedef PirAction ActionTable[PIR_COUNT][ACTION_MAX];

void actionsBegin();  // starts the polling task; call after pirBegin()

// configuration (whole table, packed: used entries first)
void actionsGet(ActionTable out);
void actionsSet(const ActionTable in);
bool actionAdd(uint8_t pir, const PirAction &action);  // false if the list is full or has it already
void actionRemove(uint8_t pir, uint8_t idx);
void actionSetHold(uint8_t pir, uint8_t idx, uint16_t holdS);
void actionSetEffect(uint8_t pir, uint8_t idx, LedMode effect);
void actionSetFades(uint8_t pir, uint8_t idx, int32_t fadeInMs, int32_t fadeOutMs);  // < 0: keep
void actionSetBrightness(uint8_t pir, uint8_t idx, uint8_t brightness);
uint32_t actionsVersion();  // changes with every edit

const char *actionName(const PirAction &action);  // "led1", "led2", "gong"
bool actionFromName(const char *name, PirAction &action);

// state
bool actionsControlStrip(uint8_t strip);  // some input has an action for this strip
// true if an action for the strip is active (motion within its hold time); action = the one with the most
// recent motion (effect and fade times)
bool actionsStripAction(uint8_t strip, uint32_t now, PirAction &action);
bool actionActive(uint8_t pir, uint8_t idx, uint32_t now);
uint32_t actionsLastMotionMs(uint8_t pir);         // 0 = none since boot
