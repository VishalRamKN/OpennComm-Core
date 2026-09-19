/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Head-position tracking: 5-point calibration and quadrant mapping.
 *
 * What is tracked is the NOSE landmark, not the pupils. Moving only the eyes
 * produces no signal at all, so every user-facing label must say "head".
 */
#ifndef OPENNCOMM_GAZE_H
#define OPENNCOMM_GAZE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { double x, y; } oc_point;

/* Below this much separation between the left and right (or top and bottom)
 * calibration points, sensor noise could flip the recorded direction. Refuse to
 * build bounds from it rather than risk mapping the patient's gaze to the
 * opposite side of the screen. */
#define OC_MIN_CALIB_SPAN 0.04

typedef struct {
    bool valid;
    double min_x, min_y;
    double range_x, range_y; /* NEGATIVE range_x is normal on a mirrored feed */
    double pad_x, pad_y;
} oc_calib;

/* Derive screen edges from COLUMN AND ROW AVERAGES, not min/max.
 *
 * min/max discard which side is which: they assume the left-hand points always
 * have the smaller raw x, which is false on a mirrored camera feed and can
 * invert left/right outright when the head barely moves. Averaging both corners
 * of a column also beats trusting one extreme sample.
 *
 * Returns .valid = false when either span is under OC_MIN_CALIB_SPAN; the
 * caller must then ask the patient to calibrate again rather than silently
 * tracking badly. */
oc_calib oc_calib_compute(oc_point tl, oc_point tr, oc_point bl, oc_point br);

/* mirror_x applies ONLY when `cal` is NULL or invalid. Once calibrated, the
 * corner data encodes the camera's orientation and the flag is ignored.
 *
 * It defaults to TRUE for an uncalibrated session: a front-facing camera hands
 * back an un-flipped sensor image, so when the user looks RIGHT the nose moves
 * LEFT in raw coordinates. Defaulting to false inverted left/right on every
 * normal webcam while leaving up/down correct -- and that asymmetry is the
 * signature of this bug. If left/right is swapped but top/bottom is fine, look
 * here, not at the calibration maths. */
int oc_gaze_quadrant(const oc_calib *cal, bool mirror_x, double nx, double ny);

/* The same mapping, before it is reduced to a quadrant: where the nose points
 * as a fraction of the screen, 0..1 in each axis.
 *
 * Exposed so the interface can draw a pointer at the place the patient is
 * actually aiming. Sharing the arithmetic with oc_gaze_quadrant() is the whole
 * point -- a dot that disagreed with the highlighted button would be worse
 * than no dot, because the patient would trust the dot.
 *
 * Clamped to 0..1. Note that mirror_x, here as everywhere, applies only while
 * no calibration exists -- once calibrated, which way round the camera sees
 * the patient is already baked into the range. */
oc_point oc_gaze_position(const oc_calib *cal, bool mirror_x, double nx, double ny);

/* Unlike oc_gaze_quadrant() this deliberately uses the UNPADDED range -- the
 * dead zone is measured against the raw calibrated span. */
bool oc_gaze_confident_switch(const oc_calib *cal, bool mirror_x,
                              double nx, double ny, double dead);

/* Exponential smoothing applied to the raw nose position each frame. */
#define OC_SMOOTH 0.15
double oc_smooth_step(double current, double target);

#ifdef __cplusplus
}
#endif
#endif
