/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Measuring what a short and a long blink mean for THIS patient.
 *
 * The three presets in blink.h are guesses. They are good guesses, but people
 * differ by more than a factor of two, and a patient whose long blink lands in
 * another patient's short band cannot use the application at all. Head tracking
 * already gets calibrated; the eyes deserve the same.
 *
 * The patient is asked for a few deliberate short blinks, then a few deliberate
 * long ones. The bands are placed in the gap between the two clusters.
 *
 * Two decisions worth stating, because both look like over-caution and are not:
 *
 *  - Bounds come from the SECOND most extreme sample in each group, not the
 *    most extreme. One mistimed blink out of four is ordinary -- a cough, a
 *    dry eye, a moment of confusion about the instruction -- and letting a
 *    single sample set the boundary would hand that patient a threshold they
 *    cannot hit again. This is the same reasoning that made gaze.c average the
 *    calibration rows instead of taking their extremes.
 *
 *  - If the two clusters are not clearly separated the calibration FAILS and
 *    says so. It does not quietly split the difference. A patient whose short
 *    and long blinks overlap needs to be told to hold the long one longer, or
 *    to use another mode; inventing a threshold from overlapping data produces
 *    an application that misreads them several times a minute and gives no clue
 *    why.
 */
#ifndef OPENNCOMM_BLINKCAL_H
#define OPENNCOMM_BLINKCAL_H

#include "openncomm/blink.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Four of each. Enough that one bad sample can be outvoted, few enough that
 * the whole exercise costs the patient well under a minute. */
#define OC_BLINKCAL_SAMPLES 4

/* Below this separation between the clusters there is no honest place to put a
 * boundary: the dead band would be narrower than the patient's own variation
 * and selections would flip between short and long at random. */
#define OC_BLINKCAL_MIN_GAP_MS 90

/* A blink longer than this during calibration is an eye closing, not a gesture
 * -- dozing, a sneeze, or a patient who did not understand "long". */
#define OC_BLINKCAL_MAX_MS 3000

typedef enum {
    OC_BLINKCAL_IDLE = 0,   /* not started */
    OC_BLINKCAL_SHORT,      /* collecting short blinks */
    OC_BLINKCAL_LONG,       /* collecting long blinks */
    OC_BLINKCAL_DONE,       /* bands ready */
    OC_BLINKCAL_FAILED      /* clusters overlap; bands NOT written */
} oc_blinkcal_phase;

typedef struct {
    oc_blinkcal_phase phase;
    int short_ms[OC_BLINKCAL_SAMPLES];
    int long_ms[OC_BLINKCAL_SAMPLES];
    int short_count;
    int long_count;

    /* Written when phase reaches OC_BLINKCAL_DONE. */
    oc_bands bands;

    /* Set on failure so the caller can explain which way it went wrong rather
     * than saying "calibration failed" and leaving the caregiver guessing. */
    int measured_gap_ms;
} oc_blinkcal;

void oc_blinkcal_init(oc_blinkcal *c);

/* Begin collecting short blinks. Discards anything measured before. */
void oc_blinkcal_start(oc_blinkcal *c);

/* Feed one completed blink, measured eyes-closed to eyes-open.
 *
 * Returns 1 if the sample was accepted, 0 if it was ignored -- too short to be
 * deliberate, too long to be a gesture, or arriving when nothing is being
 * collected. An ignored sample is not an error; the patient simply blinks
 * again. */
int oc_blinkcal_record(oc_blinkcal *c, int duration_ms);

/* How far through the current group, 0.0 to 1.0. */
double oc_blinkcal_progress(const oc_blinkcal *c);

/* Undo the last accepted sample, stepping back a group if that empties the
 * current one. Returns 1 if something was removed. */
int oc_blinkcal_undo(oc_blinkcal *c);

#ifdef __cplusplus
}
#endif

#endif
