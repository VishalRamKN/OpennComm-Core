/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Blink calibration: deriving one patient's bands from their own blinks.
 *
 * The assertions that matter most here are the refusals. A calibration that
 * produces bands from overlapping data is worse than no calibration, because
 * it looks like it worked. */
#include "oc_test.h"
#include "openncomm/blinkcal.h"

/* Drive one closure of `hold` ms through the real tracker and report what it
 * decided. Four-millisecond steps rather than a camera's sixteen, so the test
 * is measuring the bands and not the frame quantisation. */
static oc_gesture gesture_for(oc_bands bands, int hold_ms)
{
    oc_blink_tracker t;
    oc_blink_tracker_init(&t, bands);
    int64_t now = 1000;
    oc_gesture seen = OC_GESTURE_NONE;

    for (int elapsed = 0; elapsed <= hold_ms; elapsed += 4) {
        oc_blink_event ev = oc_blink_tracker_update(&t, 1, now + elapsed);
        if (ev.gesture != OC_GESTURE_NONE) seen = ev.gesture;
    }
    oc_blink_event ev = oc_blink_tracker_update(&t, 0, now + hold_ms + 4);
    if (ev.gesture != OC_GESTURE_NONE) seen = ev.gesture;
    return seen;
}

static void feed_all(oc_blinkcal *c, const int *shorts, const int *longs)
{
    for (int i = 0; i < OC_BLINKCAL_SAMPLES; i++) oc_blinkcal_record(c, shorts[i]);
    for (int i = 0; i < OC_BLINKCAL_SAMPLES; i++) oc_blinkcal_record(c, longs[i]);
}

