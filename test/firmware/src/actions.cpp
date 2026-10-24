#include "actions.h"

#include <Arduino.h>
#include <string.h>

#include "audio.h"

constexpr uint32_t POLL_MS = 10;
// lastMotion is stamped by this task, the LED task compares with its own clock a few ms later: without
// some slack a hold time of 0 would only ever match within the same millisecond
constexpr uint32_t HOLD_SLACK_MS = 50;

static ActionTable table;
static portMUX_TYPE tableLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t version = 0;
static volatile uint32_t lastMotion[PIR_COUNT];

static void copyTable(ActionTable dst, const ActionTable src) {
  portENTER_CRITICAL(&tableLock);
  memcpy(dst, src, sizeof(ActionTable));
  portEXIT_CRITICAL(&tableLock);
}

void actionsGet(ActionTable out) { copyTable(out, table); }

// Drops unused and invalid entries and duplicates, keeps the order
static void normalize(ActionTable t) {
  for (uint8_t p = 0; p < PIR_COUNT; p++) {
    PirAction packed[ACTION_MAX];
    uint8_t n = 0;
    for (uint8_t i = 0; i < ACTION_MAX; i++) {
      PirAction a = t[p][i];
      bool valid = (a.type == ActionType::Led && a.strip < LED_STRIPS) || a.type == ActionType::Gong;
      for (uint8_t k = 0; k < n && valid; k++) {
        valid = !(packed[k].type == a.type && (a.type == ActionType::Gong || packed[k].strip == a.strip));
      }
      if (!valid) continue;
      a.holdS = min(a.holdS, ACTION_MAX_HOLD);
      if (static_cast<uint8_t>(a.effect) > static_cast<uint8_t>(LedMode::Sparkle)) a.effect = LedMode::Color;
      a.fadeInMs = min(a.fadeInMs, ACTION_MAX_FADE);
      a.fadeOutMs = min(a.fadeOutMs, ACTION_MAX_FADE);
      a.brightness = min<uint8_t>(a.brightness, 100);
      packed[n++] = a;
    }
    for (uint8_t i = 0; i < ACTION_MAX; i++) t[p][i] = i < n ? packed[i] : PirAction();
  }
}

void actionsSet(const ActionTable in) {
  ActionTable t;
  memcpy(t, in, sizeof(t));
  normalize(t);
  copyTable(table, t);
  version++;
}

bool actionAdd(uint8_t pir, const PirAction &action) {
  if (pir >= PIR_COUNT) return false;
  ActionTable t;
  actionsGet(t);
  uint8_t used = 0;
  while (used < ACTION_MAX && t[pir][used].type != ActionType::None) used++;
  if (used == ACTION_MAX) return false;
  t[pir][used] = action;
  actionsSet(t);  // normalize() drops it again if it is a duplicate
  ActionTable check;
  actionsGet(check);
  return check[pir][used].type != ActionType::None;
}

void actionRemove(uint8_t pir, uint8_t idx) {
  if (pir >= PIR_COUNT || idx >= ACTION_MAX) return;
  ActionTable t;
  actionsGet(t);
  t[pir][idx] = PirAction();
  actionsSet(t);
}

void actionSetHold(uint8_t pir, uint8_t idx, uint16_t holdS) {
  if (pir >= PIR_COUNT || idx >= ACTION_MAX) return;
  ActionTable t;
  actionsGet(t);
  t[pir][idx].holdS = holdS;
  actionsSet(t);
}

void actionSetEffect(uint8_t pir, uint8_t idx, LedMode effect) {
  if (pir >= PIR_COUNT || idx >= ACTION_MAX) return;
  ActionTable t;
  actionsGet(t);
  t[pir][idx].effect = effect;
  actionsSet(t);
}

void actionSetFades(uint8_t pir, uint8_t idx, int32_t fadeInMs, int32_t fadeOutMs) {
  if (pir >= PIR_COUNT || idx >= ACTION_MAX) return;
  ActionTable t;
  actionsGet(t);
  if (fadeInMs >= 0) t[pir][idx].fadeInMs = fadeInMs;
  if (fadeOutMs >= 0) t[pir][idx].fadeOutMs = fadeOutMs;
  actionsSet(t);
}

