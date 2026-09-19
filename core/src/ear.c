/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/ear.h"

#include <math.h>
#include <string.h>

const int OC_EYE_LEFT[6]  = { 362, 385, 387, 263, 373, 380 };
const int OC_EYE_RIGHT[6] = {  33, 160, 158, 133, 153, 144 };

static double dist(oc_point a, oc_point b)
{
    return hypot(a.x - b.x, a.y - b.y);
}

double oc_ear_for_eye(const oc_point *lm, const int idx[6])
{
    oc_point p[6];
    for (int i = 0; i < 6; i++) p[i] = lm[idx[i]];

    double horizontal = dist(p[0], p[3]);
    if (horizontal <= 0.0) return 0.0;
    return (dist(p[1], p[5]) + dist(p[2], p[4])) / (2.0 * horizontal);
}

double oc_ear_both(const oc_point *lm)
{
    return (oc_ear_for_eye(lm, OC_EYE_LEFT) + oc_ear_for_eye(lm, OC_EYE_RIGHT)) / 2.0;
}

void oc_ear_history_init(oc_ear_history *h)
{
    memset(h, 0, sizeof(*h));
}

double oc_ear_average(const oc_ear_history *h)
{
    if (h->count == 0) return 0.0;
    return h->sum / (double)h->count;
}

int oc_ear_push(oc_ear_history *h, double ear)
{
    if (h->count < OC_EAR_HISTORY) {
        h->samples[h->count++] = ear;
        h->sum += ear;
    } else {
        h->sum -= h->samples[h->head];
        h->samples[h->head] = ear;
        h->sum += ear;
        h->head = (h->head + 1) % OC_EAR_HISTORY;
    }

    /* The sample just pushed is part of the average, deliberately. During a
     * long closure the average is dragged down by the closure itself, which is
     * what lets the threshold recover instead of latching shut forever. */
    double threshold = oc_ear_average(h) * OC_EAR_THRESHOLD;
    return ear < threshold ? 1 : 0;
}
