/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Eye Aspect Ratio -- how a blink is detected from face landmarks.
 *
 * EAR = (|p1-p5| + |p2-p4|) / (2 * |p0-p3|), the classic Soukupova & Cech
 * measure: two vertical eyelid distances over one horizontal eye width, so the
 * value is scale-invariant and does not care how far the patient is from the
 * camera.
 *
 * The threshold is ADAPTIVE, not fixed: 0.65 of a rolling 60-frame average.
 * Eye shape, glasses, lighting and camera angle all move the absolute EAR, and
 * a fixed threshold either misses a locked-in patient's blinks entirely or
 * fires constantly. The rolling average is also why the blink setup check
 * begins with three seconds of open eyes -- it lets the baseline settle before
 * the patient depends on it.
 *
 * INDEX NOTE: the six landmark indices per eye below refer to MediaPipe's
 * 478-point refined face mesh. They are not interchangeable with any other
 * landmark model. Swapping the model means re-tuning every constant here.
 */
#ifndef OPENNCOMM_EAR_H
#define OPENNCOMM_EAR_H

#include <stdbool.h>

#include "openncomm/gaze.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_EAR_HISTORY   60
#define OC_EAR_THRESHOLD 0.65
#define OC_LANDMARK_NOSE 1

extern const int OC_EYE_LEFT[6];  /* 362, 385, 387, 263, 373, 380 */
extern const int OC_EYE_RIGHT[6]; /*  33, 160, 158, 133, 153, 144 */

double oc_ear_for_eye(const oc_point *landmarks, const int idx[6]);

/* Mean of both eyes -- a one-eyed measurement is far noisier, and some patients
 * have asymmetric lid control. */
double oc_ear_both(const oc_point *landmarks);

typedef struct {
    double samples[OC_EAR_HISTORY];
    int count;
    int head;
    double sum;
} oc_ear_history;

void oc_ear_history_init(oc_ear_history *h);

/* Push one frame's EAR and report whether it counts as a closed eye. Returns 1
 * for closed, 0 for open. */
int oc_ear_push(oc_ear_history *h, double ear);

double oc_ear_average(const oc_ear_history *h);

#ifdef __cplusplus
}
#endif
#endif
