/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/calibrate.h"

#include <string.h>

/* Every label names the HEAD, never the eyes. "Look at the dot" invites the
 * patient to move only their eyes, which produces no signal at all -- the nose
 * landmark is what is tracked -- and makes a correct calibration feel broken. */
static const oc_calib_target k_targets[OC_CALIB_POINTS] = {
    { 50.0, 50.0, "Point your nose at the middle dot" },
    {  8.0,  8.0, "Point your nose at the top-left dot" },
    { 92.0,  8.0, "Point your nose at the top-right dot" },
    {  8.0, 92.0, "Point your nose at the bottom-left dot" },
    { 92.0, 92.0, "Point your nose at the bottom-right dot" },
};

const oc_calib_target *oc_calib_target_at(int step)
{
    if (step < 0) step = 0;
    if (step >= OC_CALIB_POINTS) step = OC_CALIB_POINTS - 1;
    return &k_targets[step];
}

void oc_calibrator_init(oc_calibrator *c)
{
    memset(c, 0, sizeof(*c));
    c->phase = OC_CALIB_IDLE;
}

void oc_calibrator_start(oc_calibrator *c, int64_t now_ms)
{
    oc_calibrator_init(c);
    c->phase = OC_CALIB_SETTLE;
    c->phase_start_ms = now_ms;
}

static void enter(oc_calibrator *c, oc_calib_phase phase, int64_t now_ms)
{
    c->phase = phase;
    c->phase_start_ms = now_ms;
}

oc_calib_progress oc_calibrator_update(oc_calibrator *c, double nose_x, double nose_y,
                                       bool face, int64_t now_ms)
{
    oc_calib_progress out = { .phase = c->phase, .step = c->step, .progress = 0.0,
                              .countdown = 0, .step_changed = false, .finished = false };

    const int64_t elapsed = now_ms - c->phase_start_ms;

    switch (c->phase) {
    case OC_CALIB_SETTLE:
        out.progress = (double)elapsed / OC_CALIB_SETTLE_MS;
        out.countdown = (int)((OC_CALIB_SETTLE_MS - elapsed + 999) / 1000);
        if (out.countdown < 0) out.countdown = 0;
        if (elapsed >= OC_CALIB_SETTLE_MS) {
            c->accum_x = c->accum_y = 0.0;
            c->samples = 0;
            enter(c, OC_CALIB_SAMPLE, now_ms);
        }
        break;

    case OC_CALIB_SAMPLE:
        /* Only average while a face is actually visible. Without this the mean
         * drifts towards wherever the last tracked position happened to be. */
        if (face) {
            c->accum_x += nose_x;
            c->accum_y += nose_y;
            c->samples++;
        }
        out.progress = (double)elapsed / OC_CALIB_SAMPLE_MS;
        if (elapsed >= OC_CALIB_SAMPLE_MS) {
            if (c->samples > 0) {
                c->points[c->step].x = c->accum_x / c->samples;
                c->points[c->step].y = c->accum_y / c->samples;
            } else {
                /* The face was never seen for this point. Stay here rather than
                 * recording a position that was never measured. */
                enter(c, OC_CALIB_SETTLE, now_ms);
                break;
            }
            enter(c, OC_CALIB_CONFIRM, now_ms);
        }
        break;

    case OC_CALIB_CONFIRM:
        out.progress = (double)elapsed / OC_CALIB_CONFIRM_MS;
        if (elapsed >= OC_CALIB_CONFIRM_MS) {
            if (c->step + 1 < OC_CALIB_POINTS) {
                c->step++;
                out.step_changed = true;
                enter(c, OC_CALIB_SETTLE, now_ms);
            } else {
                /* points[0] is the centre and is deliberately unused here: the
                 * bounds come from the four corners. */
                c->result = oc_calib_compute(c->points[1], c->points[2],
                                             c->points[3], c->points[4]);
                enter(c, c->result.valid ? OC_CALIB_DONE : OC_CALIB_FAILED, now_ms);
                out.finished = true;
            }
        }
        break;

    case OC_CALIB_DONE:
    case OC_CALIB_FAILED:
        out.finished = true;
        break;

    case OC_CALIB_IDLE:
    default:
        break;
    }

    out.phase = c->phase;
    out.step = c->step;
    if (out.progress > 1.0) out.progress = 1.0;
    if (out.progress < 0.0) out.progress = 0.0;
    return out;
}
