#include "pir.h"

#include <Arduino.h>
#include <soc/gpio_reg.h>

constexpr uint32_t DEBOUNCE_MS = 30;  // contacts bounce, long cables pick up spikes

struct PirInput {
  uint8_t pin;
  volatile bool activeHigh = false;
  volatile uint32_t count = 0;
  volatile uint32_t lastMs = 0;
  volatile uint32_t lastEdgeMs = 0;
};

static PirInput inputs[PIR_COUNT];

// register read instead of digitalRead(): the ISR must not touch flash (OTA/NVS writes disable the cache)
static inline bool IRAM_ATTR readPin(uint8_t pin) { return (REG_READ(GPIO_IN_REG) >> pin) & 1; }

static void IRAM_ATTR onEdge(void *arg) {
  PirInput &in = *static_cast<PirInput *>(arg);
  uint32_t now = millis();
  bool edgeTooSoon = now - in.lastEdgeMs < DEBOUNCE_MS;
  in.lastEdgeMs = now;
  if (edgeTooSoon) return;
  if (readPin(in.pin) == in.activeHigh) {
    in.count++;
    in.lastMs = now ? now : 1;  // 0 means "never"
  }
}

void pirBegin(const uint8_t pins[PIR_COUNT]) {
  for (uint8_t i = 0; i < PIR_COUNT; i++) {
    assert(pins[i] < 32);  // readPin() only covers GPIO_IN_REG
    inputs[i].pin = pins[i];
    pinMode(pins[i], INPUT);  // pull-up is on the board
    attachInterruptArg(digitalPinToInterrupt(pins[i]), onEdge, &inputs[i], CHANGE);
  }
}

void pirSetActiveHigh(uint8_t idx, bool activeHigh) {
  if (idx < PIR_COUNT) inputs[idx].activeHigh = activeHigh;
}

bool pirActiveHigh(uint8_t idx) { return idx < PIR_COUNT && inputs[idx].activeHigh; }

bool pirLevel(uint8_t idx) { return idx < PIR_COUNT && readPin(inputs[idx].pin); }

bool pirActive(uint8_t idx) { return idx < PIR_COUNT && pirLevel(idx) == inputs[idx].activeHigh; }

uint32_t pirCount(uint8_t idx) { return idx < PIR_COUNT ? inputs[idx].count : 0; }

uint32_t pirLastMs(uint8_t idx) { return idx < PIR_COUNT ? inputs[idx].lastMs : 0; }

void pirResetCounts() {
  for (PirInput &in : inputs) {
    in.count = 0;
    in.lastMs = 0;
  }
}
