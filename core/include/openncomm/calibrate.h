/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Five-point gaze calibration.
 *
 * The patient points their nose at five dots in turn and the corners are turned
 * into screen bounds by oc_calib_compute(). Until this has been done, head mode
 * runs on a raw fallback that assumes a mirrored feed and a centred head, which
 * works but is only ever approximate.
 *
 * Each point is measured in two phases, and keeping them separate is the whole
 * point of this file:
 *
 *   SETTLE  -- the dot has moved; the patient is still travelling towards it.
 *              NOTHING is sampled. A visible countdown runs.
 *   SAMPLE  -- the head has arrived; positions are averaged.
 *   CONFIRM -- a brief pause on a tick so the step feels finished.
 *
 * An earlier version sampled from the moment the dot moved, which folded the
 * journey into the average and skewed every calibration point towards the
 * previous one. Do not merge these phases back together.
 *
 * Nothing starts until oc_calibrator_start() is called, which the UI does from
 * a Start button rather than on opening the screen. Being dropped straight into
 * a countdown rushes the patient, and for calibration it actively corrupts the
 * result.
 */
#ifndef OPENNCOMM_CALIBRATE_H
#define OPENNCOMM_CALIBRATE_H

#include <stdbool.h>
#include <stdint.h>

#include "openncomm/gaze.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_CALIB_SETTLE_MS  3000
#define OC_CALIB_SAMPLE_MS  3500
#define OC_CALIB_CONFIRM_MS  900
#define OC_CALIB_POINTS        5

typedef enum {
    OC_CALIB_IDLE = 0,
    OC_CALIB_SETTLE,
    OC_CALIB_SAMPLE,
    OC_CALIB_CONFIRM,
    OC_CALIB_DONE,
    OC_CALIB_FAILED   /* too little head movement to be usable -- try again */
} oc_calib_phase;

/* Where each dot sits, as a percentage of the screen. The centre point is
 * measured first: it is not used for the bounds, but it gives the patient a
 * gentle start and lets the caregiver confirm tracking works before the
 * corners, which are the hard ones. */
typedef struct {
    double x_pct, y_pct;
    const char *label;
} oc_calib_target;

const oc_calib_target *oc_calib_target_at(int step);

typedef struct {
    oc_calib_phase phase;
    int step;                 /* 0..OC_CALIB_POINTS-1 */
    int64_t phase_start_ms;

    double accum_x, accum_y;
    int samples;

    oc_point points[OC_CALIB_POINTS]; /* centre, tl, tr, bl, br */
    oc_calib result;
} oc_calibrator;

typedef struct {
    oc_calib_phase phase;
    int step;
    /* 0..1 through the current phase, for a countdown or a filling bar. */
    double progress;
    /* Whole seconds remaining in SETTLE, for the 3-2-1 count. */
    int countdown;
    bool step_changed;
    bool finished;            /* phase is now DONE or FAILED */
} oc_calib_progress;

void oc_calibrator_init(oc_calibrator *c);
void oc_calibrator_start(oc_calibrator *c, int64_t now_ms);

/* Feed one frame. `face` false means the face was lost: sampling stalls rather
 * than averaging in a position nobody is holding. */
oc_calib_progress oc_calibrator_update(oc_calibrator *c, double nose_x, double nose_y,
                                       bool face, int64_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
