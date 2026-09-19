/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* What the patient is offered to say.
 *
 * Two sources feed the same four buttons:
 *
 *   1. A phrasebook, keyed on a keyword-matched intent. Instant, deterministic,
 *      always available, costs nothing.
 *   2. A local language model, which personalises the wording.
 *
 * The phrasebook is NOT a last-resort fallback. It is shown the moment the
 * question is asked, and the model's answers replace it if and when they
 * arrive. On a machine with no model, a model still downloading, or a model
 * that produced nonsense, the patient sees four real answers rather than
 * "Yes / No / Maybe / Later" -- and never waits for a button to become usable.
 *
 * This is the design rule that makes local inference acceptable at all: a model
 * that takes three seconds is fine when nothing is blocked on it.
 */
#ifndef OPENNCOMM_ANSWERS_H
#define OPENNCOMM_ANSWERS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OC_OPTION_COUNT  4
#define OC_OPTION_MAX    96

typedef enum {
    OC_INTENT_NAME = 0,
    OC_INTENT_FOOD,
    OC_INTENT_WATER,
    OC_INTENT_TIRED,
    OC_INTENT_PAIN,
    OC_INTENT_HELP,
    OC_INTENT_ACTIVITY,
    OC_INTENT_YES_NO,
    OC_INTENT_GENERAL,
    OC_INTENT_COUNT
} oc_intent;

/* Keyword matching, deliberately not a model: it must be instant and it must
 * never surprise anyone. Checked in order, first match wins. */
oc_intent oc_detect_intent(const char *question);
const char *oc_intent_name(oc_intent intent);

typedef struct {
    char text[OC_OPTION_COUNT][OC_OPTION_MAX];
} oc_options;

/* Four curated answers for an intent, covering yes / no / a specific need / an
 * alternative -- the same spread the model is prompted for, so the two are
 * interchangeable on screen. `patient_name` may be NULL or empty. */
void oc_phrasebook(oc_intent intent, const char *patient_name, oc_options *out);

/* Is one line usable as a spoken answer?
 *
 * This exists to reject model output, not to police wording: preambles
 * ("Here are four options:"), numbered or bulleted list items, empty lines and
 * runaway sentences. A short answer like "Yes" is perfectly good and passes.
 * The 3-to-7-word target lives in the prompt, not here -- enforcing it as a
 * filter would throw away the model's best answers on a technicality. */
bool oc_answer_acceptable(const char *line);

/* Build the final four buttons from raw model output.
 *
 * Unacceptable lines are dropped and duplicates removed; any shortfall is
 * filled from the phrasebook. The result ALWAYS contains four non-empty
 * answers, whatever the model returned -- including nothing at all.
 * Returns how many came from the model. */
int oc_options_from_model(const char *const *lines, int line_count,
                          oc_intent intent, const char *patient_name,
                          oc_options *out);

#ifdef __cplusplus
}
#endif
#endif
