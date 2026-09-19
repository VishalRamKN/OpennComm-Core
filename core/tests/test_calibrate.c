/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "oc_test.h"
#include "openncomm/calibrate.h"

/* Advance at 30fps, holding the nose still, until the phase changes.
 *
 * Deliberately phase-driven rather than duration-driven. Running for "settle
 * plus a bit" overshoots into sampling and feeds it the pre-move position,
 * which silently contaminates exactly the behaviour these tests exist to pin. */
static oc_calib_progress until_phase_change(oc_calibrator *c, double x, double y,
                                            bool face, int64_t *t)
{
    const oc_calib_phase from = c->phase;
    oc_calib_progress p = { 0 };
    for (int frame = 0; frame < 1000; frame++) {
        *t += 33;
        p = oc_calibrator_update(c, x, y, face, *t);
        if (c->phase != from) break;
    }
    return p;
}

/* Run frames at 30fps for a fixed duration, for the cases that care about it. */
static oc_calib_progress run(oc_calibrator *c, double x, double y, bool face,
                             int64_t ms, int64_t *t)
{
    oc_calib_progress p = { 0 };
    for (int64_t e = 0; e < ms; e += 33) { *t += 33; p = oc_calibrator_update(c, x, y, face, *t); }
    return p;
}

/* Walk one whole point: travel to the dot during SETTLE, hold it through
 * SAMPLE, then wait out CONFIRM. */
static oc_calib_progress do_point(oc_calibrator *c, double from_x, double from_y,
                                  double to_x, double to_y, int64_t *t)
{
    until_phase_change(c, from_x, from_y, true, t);   /* SETTLE -> SAMPLE */
    until_phase_change(c, to_x, to_y, true, t);       /* SAMPLE -> CONFIRM */
    return until_phase_change(c, to_x, to_y, true, t); /* CONFIRM -> next */
}

