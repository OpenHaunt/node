// Motion detector inputs (J8 / J10, pin 3): 10K pull-up to 3.3 V on the board, 10K in series to the GPIO.
// Edges are caught by interrupt, so short pulses between two status polls are still counted.
#pragma once

#include <stdint.h>

constexpr uint8_t PIR_COUNT = 2;

void pirBegin(const uint8_t pins[PIR_COUNT]);

// activeHigh: which level means "motion". Low (default): NO contact or open collector to GND.
// High: NC contact to GND (also reports a cut cable) or HC-SR501-style 3.3 V output.
void pirSetActiveHigh(uint8_t idx, bool activeHigh);
bool pirActiveHigh(uint8_t idx);

bool pirLevel(uint8_t idx);       // raw pin level
bool pirActive(uint8_t idx);      // level == active level
uint32_t pirCount(uint8_t idx);   // activations since boot / last reset
uint32_t pirLastMs(uint8_t idx);  // millis() of the last activation, 0 = never
void pirResetCounts();
