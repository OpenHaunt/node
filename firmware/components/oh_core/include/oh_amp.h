/*
 * When the speaker amplifier is powered: on just ahead of a sound and off a while
 * after one, or on for good.
 *
 * "For good" is a property of the board, not a saving to be had. A MAX98357A in
 * shutdown leaves its outputs high-impedance, and V0.1 wires them to a 3.5 mm jack
 * that usually feeds somebody else's amplifier: an input left floating picks up mains
 * and hums the moment the node lets go of it. So V0.1 keeps it on and feeds it
 * silence, which drives the line to zero; V0.2, whose amplifier drives a speaker
 * terminal and whose line out is the PCM5102A, switches it off to save the power bank.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool on;
    /* When the last sound ended, or 0 while one is playing or due. */
    int64_t quiet_since_us;
    int64_t idle_us;
    bool always_on;
} oh_amp_t;

void oh_amp_init(oh_amp_t *a, int64_t idle_us, bool always_on);

/* One output block: `wanted` is whether something is sounding or about to. Returns
 * true when `on` changed, which is when the enable pin has to be set again. */
bool oh_amp_step(oh_amp_t *a, bool wanted, int64_t now_us);

#ifdef __cplusplus
}
#endif