int main(void)
{
    group("targets");
    {
        check_str(oc_calib_target_at(0)->label, "Point your nose at the middle dot",
                  "the centre is measured first");
        /* Every label must name the head, not the eyes: moving only the eyes
         * produces no signal, because the nose landmark is what is tracked. */
        int says_head = 1;
        for (int i = 0; i < OC_CALIB_POINTS; i++)
            if (!strstr(oc_calib_target_at(i)->label, "nose")) says_head = 0;
        check(says_head, "every instruction names the nose, never the eyes");

        check_near(oc_calib_target_at(1)->x_pct, 8.0, 1e-9, "top-left sits near the corner");
        check_near(oc_calib_target_at(4)->y_pct, 92.0, 1e-9, "bottom-right sits near the corner");
        check(oc_calib_target_at(99) == oc_calib_target_at(OC_CALIB_POINTS - 1),
              "an out-of-range step is clamped");
    }

    group("phase ordering");
    {
        oc_calibrator c;
        oc_calibrator_init(&c);
        int64_t t = 5000;

        oc_calib_progress p = oc_calibrator_update(&c, 0.5, 0.5, true, t);
        check_int(p.phase, OC_CALIB_IDLE, "nothing happens until it is started");

        oc_calibrator_start(&c, t);
        p = run(&c, 0.5, 0.5, true, 500, &t);
        check_int(p.phase, OC_CALIB_SETTLE, "it begins by settling");
        check_int(p.countdown, 3, "and counts down from three");

        p = run(&c, 0.5, 0.5, true, 1200, &t);
        check_int(p.countdown, 2, "the countdown advances");

        p = until_phase_change(&c, 0.5, 0.5, true, &t);
        check_int(p.phase, OC_CALIB_SAMPLE, "then samples");
        check_int(p.step, 0, "still on the first point");

        p = until_phase_change(&c, 0.5, 0.5, true, &t);
        check_int(p.phase, OC_CALIB_CONFIRM, "then confirms");

        p = until_phase_change(&c, 0.5, 0.5, true, &t);
        check_int(p.step, 1, "and moves to the next point");
        check_int(p.phase, OC_CALIB_SETTLE, "which settles again");
    }

    group("settling is not sampled");
    {
        /* THE invariant of this file. The patient is at 0.9 while the dot is
         * still being travelled to, and arrives at 0.1. If settling were
         * averaged in, the recorded point would land between the two. */
        oc_calibrator c;
        oc_calibrator_init(&c);
        int64_t t = 5000;
        oc_calibrator_start(&c, t);

        until_phase_change(&c, 0.9, 0.9, true, &t);   /* travelling */
        until_phase_change(&c, 0.1, 0.1, true, &t);   /* arrived, sampled */

        check_near(c.points[0].x, 0.1, 0.01, "only the settled position is recorded");
        check_near(c.points[0].y, 0.1, 0.01, "on both axes");
        check(c.points[0].x < 0.3, "the journey to the dot is not folded into the average");
    }

    group("a full run");
    {
        oc_calibrator c;
        oc_calibrator_init(&c);
        int64_t t = 5000;
        oc_calibrator_start(&c, t);

        do_point(&c, 0.5, 0.5, 0.50, 0.50, &t);   /* centre */
        do_point(&c, 0.5, 0.5, 0.30, 0.30, &t);   /* tl */
        do_point(&c, 0.3, 0.3, 0.70, 0.30, &t);   /* tr */
        do_point(&c, 0.7, 0.3, 0.30, 0.70, &t);   /* bl */
        oc_calib_progress p = do_point(&c, 0.3, 0.7, 0.70, 0.70, &t); /* br */

        check(p.finished, "the run reports that it finished");
        check_int(c.phase, OC_CALIB_DONE, "and lands on done");
        check(c.result.valid, "the bounds are usable");
        check_near(c.result.range_x, 0.4, 0.02, "horizontal span matches the head movement");
        check_near(c.result.range_y, 0.4, 0.02, "vertical span matches too");
    }

    group("a run too small to trust");
    {
        /* A patient who barely moved. Better to ask again than to track badly:
         * at this span, sensor noise alone could invert left and right. */
        oc_calibrator c;
        oc_calibrator_init(&c);
        int64_t t = 5000;
        oc_calibrator_start(&c, t);

        do_point(&c, 0.5, 0.5, 0.500, 0.500, &t);
        do_point(&c, 0.5, 0.5, 0.495, 0.495, &t);
        do_point(&c, 0.5, 0.5, 0.505, 0.495, &t);
        do_point(&c, 0.5, 0.5, 0.495, 0.505, &t);
        oc_calib_progress p = do_point(&c, 0.5, 0.5, 0.505, 0.505, &t);

        check(p.finished, "it still finishes");
        check_int(c.phase, OC_CALIB_FAILED, "but reports failure rather than bad bounds");
        check(!c.result.valid, "and produces nothing usable");
    }

    group("losing the face");
    {
        oc_calibrator c;
        oc_calibrator_init(&c);
        int64_t t = 5000;
        oc_calibrator_start(&c, t);

        until_phase_change(&c, 0.5, 0.5, true, &t);
        /* Face lost for the whole sampling window: nothing was measured, so the
         * point must not be recorded from stale data. */
        oc_calib_progress p = until_phase_change(&c, 0.5, 0.5, false, &t);
        check_int(p.phase, OC_CALIB_SETTLE, "the point restarts instead of being guessed");
        check_int(p.step, 0, "and stays on the same point");

        /* A partial loss still averages only the frames that had a face. */
        oc_calibrator_start(&c, t);
        until_phase_change(&c, 0.5, 0.5, true, &t);
        run(&c, 0.2, 0.2, false, 1000, &t);
        until_phase_change(&c, 0.8, 0.8, true, &t);
        check_near(c.points[0].x, 0.8, 0.02, "frames without a face are not averaged in");
    }

    return oc_report("test_calibrate");
}
