/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "oc_test.h"
#include "openncomm/head.h"

/* Settle the smoother on a position by feeding it repeatedly with eyes open. */
static void look_at(oc_head_selector *h, double x, double y, int64_t *t)
{
    for (int i = 0; i < 40; i++) { *t += 33; oc_head_update(h, x, y, 0, *t); }
}

int main(void)
{
    group("quadrant tracking, uncalibrated");
    {
        oc_head_selector h;
        oc_head_init(&h, true);           /* mirrored, the default */
        oc_head_set_options(&h, true);
        int64_t t = 10000;

        look_at(&h, 0.2, 0.2, &t);
        check_int(h.quadrant, 1, "nose left of a mirrored frame means looking right");
        look_at(&h, 0.8, 0.2, &t);
        check_int(h.quadrant, 0, "and the other side maps across too");
        look_at(&h, 0.8, 0.8, &t);
        check_int(h.quadrant, 2, "down-left on a mirrored feed");
        look_at(&h, 0.2, 0.8, &t);
        check_int(h.quadrant, 3, "down-right on a mirrored feed");
    }

    group("dead zone");
    {
        oc_head_selector h;
        oc_head_init(&h, false);
        oc_head_set_options(&h, true);
        int64_t t = 10000;

        look_at(&h, 0.2, 0.2, &t);
        check_int(h.quadrant, 0, "settled in the top-left");
        /* Drift to almost exactly centre: inside the dead zone on both axes. */
        look_at(&h, 0.5, 0.5, &t);
        check_int(h.quadrant, 0, "resting near the centre does not flip the highlight");
        look_at(&h, 0.9, 0.9, &t);
        check_int(h.quadrant, 3, "a decisive move is still believed");
    }

    group("selection");
    {
        oc_head_selector h;
        oc_head_init(&h, false);
        oc_head_set_options(&h, true);
        int64_t t = 10000;
        look_at(&h, 0.8, 0.2, &t);

        t += 33;
        oc_sel_event ev = oc_head_update(&h, 0.8, 0.2, 1, t);
        check_int(ev.kind, OC_SEL_COMMITTED, "any blink selects the pointed quadrant");
        check_int(ev.index, 1, "and posts the highlighted option");

        /* Still closed on the next frame: one closure is one selection. */
        t += 33;
        ev = oc_head_update(&h, 0.8, 0.2, 1, t);
        check_int(ev.kind, OC_SEL_NOTHING, "holding the blink does not select repeatedly");

        /* Locked while the answer is spoken. */
        t += 33; oc_head_update(&h, 0.8, 0.2, 0, t);
        t += 33;
        ev = oc_head_update(&h, 0.8, 0.2, 1, t);
        check_int(ev.kind, OC_SEL_NOTHING, "blinks during playback are ignored");

        oc_head_unlock(&h);
        t += 200; oc_head_update(&h, 0.8, 0.2, 0, t);
        t += 200;
        ev = oc_head_update(&h, 0.8, 0.2, 1, t);
        check_int(ev.kind, OC_SEL_NOTHING,
                  "a blink just after the answer is still inside the cooldown");

        t += OC_HEAD_COMMIT_COOLDOWN_MS;
        oc_head_update(&h, 0.8, 0.2, 0, t);
        t += 33;
        ev = oc_head_update(&h, 0.8, 0.2, 1, t);
        check_int(ev.kind, OC_SEL_COMMITTED, "and selects again once the cooldown passes");
    }

    group("gating before options load");
    {
        oc_head_selector h;
        oc_head_init(&h, false);
        oc_head_set_options(&h, false);
        int64_t t = 10000;
        look_at(&h, 0.8, 0.2, &t);

        t += 33;
        oc_sel_event ev = oc_head_update(&h, 0.8, 0.2, 1, t);
        check_int(ev.kind, OC_SEL_NEEDS_QUESTION, "a blink with nothing to answer explains itself");
        check(!h.input_locked, "and does not lock input");
    }

    group("calibration overrides the mirror flag");
    {
        /* A mirrored calibration: the right-hand dots sit at smaller raw x. */
        oc_calib mir = oc_calib_compute(
            (oc_point){ 0.7, 0.3 }, (oc_point){ 0.3, 0.3 },
            (oc_point){ 0.7, 0.7 }, (oc_point){ 0.3, 0.7 });
        check(mir.valid, "the mirrored calibration is usable");

        oc_head_selector a, b;
        oc_head_init(&a, true);  oc_head_set_calibration(&a, mir); oc_head_set_options(&a, true);
        oc_head_init(&b, false); oc_head_set_calibration(&b, mir); oc_head_set_options(&b, true);
        int64_t t = 10000;
        look_at(&a, 0.65, 0.35, &t);
        t = 10000;
        look_at(&b, 0.65, 0.35, &t);
        check_int(a.quadrant, b.quadrant,
                  "once calibrated, the mirror flag makes no difference");
    }

    return oc_report("test_head");
}
