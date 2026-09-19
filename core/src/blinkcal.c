/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/blinkcal.h"

#include <string.h>

/* A blink shorter than this is involuntary even when the patient is trying.
 * Accepting it as a deliberate "short" would drag the whole scale down. */
#define MIN_DELIBERATE_MS 70

static void sort4(int *v, int n)
{
    for (int i = 1; i < n; i++) {
        const int key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
}

/* The second largest, or the largest when there are fewer than two samples.
 * See the header: one mistimed blink out of four must not set a boundary the
 * patient then has to hit every time. */
static int robust_high(const int *v, int n)
{
    int s[OC_BLINKCAL_SAMPLES];
    memcpy(s, v, (size_t)n * sizeof(int));
    sort4(s, n);
    return n >= 2 ? s[n - 2] : s[n - 1];
}

static int robust_low(const int *v, int n)
{
    int s[OC_BLINKCAL_SAMPLES];
    memcpy(s, v, (size_t)n * sizeof(int));
    sort4(s, n);
    return n >= 2 ? s[1] : s[0];
}

static int lowest(const int *v, int n)
{
    int lo = v[0];
    for (int i = 1; i < n; i++) if (v[i] < lo) lo = v[i];
    return lo;
}

/* Place the bands once both groups are full.
 *
 * The dead band between short_max_ms and long_ms is the whole point: a blink
 * that lands in it selects nothing. Widening it costs the patient a repeated
 * blink; narrowing it costs them a word they did not choose being spoken
 * aloud. The split below gives the gap's middle 60% to the dead band. */
static void finish(oc_blinkcal *c)
{
    const int short_hi = robust_high(c->short_ms, c->short_count);
    const int long_lo  = robust_low(c->long_ms, c->long_count);
    const int gap = long_lo - short_hi;

    c->measured_gap_ms = gap;

    if (gap < OC_BLINKCAL_MIN_GAP_MS) {
        c->phase = OC_BLINKCAL_FAILED;
        return;
    }

    c->bands.short_max_ms = short_hi + gap / 5;
    c->bands.long_ms      = long_lo - gap / 5;

    /* Anything appreciably faster than the patient's own quickest deliberate
     * blink was not aimed at us. Six tenths of it leaves room for a good day
     * without opening the door to every flutter. */
    int ignore = (lowest(c->short_ms, c->short_count) * 3) / 5;
    if (ignore < 40) ignore = 40;
    if (ignore > c->bands.short_max_ms - 40) ignore = c->bands.short_max_ms - 40;
    c->bands.ignore_ms = ignore;

    c->phase = OC_BLINKCAL_DONE;
}

void oc_blinkcal_init(oc_blinkcal *c)
{
    memset(c, 0, sizeof(*c));
    c->phase = OC_BLINKCAL_IDLE;
}

void oc_blinkcal_start(oc_blinkcal *c)
{
    oc_blinkcal_init(c);
    c->phase = OC_BLINKCAL_SHORT;
}

int oc_blinkcal_record(oc_blinkcal *c, int duration_ms)
{
    if (c->phase != OC_BLINKCAL_SHORT && c->phase != OC_BLINKCAL_LONG) return 0;
    if (duration_ms < MIN_DELIBERATE_MS) return 0;
    if (duration_ms > OC_BLINKCAL_MAX_MS) return 0;

    if (c->phase == OC_BLINKCAL_SHORT) {
        c->short_ms[c->short_count++] = duration_ms;
        if (c->short_count >= OC_BLINKCAL_SAMPLES) c->phase = OC_BLINKCAL_LONG;
        return 1;
    }

    c->long_ms[c->long_count++] = duration_ms;
    if (c->long_count >= OC_BLINKCAL_SAMPLES) finish(c);
    return 1;
}

double oc_blinkcal_progress(const oc_blinkcal *c)
{
    const int n = (c->phase == OC_BLINKCAL_LONG) ? c->long_count : c->short_count;
    return (double)n / (double)OC_BLINKCAL_SAMPLES;
}

int oc_blinkcal_undo(oc_blinkcal *c)
{
    /* Stepping back out of LONG with nothing recorded there returns to the
     * short group, so a patient who realises they misread the instruction can
     * back all the way out rather than restarting. */
    if (c->phase == OC_BLINKCAL_LONG && c->long_count == 0) {
        c->phase = OC_BLINKCAL_SHORT;
        if (c->short_count > 0) c->short_count--;
        return 1;
    }
    if (c->phase == OC_BLINKCAL_LONG && c->long_count > 0) { c->long_count--; return 1; }
    if (c->phase == OC_BLINKCAL_SHORT && c->short_count > 0) { c->short_count--; return 1; }
    return 0;
}
