/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Blink timing and the blink-mode selection machine.
 *
 * Every eye closure is classified purely by how long the eye stayed shut.
 * There are three meaningful outcomes and one deliberate hole:
 *
 *     < ignore_ms                 involuntary blink or tracking noise
 *     ignore_ms .. short_max_ms   SHORT  -> advance the highlight / morse dot
 *     short_max_ms .. long_ms     AMBIGUOUS -> discarded on purpose
 *     >= long_ms                  LONG   -> select / morse dash
 *
 * The hole between short_max_ms and long_ms is not an oversight. Without it a
 * cycling blink held slightly too long is read as a selection, and the machine
 * speaks a sentence the patient never chose. Discarding the middle band costs
 * one extra blink; guessing costs a wrong sentence. See oc_blink_bands_for().
 */
#ifndef OPENNCOMM_BLINK_H
#define OPENNCOMM_BLINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OC_SPEED_FAST = 0,
    OC_SPEED_NORMAL,
    OC_SPEED_RELAXED,
    OC_SPEED_COUNT
} oc_speed;

typedef struct {
    int ignore_ms;
    int short_max_ms;
    int long_ms;
} oc_bands;

/* How long a "long blink" should be varies enormously between patients, so the
 * bands are a preset rather than a constant.
 *
 * INVARIANT: every preset keeps a wide ambiguous band between short_max_ms and
 * long_ms, scaled with the rest so it can never be squeezed out. Keep that
 * property if you add a preset; test_blink asserts it.
 *
 * The presets are not a single ratio. long_ms is exactly 1.5x short_max_ms
 * for fast (280/420) and relaxed (600/900) but NOT for normal, the default,
 * which ships 380/560 -- a ratio of 1.474 and a 180ms band. The 560 stays
 * rather than being "corrected" to 570: it is the timing real patients were
 * tuned against, the 10ms is imperceptible, and quietly changing blink timing
 * to satisfy a tidier ratio is exactly the kind of tidying this file exists to
 * prevent. The test asserts the band, not the ratio. */
oc_bands oc_blink_bands_for(oc_speed speed);

oc_speed oc_speed_from_name(const char *name); /* unknown -> OC_SPEED_NORMAL */
const char *oc_speed_name(oc_speed speed);

/* ---- raw closure tracker ------------------------------------------------- */

typedef enum {
    OC_GESTURE_NONE = 0, /* nothing finished this frame */
    OC_GESTURE_SHORT,    /* a deliberate short blink completed */
    OC_GESTURE_LONG,     /* held past long_ms; fires while the eye is STILL SHUT */
    OC_GESTURE_DISCARD   /* completed, but too short or inside the dead band */
} oc_gesture;

typedef struct {
    oc_bands bands;
    bool eye_closed;
    int64_t close_start_ms;
    bool long_fired; /* a long gesture already fired for this closure */
} oc_blink_tracker;

void oc_blink_tracker_init(oc_blink_tracker *t, oc_bands bands);

/* Forget any closure in progress. Called when input is handed back to the
 * patient, so releasing a lock mid-blink is not read as a fresh gesture. */
void oc_blink_tracker_reset(oc_blink_tracker *t);

typedef struct {
    oc_gesture gesture;
    /* 0..1 once the closure passes short_max_ms, else -1 for "draw no bar".
     * The bar appearing is itself the signal that continuing to hold selects. */
    double hold_progress;
    int64_t held_ms;
} oc_blink_event;

/* Feed one frame. `blink` is 1 while the eye is shut, 0 while open.
 *
 * OC_GESTURE_LONG is emitted the moment the threshold is crossed, while the eye
 * is still closed -- not on reopening. The patient gets the spoken answer as
 * confirmation rather than having to hold and hope. */
oc_blink_event oc_blink_tracker_update(oc_blink_tracker *t, int blink, int64_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
