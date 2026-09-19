/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Blink-mode option scanning.
 *
 * For a patient who cannot move their neck. The four options are scanned rather
 * than pointed at: a short blink advances the highlight, a long blink chooses.
 *
 * Two rules here look like oversights and are not:
 *
 *  - Selection is gated on `options_loaded`, never on what the buttons say. An
 *    older version sniffed the button text and refused any answer beginning
 *    "Look", which silently rejected legitimate answers like "Look after me".
 *
 *  - While `input_locked` is set the patient is listening to their own answer
 *    being spoken, not answering. Every closure is ignored AND the tracker is
 *    reset, so whatever the eyes did during playback cannot leak into the next
 *    gesture.
 */
#ifndef OPENNCOMM_SELECT_H
#define OPENNCOMM_SELECT_H

#include "openncomm/blink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OC_SEL_NOTHING = 0,
    OC_SEL_ADVANCED,       /* highlight moved to .index */
    OC_SEL_COMMITTED,      /* .index was chosen; speak it */
    OC_SEL_NEEDS_QUESTION  /* a gesture arrived with nothing to answer */
} oc_sel_event_kind;

/* Slower than this and the patient waits through a full cycle for an answer
 * they can see; faster and the highlight is gone before they finish blinking. */
#define OC_AUTO_SCAN_MIN_S 2
#define OC_AUTO_SCAN_MAX_S 10

typedef struct {
    oc_blink_tracker tracker;
    int index;
    int option_count;
    bool options_loaded;
    bool input_locked;

    /* Auto-scan: advance the highlight on a timer, so a patient who blinks
     * with difficulty needs one long blink rather than up to four short ones.
     * Seconds; 0 is off, which is the default and the right default -- a
     * moving target is harder to hit than a still one, and this is for people
     * for whom each blink is expensive.
     *
     * It never moves the highlight out from under a blink in progress. A
     * patient who began holding on option 3 must still be holding on option 3
     * when the hold completes. */
    int auto_scan_seconds;
    int64_t last_scan_ms;
} oc_selector;

typedef struct {
    oc_sel_event_kind kind;
    int index;
    double hold_progress; /* -1 for "draw no bar" */
} oc_sel_event;

void oc_selector_init(oc_selector *s, oc_bands bands, int option_count);

/* Called when new options arrive: the highlight restarts at the first option so
 * it always begins somewhere predictable. */
void oc_selector_set_options(oc_selector *s, bool loaded);

/* Release the lock after an answer finishes being spoken. Discards any closure
 * in progress -- see the note in select.h's header comment. */
void oc_selector_unlock(oc_selector *s);

/* 0 turns it off. Any other value is clamped to something a person can
 * actually track. */
void oc_selector_set_auto_scan(oc_selector *s, int seconds);

oc_sel_event oc_selector_update(oc_selector *s, int blink, int64_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
