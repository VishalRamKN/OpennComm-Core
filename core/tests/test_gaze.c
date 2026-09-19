/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Head position to quadrant, calibrated and uncalibrated. */
#include <math.h>
#include "oc_test.h"
#include "openncomm/ear.h"
#include "openncomm/gaze.h"

/* Quadrants: 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right. */

int main(void)
{
    group("uncalibrated fallback");
    {
        /* Raw feed, no calibration. mirror_x defaults to true, so a nose to the
         * LEFT of the raw frame means the patient is looking RIGHT. */
        check_int(oc_gaze_quadrant(NULL, true, 0.2, 0.2), 1,
                  "an uncalibrated session assumes a mirrored feed");
        check_int(oc_gaze_quadrant(NULL, true, 0.8, 0.2), 0,
                  "and maps the other side to match");
        check_int(oc_gaze_quadrant(NULL, false, 0.2, 0.2), 0,
                  "stored mirror=false is respected");
        check_int(oc_gaze_quadrant(NULL, true, 0.2, 0.8), 3,
                  "up/down is never flipped by the mirror");

        oc_calib invalid = { .valid = false };
        check_int(oc_gaze_quadrant(&invalid, true, 0.2, 0.2), 1,
                  "an invalid calibration falls back to the mirror flag");
    }

    group("calibration bounds");
    {
        /* A healthy, non-mirrored calibration. */
        oc_calib c = oc_calib_compute(
            (oc_point){ 0.3, 0.3 }, (oc_point){ 0.7, 0.3 },
            (oc_point){ 0.3, 0.7 }, (oc_point){ 0.7, 0.7 });
        check(c.valid, "a healthy calibration is not flagged");
        check_near(c.range_x, 0.4, 1e-9, "horizontal span is measured from column averages");
        check_near(c.pad_x, 0.08, 1e-9, "padding is 20 percent of the span");

        /* Mirrored feed: the right-hand dots sit at SMALLER raw x. min/max would
         * silently swap the sides here; column averaging keeps the direction. */
        oc_calib mir = oc_calib_compute(
            (oc_point){ 0.7, 0.3 }, (oc_point){ 0.3, 0.3 },
            (oc_point){ 0.7, 0.7 }, (oc_point){ 0.3, 0.7 });
        check(mir.valid, "a mirrored calibration is still valid");
        check(mir.range_x < 0, "a negative horizontal range is normal on a mirrored feed");
        check_int(oc_gaze_quadrant(&mir, false, 0.65, 0.35), 0,
                  "calibrated mapping ignores the mirror flag");
        check_int(oc_gaze_quadrant(&mir, true, 0.65, 0.35), 0,
                  "and gives the same answer whatever the flag says");

        oc_calib narrow = oc_calib_compute(
            (oc_point){ 0.50, 0.3 }, (oc_point){ 0.52, 0.3 },
            (oc_point){ 0.50, 0.7 }, (oc_point){ 0.52, 0.7 });
        check(!narrow.valid, "a too-small horizontal span is rejected");

        oc_calib flat = oc_calib_compute(
            (oc_point){ 0.3, 0.50 }, (oc_point){ 0.7, 0.50 },
            (oc_point){ 0.3, 0.52 }, (oc_point){ 0.7, 0.52 });
        check(!flat.valid, "a too-small vertical span is rejected");
    }

    group("confident switch");
    {
        oc_calib c = oc_calib_compute(
            (oc_point){ 0.3, 0.3 }, (oc_point){ 0.7, 0.3 },
            (oc_point){ 0.3, 0.7 }, (oc_point){ 0.7, 0.7 });
        check(!oc_gaze_confident_switch(&c, false, 0.5, 0.5, 0.03),
              "dead centre is never a confident switch");
        check(oc_gaze_confident_switch(&c, false, 0.32, 0.32, 0.03),
              "a clear corner is a confident switch");
        check(!oc_gaze_confident_switch(&c, false, 0.5, 0.32, 0.03),
              "a corner on one axis only is not confident");
    }

    group("smoothing");
    {
        double x = 0.5;
        for (int i = 0; i < 100; i++) x = oc_smooth_step(x, 0.9);
        check_near(x, 0.9, 1e-3, "smoothing converges on the target");
        double one = oc_smooth_step(0.0, 1.0);
        check_near(one, OC_SMOOTH, 1e-9, "one step moves by exactly the smoothing factor");
    }

    group("eye aspect ratio");
    {
        /* Build a synthetic 478-point mesh: a wide-open eye, then a shut one. */
        static oc_point lm[478];
        for (int i = 0; i < 478; i++) lm[i] = (oc_point){ 0.5, 0.5 };

        const int *eyes[2] = { OC_EYE_LEFT, OC_EYE_RIGHT };
        for (int e = 0; e < 2; e++) {
            const int *ix = eyes[e];
            lm[ix[0]] = (oc_point){ 0.40, 0.50 };  /* outer corner */
            lm[ix[3]] = (oc_point){ 0.50, 0.50 };  /* inner corner */
            lm[ix[1]] = (oc_point){ 0.43, 0.47 };  /* upper lid */
            lm[ix[2]] = (oc_point){ 0.47, 0.47 };
            lm[ix[5]] = (oc_point){ 0.43, 0.53 };  /* lower lid */
            lm[ix[4]] = (oc_point){ 0.47, 0.53 };
        }
        double open_ear = oc_ear_both(lm);
        check(open_ear > 0.5, "an open eye has a high aspect ratio");

        oc_ear_history h;
        oc_ear_history_init(&h);
        int closed_frames = 0;
        for (int i = 0; i < 60; i++) closed_frames += oc_ear_push(&h, open_ear);
        check_int(closed_frames, 0, "a steady open eye never reads as a blink");
        check_near(oc_ear_average(&h), open_ear, 1e-9, "the rolling average tracks the input");

        /* Shut the lids: vertical distance collapses. */
        for (int e = 0; e < 2; e++) {
            const int *ix = eyes[e];
            lm[ix[1]] = (oc_point){ 0.43, 0.4995 };
            lm[ix[2]] = (oc_point){ 0.47, 0.4995 };
            lm[ix[5]] = (oc_point){ 0.43, 0.5005 };
            lm[ix[4]] = (oc_point){ 0.47, 0.5005 };
        }
        double shut_ear = oc_ear_both(lm);
        check(shut_ear < open_ear * OC_EAR_THRESHOLD, "a shut eye falls below the threshold");
        check_int(oc_ear_push(&h, shut_ear), 1, "and is detected as a closure");

        /* The threshold is adaptive: a patient whose eyes are simply narrower
         * must still register blinks, not sit permanently "closed". */
        oc_ear_history narrow;
        oc_ear_history_init(&narrow);
        int false_positives = 0;
        for (int i = 0; i < 60; i++) false_positives += oc_ear_push(&narrow, open_ear * 0.4);
        check_int(false_positives, 0, "a narrow-eyed patient does not read as permanently shut");
    }

    group("pointer position agrees with the quadrant it reports");
    {
        /* The dot and the highlight come from one mapping on purpose: a
         * patient trusts the dot, so a dot in a different quadrant from the
         * highlighted button would be actively misleading. */
        oc_calib cal = { .valid = true, .min_x = 0.3, .min_y = 0.3,
                         .range_x = 0.4, .range_y = 0.4, .pad_x = 0.0, .pad_y = 0.0 };

        for (int i = 0; i <= 40; i++) {
            for (int j = 0; j <= 40; j++) {
                const double nx = 0.205 + i * 0.015;
                const double ny = 0.205 + j * 0.015;
                const oc_point p = oc_gaze_position(&cal, false, nx, ny);
                /* The exact midline is the one place the two need not agree:
                 * oc_gaze_quadrant resolves a tie one way and a drawn dot sits
                 * on the line. Nothing selects there anyway. */
                if (fabs(p.x - 0.5) < 1e-9 || fabs(p.y - 0.5) < 1e-9) continue;
                const int q = oc_gaze_quadrant(&cal, false, nx, ny);
                const int from_point = (p.x > 0.5 ? 1 : 0) + (p.y > 0.5 ? 2 : 0);
                if (from_point != q) {
                    check(0, "position and quadrant disagree somewhere");
                    goto done;
                }
            }
        }
        check(1, "they agree across the whole field");
    done:;

        const oc_point mid = oc_gaze_position(&cal, false, 0.5, 0.5);
        check_near(mid.x, 0.5, 1e-9, "the middle of the calibrated range is the middle");
        check_near(mid.y, 0.5, 1e-9, "in both axes");

        const oc_point past = oc_gaze_position(&cal, false, 9.0, 9.0);
        check(past.x <= 1.0 && past.y <= 1.0, "far outside the range is clamped on screen");
        const oc_point before = oc_gaze_position(&cal, false, -9.0, -9.0);
        check(before.x >= 0.0 && before.y >= 0.0, "and clamped at the other end");

        /* mirror_x applies only while no calibration exists: once calibrated,
         * which way round the camera sees the patient is already in the range.
         * Both halves of that are worth pinning down. */
        const oc_calib none = { .valid = false };
        const oc_point raw = oc_gaze_position(&none, false, 0.3, 0.5);
        const oc_point flipped = oc_gaze_position(&none, true, 0.3, 0.5);
        check_near(raw.x, 0.3, 1e-9, "uncalibrated, the raw position passes through");
        check_near(flipped.x, 0.7, 1e-9, "and mirroring flips it");
        check_near(raw.y, flipped.y, 1e-9, "leaving the other axis alone");

        const oc_point cal_plain = oc_gaze_position(&cal, false, 0.4, 0.5);
        const oc_point cal_mirror = oc_gaze_position(&cal, true, 0.4, 0.5);
        check_near(cal_plain.x, cal_mirror.x, 1e-9,
                   "once calibrated, mirroring is already in the range and changes nothing");
    }

    return oc_report("test_gaze");
}
