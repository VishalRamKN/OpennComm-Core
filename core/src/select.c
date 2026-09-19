/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/select.h"

#include <string.h>

void oc_selector_init(oc_selector *s, oc_bands bands, int option_count)
{
    memset(s, 0, sizeof(*s));
    oc_blink_tracker_init(&s->tracker, bands);
    s->option_count = option_count > 0 ? option_count : 4;
}

void oc_selector_set_options(oc_selector *s, bool loaded)
{
    s->options_loaded = loaded;
    s->index = 0;
    s->last_scan_ms = 0;
    oc_blink_tracker_reset(&s->tracker);
}

void oc_selector_set_auto_scan(oc_selector *s, int seconds)
{
    if (seconds <= 0)      s->auto_scan_seconds = 0;
    else if (seconds < OC_AUTO_SCAN_MIN_S) s->auto_scan_seconds = OC_AUTO_SCAN_MIN_S;
    else if (seconds > OC_AUTO_SCAN_MAX_S) s->auto_scan_seconds = OC_AUTO_SCAN_MAX_S;
    else                   s->auto_scan_seconds = seconds;
    s->last_scan_ms = 0;
}

void oc_selector_unlock(oc_selector *s)
{
    s->input_locked = false;
    oc_blink_tracker_reset(&s->tracker);
}

oc_sel_event oc_selector_update(oc_selector *s, int blink, int64_t now_ms)
{
    oc_sel_event out = { .kind = OC_SEL_NOTHING, .index = s->index, .hold_progress = -1.0 };

    if (s->input_locked) {
        oc_blink_tracker_reset(&s->tracker);
        return out;
    }

    oc_blink_event ev = oc_blink_tracker_update(&s->tracker, blink, now_ms);
    out.hold_progress = ev.hold_progress;

    /* Auto-scan runs only when nothing else is happening: options are up, the
     * eye is open, and no gesture completed on this frame. Moving the
     * highlight during a closure would hand the patient an answer they did not
     * choose, which is the one outcome this whole program is arranged to
     * prevent. */
    if (s->auto_scan_seconds > 0 && s->options_loaded
        && !s->tracker.eye_closed && ev.gesture == OC_GESTURE_NONE) {
        if (s->last_scan_ms == 0) {
            s->last_scan_ms = now_ms;
        } else if (now_ms - s->last_scan_ms >= (int64_t)s->auto_scan_seconds * 1000) {
            s->last_scan_ms = now_ms;
            s->index = (s->index + 1) % s->option_count;
            out.kind = OC_SEL_ADVANCED;
            out.index = s->index;
            return out;
        }
    }

    switch (ev.gesture) {
    case OC_GESTURE_LONG:
        /* Fired while the eye is still shut. If there is nothing to answer, say
         * so -- a long blink before any question otherwise looks like the hold
         * simply failed. */
        if (!s->options_loaded) {
            out.kind = OC_SEL_NEEDS_QUESTION;
        } else {
            out.kind = OC_SEL_COMMITTED;
            out.index = s->index;
            s->input_locked = true;
        }
        break;

    case OC_GESTURE_SHORT:
        if (!s->options_loaded) {
            out.kind = OC_SEL_NEEDS_QUESTION;
        } else {
            s->index = (s->index + 1) % s->option_count;
            out.kind = OC_SEL_ADVANCED;
            out.index = s->index;
            /* Restart the interval. With a free-running timer a manual short
             * blink could be followed a fraction of a second later by an
             * automatic one -- two moves for one gesture. */
            s->last_scan_ms = now_ms;
        }
        break;

    case OC_GESTURE_DISCARD:
    case OC_GESTURE_NONE:
    default:
        break;
    }

    return out;
}
