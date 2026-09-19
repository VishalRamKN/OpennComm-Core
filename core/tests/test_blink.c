/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* The blink state machine, driven through the selector.
 *
 * The clock is injected, so a closure of any duration can be produced without
 * waiting for it. */
#include "oc_test.h"
#include "openncomm/select.h"

/* Drive a closure of `hold` ms: eye shuts at t, frames every 16ms, reopens. */
typedef struct {
    oc_selector *s;
    int64_t now;
    int advanced, committed, needs_question;
    int last_index;
    double last_progress;
} driver;

static void feed(driver *d, int blink, int64_t dt)
{
    d->now += dt;
    oc_sel_event ev = oc_selector_update(d->s, blink, d->now);
    if (ev.hold_progress >= 0) d->last_progress = ev.hold_progress;
    switch (ev.kind) {
    case OC_SEL_ADVANCED:       d->advanced++;       d->last_index = ev.index; break;
    case OC_SEL_COMMITTED:      d->committed++;      d->last_index = ev.index; break;
    case OC_SEL_NEEDS_QUESTION: d->needs_question++; break;
    default: break;
    }
}

static void blink_for(driver *d, int64_t hold_ms)
{
    feed(d, 1, 16);                       /* eye shuts */
    int64_t elapsed = 0;
    while (elapsed + 16 < hold_ms) { feed(d, 1, 16); elapsed += 16; }
    feed(d, 0, hold_ms - elapsed);        /* eye opens, total closure = hold_ms */
}

