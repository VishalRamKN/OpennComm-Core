/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/blink.h"

#include <string.h>

/* Three presets rather than one constant: patients differ by a factor of two
 * or more in how long a deliberate long blink takes. */
static const oc_bands k_bands[OC_SPEED_COUNT] = {
    [OC_SPEED_FAST]    = { .ignore_ms =  90, .short_max_ms = 280, .long_ms = 420 },
    [OC_SPEED_NORMAL]  = { .ignore_ms = 110, .short_max_ms = 380, .long_ms = 560 },
    [OC_SPEED_RELAXED] = { .ignore_ms = 120, .short_max_ms = 600, .long_ms = 900 },
};

static const char *k_names[OC_SPEED_COUNT] = { "fast", "normal", "relaxed" };

oc_bands oc_blink_bands_for(oc_speed speed)
{
    if (speed < 0 || speed >= OC_SPEED_COUNT) speed = OC_SPEED_NORMAL;
    return k_bands[speed];
}

oc_speed oc_speed_from_name(const char *name)
{
    if (!name) return OC_SPEED_NORMAL;
    for (int i = 0; i < OC_SPEED_COUNT; i++)
        if (strcmp(name, k_names[i]) == 0) return (oc_speed)i;
    return OC_SPEED_NORMAL;
}

const char *oc_speed_name(oc_speed speed)
{
    if (speed < 0 || speed >= OC_SPEED_COUNT) speed = OC_SPEED_NORMAL;
    return k_names[speed];
}

void oc_blink_tracker_init(oc_blink_tracker *t, oc_bands bands)
{
    memset(t, 0, sizeof(*t));
    t->bands = bands;
}

void oc_blink_tracker_reset(oc_blink_tracker *t)
{
    t->eye_closed = false;
    t->long_fired = false;
    t->close_start_ms = 0;
}

oc_blink_event oc_blink_tracker_update(oc_blink_tracker *t, int blink, int64_t now_ms)
{
    oc_blink_event ev = { .gesture = OC_GESTURE_NONE, .hold_progress = -1.0, .held_ms = 0 };

    if (blink) {
        if (!t->eye_closed) {
            t->eye_closed = true;
            t->close_start_ms = now_ms;
            t->long_fired = false;
        }
        int64_t held = now_ms - t->close_start_ms;
        ev.held_ms = held;

        /* Already committed for this closure. Guarding here is what stops a very
         * long hold selecting twice, and stops the release also reading as a
         * short blink once the eye finally opens. */
        if (t->long_fired) return ev;

        if (held >= t->bands.long_ms) {
            t->long_fired = true;
            ev.gesture = OC_GESTURE_LONG;
            ev.hold_progress = 1.0;
        } else if (held >= t->bands.short_max_ms) {
            ev.hold_progress = (double)(held - t->bands.short_max_ms) /
                               (double)(t->bands.long_ms - t->bands.short_max_ms);
        }
        return ev;
    }

    /* Eye is open. */
    if (!t->eye_closed) return ev;

    int64_t duration = now_ms - t->close_start_ms;
    ev.held_ms = duration;
    t->eye_closed = false;

    if (t->long_fired) {
        /* The long gesture already fired while the eye was shut. Reopening is
         * not a second event. */
        t->long_fired = false;
        ev.gesture = OC_GESTURE_NONE;
        return ev;
    }

    if (duration >= t->bands.ignore_ms && duration <= t->bands.short_max_ms)
        ev.gesture = OC_GESTURE_SHORT;
    else
        ev.gesture = OC_GESTURE_DISCARD; /* noise, or the deliberate dead band */

    return ev;
}