int main(void)
{
    group("phases");
    {
        oc_blinkcal c;
        oc_blinkcal_init(&c);
        check_int(c.phase, OC_BLINKCAL_IDLE, "starts idle");
        check_int(oc_blinkcal_record(&c, 200), 0, "records nothing before start");

        oc_blinkcal_start(&c);
        check_int(c.phase, OC_BLINKCAL_SHORT, "start begins with short blinks");
        for (int i = 0; i < OC_BLINKCAL_SAMPLES; i++) {
            check_int(oc_blinkcal_record(&c, 200), 1, "short sample accepted");
        }
        check_int(c.phase, OC_BLINKCAL_LONG, "moves to long blinks when short group fills");
        check_int(c.short_count, OC_BLINKCAL_SAMPLES, "short group is full");

        for (int i = 0; i < OC_BLINKCAL_SAMPLES - 1; i++) oc_blinkcal_record(&c, 700);
        check_int(c.phase, OC_BLINKCAL_LONG, "still collecting with one to go");
        oc_blinkcal_record(&c, 700);
        check_int(c.phase, OC_BLINKCAL_DONE, "finishes when long group fills");
    }

    group("rejects what was not a deliberate blink");
    {
        oc_blinkcal c;
        oc_blinkcal_start(&c);
        check_int(oc_blinkcal_record(&c, 30), 0, "an involuntary flutter is ignored");
        check_int(oc_blinkcal_record(&c, 69), 0, "just under the deliberate floor is ignored");
        check_int(c.short_count, 0, "nothing was stored");
        check_int(oc_blinkcal_record(&c, OC_BLINKCAL_MAX_MS + 1), 0, "dozing is ignored");
        check_int(c.short_count, 0, "still nothing stored");
        check_int(oc_blinkcal_record(&c, 70), 1, "the floor itself is accepted");
    }

    group("bands land between the two clusters");
    {
        oc_blinkcal c;
        oc_blinkcal_start(&c);
        const int shorts[] = { 180, 200, 210, 190 };
        const int longs[]  = { 700, 740, 690, 720 };
        feed_all(&c, shorts, longs);

        check_int(c.phase, OC_BLINKCAL_DONE, "calibration succeeds on clean data");
        check(c.bands.short_max_ms > 200, "short band clears the patient's short blinks");
        check(c.bands.long_ms < 700, "long threshold is reachable by their long blinks");
        check(c.bands.short_max_ms < c.bands.long_ms, "the dead band is not empty");
        check(c.bands.ignore_ms < c.bands.short_max_ms, "ignore floor sits below the short band");
        check(c.bands.ignore_ms >= 40, "ignore floor is never zero");
    }

    group("one mistimed blink does not set the boundary");
    {
        /* Three tidy short blinks and one that ran long. The boundary must
         * follow the three, not the outlier. */
        oc_blinkcal tidy, noisy;
        oc_blinkcal_start(&tidy);
        oc_blinkcal_start(&noisy);
        const int clean_shorts[]  = { 180, 200, 210, 190 };
        const int noisy_shorts[]  = { 180, 200, 460, 190 };
        const int longs[]         = { 800, 840, 790, 820 };
        feed_all(&tidy, clean_shorts, longs);
        feed_all(&noisy, noisy_shorts, longs);

        check_int(noisy.phase, OC_BLINKCAL_DONE, "still succeeds with one stray sample");
        check(noisy.bands.short_max_ms - tidy.bands.short_max_ms < 60,
              "a single stray blink barely moves the boundary");
    }

    group("refuses to guess when the clusters overlap");
    {
        oc_blinkcal c;
        oc_blinkcal_start(&c);
        const int shorts[] = { 300, 320, 340, 310 };
        const int longs[]  = { 360, 380, 350, 370 };
        feed_all(&c, shorts, longs);

        check_int(c.phase, OC_BLINKCAL_FAILED, "overlapping blinks fail rather than average");
        check(c.measured_gap_ms < OC_BLINKCAL_MIN_GAP_MS, "the measured gap is reported");
        check_int(c.bands.short_max_ms, 0, "no bands are written on failure");
        check_int(c.bands.long_ms, 0, "no long threshold is written on failure");
    }

    group("the bands it writes actually classify the blinks it saw");
    {
        oc_blinkcal c;
        oc_blinkcal_start(&c);
        const int shorts[] = { 150, 170, 160, 180 };
        const int longs[]  = { 620, 680, 650, 700 };
        feed_all(&c, shorts, longs);
        check_int(c.phase, OC_BLINKCAL_DONE, "calibrated");

        /* The real proof: feed the same durations back through the tracker
         * using the derived bands and check each one lands where the patient
         * intended it to. Bands that do not classify the very blinks they were
         * measured from are useless however tidy the arithmetic looks. */
        for (int i = 0; i < OC_BLINKCAL_SAMPLES; i++) {
            check(gesture_for(c.bands, shorts[i]) == OC_GESTURE_SHORT,
                  "their short blink reads as short");
            check(gesture_for(c.bands, longs[i]) == OC_GESTURE_LONG,
                  "their long blink reads as long");
        }
        check(gesture_for(c.bands, 20) == OC_GESTURE_DISCARD, "a flutter selects nothing");
        check(gesture_for(c.bands, (c.bands.short_max_ms + c.bands.long_ms) / 2)
                  == OC_GESTURE_DISCARD,
              "a blink in the dead band selects nothing at all");
    }

    group("undo");
    {
        oc_blinkcal c;
        oc_blinkcal_start(&c);
        check_int(oc_blinkcal_undo(&c), 0, "nothing to undo at the start");
        oc_blinkcal_record(&c, 200);
        oc_blinkcal_record(&c, 210);
        check_int(c.short_count, 2, "two short samples in");
        check_int(oc_blinkcal_undo(&c), 1, "undo removes one");
        check_int(c.short_count, 1, "one short sample left");

        while (c.phase == OC_BLINKCAL_SHORT) oc_blinkcal_record(&c, 200);
        check_int(c.phase, OC_BLINKCAL_LONG, "now on the long group");
        check_int(c.long_count, 0, "with nothing recorded there yet");
        check_int(oc_blinkcal_undo(&c), 1, "undo at the top of a group steps back");
        check_int(c.phase, OC_BLINKCAL_SHORT, "back to the short group");
        check_int(c.short_count, OC_BLINKCAL_SAMPLES - 1, "and one short sample was dropped");
    }

    group("progress");
    {
        oc_blinkcal c;
        oc_blinkcal_start(&c);
        check_near(oc_blinkcal_progress(&c), 0.0, 1e-9, "starts at zero");
        oc_blinkcal_record(&c, 200);
        oc_blinkcal_record(&c, 200);
        check_near(oc_blinkcal_progress(&c), 0.5, 1e-9, "half way through the short group");
    }

    return oc_report("test_blinkcal");
}
