/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/head.h"

#include <string.h>

void oc_head_init(oc_head_selector *h, bool mirror_x)
{
    memset(h, 0, sizeof(*h));
    h->mirror_x = mirror_x;
    h->smooth_x = 0.5;
    h->smooth_y = 0.5;
}

void oc_head_set_calibration(oc_head_selector *h, oc_calib calib)
{
    h->calib = calib;
}

void oc_head_set_options(oc_head_selector *h, bool loaded)
{
    h->options_loaded = loaded;
}

void oc_head_unlock(oc_head_selector *h)
{
    h->input_locked = false;
    /* Discard any closure in progress, so releasing the lock mid-blink is not
     * read as a fresh selection. */
    h->eye_was_closed = true;
}

oc_sel_event oc_head_update(oc_head_selector *h, double nose_x, double nose_y,
                            int blink, int64_t now_ms)
{
    oc_sel_event out = { .kind = OC_SEL_NOTHING, .index = h->quadrant,
                         .hold_progress = -1.0 };

    /* Seed the smoother on the first frame rather than easing in from the
     * centre, which otherwise drags the highlight across the screen for the
     * first second of every session. */
    if (!h->seeded) {
        h->smooth_x = nose_x;
        h->smooth_y = nose_y;
        h->seeded = true;
    } else {
        h->smooth_x = oc_smooth_step(h->smooth_x, nose_x);
        h->smooth_y = oc_smooth_step(h->smooth_y, nose_y);
    }

    const oc_calib *cal = h->calib.valid ? &h->calib : NULL;
    int q = oc_gaze_quadrant(cal, h->mirror_x, h->smooth_x, h->smooth_y);

    /* Only believe a quadrant change when the head is clearly off centre on
     * both axes; otherwise keep the last one. */
    if (oc_gaze_confident_switch(cal, h->mirror_x, h->smooth_x, h->smooth_y,
                                 OC_HEAD_DEAD_ZONE)) {
        h->quadrant = q;
    }
    out.index = h->quadrant;

    if (h->input_locked) {
        h->eye_was_closed = (blink != 0);
        return out;
    }

    bool closing_edge = blink && !h->eye_was_closed;
    h->eye_was_closed = (blink != 0);
    if (!closing_edge) return out;

    if (now_ms - h->last_commit_ms <= OC_HEAD_COMMIT_COOLDOWN_MS) return out;

    if (!h->options_loaded) {
        out.kind = OC_SEL_NEEDS_QUESTION;
        return out;
    }

    h->last_commit_ms = now_ms;
    h->input_locked = true;
    out.kind = OC_SEL_COMMITTED;
    out.index = h->quadrant;
    return out;
}