void actionSetBrightness(uint8_t pir, uint8_t idx, uint8_t brightness) {
  if (pir >= PIR_COUNT || idx >= ACTION_MAX) return;
  ActionTable t;
  actionsGet(t);
  t[pir][idx].brightness = brightness;
  actionsSet(t);
}

uint32_t actionsVersion() { return version; }

const char *actionName(const PirAction &action) {
  switch (action.type) {
    case ActionType::Led:  return action.strip == 0 ? "led1" : "led2";
    case ActionType::Gong: return "gong";
    default:               return "";
  }
}

bool actionFromName(const char *name, PirAction &action) {
  action = PirAction();
  if (strncmp(name, "led1", 4) == 0 || strncmp(name, "led2", 4) == 0) {
    action.type = ActionType::Led;
    action.strip = name[3] - '1';
    return true;
  }
  if (strncmp(name, "gong", 4) == 0) {
    action.type = ActionType::Gong;
    return true;
  }
  return false;
}

bool actionsControlStrip(uint8_t strip) {
  ActionTable t;
  actionsGet(t);
  for (uint8_t p = 0; p < PIR_COUNT; p++) {
    for (const PirAction &a : t[p]) {
      if (a.type == ActionType::Led && a.strip == strip) return true;
    }
  }
  return false;
}

// Active while the input is, then for the hold time after the last motion (hold 0: only while active)
static bool withinHold(const PirAction &a, uint8_t pir, uint32_t last, uint32_t now) {
  return pirActive(pir) || (last && now - last <= a.holdS * 1000UL + HOLD_SLACK_MS);
}

bool actionsStripAction(uint8_t strip, uint32_t now, PirAction &action) {
  ActionTable t;
  actionsGet(t);
  bool found = false;
  uint32_t newest = 0;
  for (uint8_t p = 0; p < PIR_COUNT; p++) {
    uint32_t last = lastMotion[p];
    for (const PirAction &a : t[p]) {
      if (a.type != ActionType::Led || a.strip != strip || !withinHold(a, p, last, now)) continue;
      if (!found || static_cast<int32_t>(last - newest) > 0) {  // the most recent motion wins
        action = a;
        newest = last;
        found = true;
      }
    }
  }
  return found;
}

bool actionActive(uint8_t pir, uint8_t idx, uint32_t now) {
  if (pir >= PIR_COUNT || idx >= ACTION_MAX) return false;
  ActionTable t;
  actionsGet(t);
  return t[pir][idx].type == ActionType::Led && withinHold(t[pir][idx], pir, lastMotion[pir], now);
}

uint32_t actionsLastMotionMs(uint8_t pir) { return pir < PIR_COUNT ? lastMotion[pir] : 0; }

static void actionsTask(void *) {
  uint32_t lastCount[PIR_COUNT];
  for (uint8_t p = 0; p < PIR_COUNT; p++) lastCount[p] = pirCount(p);
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    uint32_t now = millis();
    ActionTable t;
    actionsGet(t);
    for (uint8_t p = 0; p < PIR_COUNT; p++) {
      if (pirActive(p)) lastMotion[p] = now ? now : 1;
      // short pulse between two polls: the interrupt kept its time
      uint32_t edge = pirLastMs(p);
      if (edge && (!lastMotion[p] || static_cast<int32_t>(edge - lastMotion[p]) > 0)) lastMotion[p] = edge;

      uint32_t count = pirCount(p);
      if (count > lastCount[p]) {  // new activation(s) since the last poll
        for (const PirAction &a : t[p]) {
          if (a.type == ActionType::Gong) audioPlay(AudioClip::Gong, audioVolume());
        }
      }
      lastCount[p] = count;  // also follows a counter reset
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(POLL_MS));
  }
}

void actionsBegin() {
  // above the LED task (3), so a strip sees fresh motion state; far below audio and RTP
  xTaskCreatePinnedToCore(actionsTask, "actions", 3072, nullptr, 4, nullptr, 1);
}