static driver fresh(oc_selector *s, bool options_loaded)
{
    oc_selector_init(s, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
    oc_selector_set_options(s, options_loaded);
    driver d = { .s = s, .now = 1000, .last_index = -1, .last_progress = -1 };
    return d;
}

int main(void)
{
    oc_selector s;

    group("blink speed presets");
    {
        for (int i = 0; i < OC_SPEED_COUNT; i++) {
            oc_bands b = oc_blink_bands_for((oc_speed)i);
            char label[96];
            /* The band must be wide enough that a cycling blink held a little
             * too long lands inside it rather than selecting. Asserted as a
             * proportion of short_max_ms so it scales with every preset.
             * See the note in blink.h on why normal is 1.474x, not 1.5x. */
            snprintf(label, sizeof label,
                     "%s keeps a wide ambiguous band", oc_speed_name(i));
            check(b.long_ms >= b.short_max_ms * 1.45 &&
                  b.long_ms <= b.short_max_ms * 1.55, label);

            snprintf(label, sizeof label,
                     "%s dead band is never squeezed out", oc_speed_name(i));
            check(b.long_ms - b.short_max_ms >= 140, label);

            snprintf(label, sizeof label, "%s has a real ignore floor", oc_speed_name(i));
            check(b.ignore_ms > 0 && b.ignore_ms < b.short_max_ms, label);
        }
        oc_bands fast = oc_blink_bands_for(OC_SPEED_FAST);
        oc_bands relaxed = oc_blink_bands_for(OC_SPEED_RELAXED);
        check(fast.long_ms < relaxed.long_ms, "a dash is quicker on fast than relaxed");
        check_int(oc_speed_from_name("nonsense"), OC_SPEED_NORMAL,
                  "an unknown speed name falls back to normal");
    }

    group("duration classification");
    {
        driver d = fresh(&s, true);
        blink_for(&d, 50);
        check_int(d.advanced, 0, "sub-threshold involuntary blink does not advance");
        check_int(d.committed, 0, "sub-threshold involuntary blink does not select");

        d = fresh(&s, true);
        blink_for(&d, 200);
        check_int(d.advanced, 1, "short blink advances to option 2");
        check_int(d.last_index, 1, "highlight moved to index 1");
        check_int(d.committed, 0, "short blink does not select");

        d = fresh(&s, true);
        blink_for(&d, 450);  /* dead band: 380..560 */
        check_int(d.advanced, 0, "ambiguous blink does not advance");
        check_int(d.committed, 0, "ambiguous blink does not select");

        d = fresh(&s, true);
        blink_for(&d, 700);
        check_int(d.committed, 1, "long blink selects");
        check_int(d.last_index, 0, "long blink posts the highlighted answer");
        check_int(d.advanced, 0, "long blink does not also advance the highlight");
    }

    group("cycling");
    {
        driver d = fresh(&s, true);
        blink_for(&d, 200); blink_for(&d, 200);
        check_int(d.last_index, 2, "two short blinks land on option 3");

        oc_selector_unlock(&s);
        blink_for(&d, 700);
        check_int(d.committed, 1, "selects the third option");
        check_int(d.last_index, 2, "the third option is what was posted");

        d = fresh(&s, true);
        for (int i = 0; i < 4; i++) blink_for(&d, 200);
        check_int(d.last_index, 0, "four short blinks wrap back to option 1");

        d = fresh(&s, true);
        for (int i = 0; i < 9; i++) blink_for(&d, 200);
        check_int(d.last_index, 1, "nine short blinks total land on option 2");
    }

    group("single commit");
    {
        driver d = fresh(&s, true);
        blink_for(&d, 4000);
        check_int(d.committed, 1, "a very long hold still selects exactly once");
        check_int(d.advanced, 0, "and the release is not read as a short blink");

        /* Still locked: the answer is being spoken. */
        blink_for(&d, 700);
        check_int(d.committed, 1, "second long blink after commit does not re-post");
        blink_for(&d, 200);
        check_int(d.advanced, 0, "blinks during playback are ignored");

        oc_selector_unlock(&s);
        blink_for(&d, 200);
        check_int(d.advanced, 1, "input resumes once the answer has been spoken");
    }

    group("gating before options load");
    {
        driver d = fresh(&s, false);
        blink_for(&d, 700);
        check_int(d.committed, 0, "long blink before options load selects nothing");
        check(d.needs_question > 0, "and explains why nothing happened");

        d = fresh(&s, false);
        blink_for(&d, 200);
        check_int(d.advanced, 0, "short blink before options load does not advance");
        check(d.needs_question > 0, "and explains why nothing happened");
    }

    group("hold feedback");
    {
        driver d = fresh(&s, true);
        feed(&d, 1, 16);
        for (int i = 0; i < 20; i++) feed(&d, 1, 16); /* ~336ms, under short max */
        check(d.last_progress < 0, "no bar is drawn inside the short-blink window");

        d = fresh(&s, true);
        feed(&d, 1, 16);
        for (int i = 0; i < 29; i++) feed(&d, 1, 16); /* ~480ms, inside the dead band */
        check(d.last_progress > 0.0 && d.last_progress < 1.0,
              "the bar fills once holding past the short window");
    }

    group("auto-scan");
    {
        /* Off by default: a moving target is harder to hit than a still one,
         * and every blink costs this patient something. */
        oc_selector s;
        oc_selector_init(&s, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
        check_int(s.auto_scan_seconds, 0, "off unless asked for");

        oc_selector_set_auto_scan(&s, 3);
        check_int(s.auto_scan_seconds, 3, "accepts a sensible interval");
        oc_selector_set_auto_scan(&s, 0);
        check_int(s.auto_scan_seconds, 0, "zero turns it off");
        oc_selector_set_auto_scan(&s, 1);
        check_int(s.auto_scan_seconds, OC_AUTO_SCAN_MIN_S, "too fast is clamped up");
        oc_selector_set_auto_scan(&s, 600);
        check_int(s.auto_scan_seconds, OC_AUTO_SCAN_MAX_S, "too slow is clamped down");
    }

    {
        oc_selector s;
        oc_selector_init(&s, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
        driver d = fresh(&s, true);
        oc_selector_set_auto_scan(&s, 2);
        /* Eye open throughout. The highlight should step once per interval. */
        feed(&d, 0, 16);
        check_int(d.advanced, 0, "nothing moves immediately");
        for (int i = 0; i < 130; i++) feed(&d, 0, 16); /* ~2.1s */
        check_int(d.advanced, 1, "advances once the interval passes");
        check_int(d.last_index, 1, "to the next option");
        for (int i = 0; i < 130; i++) feed(&d, 0, 16);
        check_int(d.advanced, 2, "and again");
        check_int(d.last_index, 2, "stepping through in order");
    }

    {
        /* The rule that matters: it must never move the highlight out from
         * under a blink in progress. A patient who starts holding on option 1
         * must still be on option 1 when the hold completes. */
        oc_selector s;
        oc_selector_init(&s, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
        driver d = fresh(&s, true);
        oc_selector_set_auto_scan(&s, 2);
        for (int i = 0; i < 130; i++) feed(&d, 0, 16);   /* scan to option 1 */
        check_int(d.last_index, 1, "sitting on option 1");

        /* Now hold well past a whole interval. */
        for (int i = 0; i < 200; i++) feed(&d, 1, 16);   /* ~3.2s held */
        check_int(d.committed, 1, "the hold committed");
        check_int(d.last_index, 1, "on the option the patient was holding, not a later one");
    }

    {
        /* A manual short blink restarts the interval, so one gesture does not
         * become two moves. */
        oc_selector s;
        oc_selector_init(&s, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
        driver d = fresh(&s, true);
        oc_selector_set_auto_scan(&s, 2);
        for (int i = 0; i < 100; i++) feed(&d, 0, 16);   /* 1.6s, not yet due */
        check_int(d.advanced, 0, "no auto-advance yet");

        feed(&d, 1, 16);
        for (int i = 0; i < 15; i++) feed(&d, 1, 16);    /* a short blink */
        feed(&d, 0, 16);
        check_int(d.advanced, 1, "the short blink advanced it");

        for (int i = 0; i < 60; i++) feed(&d, 0, 16);    /* ~1s more */
        check_int(d.advanced, 1, "and the interval restarted rather than firing at once");
    }

    {
        /* Nothing scans before a question has been asked. */
        oc_selector s;
        oc_selector_init(&s, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
        driver d = fresh(&s, false);
        oc_selector_set_auto_scan(&s, 2);
        for (int i = 0; i < 300; i++) feed(&d, 0, 16);
        check_int(d.advanced, 0, "no options, no scanning");
    }

    {
        /* And nothing scans while an answer is being spoken. */
        oc_selector s;
        oc_selector_init(&s, oc_blink_bands_for(OC_SPEED_NORMAL), 4);
        driver d = fresh(&s, true);
        oc_selector_set_auto_scan(&s, 2);
        s.input_locked = true;
        for (int i = 0; i < 300; i++) feed(&d, 0, 16);
        check_int(d.advanced, 0, "locked means locked");
    }

    return oc_report("test_blink");
}
