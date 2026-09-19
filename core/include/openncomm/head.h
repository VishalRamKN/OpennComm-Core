/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Head-movement mode: point the nose at a quadrant, blink to choose it.
 *
 * Unlike blink mode this does NOT classify closure duration. Any detected
 * closure selects, because the patient has already indicated which option they
 * want by where their head is pointing -- the blink only has to mean "that
 * one". Asking them to also hold it for a measured length would be a second
 * demand for no extra information.
 *
 * What stops a double selection is a cooldown measured from the last COMMIT,
 * not from the last blink: after an answer is chosen the patient is listening
 * to it, and their next natural blink must not choose again.
 */
#ifndef OPENNCOMM_HEAD_H
#define OPENNCOMM_HEAD_H

#include <stdbool.h>
#include <stdint.h>

#include "openncomm/gaze.h"
#include "openncomm/select.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Milliseconds after a commit during which another blink cannot select. */
#define OC_HEAD_COMMIT_COOLDOWN_MS 1400

/* How far past centre the head must be, on BOTH axes, before a quadrant change
 * is believed. Without it the highlight flickers between neighbours whenever
 * the patient rests near a boundary. */
#define OC_HEAD_DEAD_ZONE 0.03

typedef struct {
    oc_calib calib;
    bool mirror_x;
    bool options_loaded;
    bool input_locked;

    int quadrant;          /* currently highlighted */
    int64_t last_commit_ms;
    bool eye_was_closed;   /* so one closure cannot select on every frame */

    double smooth_x, smooth_y;
    bool seeded;
} oc_head_selector;

void oc_head_init(oc_head_selector *h, bool mirror_x);
void oc_head_set_calibration(oc_head_selector *h, oc_calib calib);
void oc_head_set_options(oc_head_selector *h, bool loaded);
void oc_head_unlock(oc_head_selector *h);

/* Feed one frame: the raw nose position and whether the eye reads as closed. */
oc_sel_event oc_head_update(oc_head_selector *h, double nose_x, double nose_y,
                            int blink, int64_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
