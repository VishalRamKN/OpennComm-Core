/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "oc_test.h"
#include "openncomm/answers.h"

int main(void)
{
    group("intent detection");
    {
        check_int(oc_detect_intent("Are you in pain?"), OC_INTENT_PAIN, "pain");
        check_int(oc_detect_intent("Does your back hurt?"), OC_INTENT_PAIN, "hurt is pain");
        check_int(oc_detect_intent("Do you want water?"), OC_INTENT_WATER, "water");
        check_int(oc_detect_intent("Are you thirsty?"), OC_INTENT_WATER, "thirsty is water");
        check_int(oc_detect_intent("Are you hungry?"), OC_INTENT_FOOD, "hungry is food");
        check_int(oc_detect_intent("Do you want to sleep?"), OC_INTENT_TIRED, "sleep is tired");
        check_int(oc_detect_intent("Should I call the nurse?"), OC_INTENT_HELP, "nurse is help");
        check_int(oc_detect_intent("Do you want to watch a movie?"), OC_INTENT_ACTIVITY, "movie");
        check_int(oc_detect_intent("What is your name?"), OC_INTENT_NAME, "name");
        check_int(oc_detect_intent("Is the light too bright?"), OC_INTENT_YES_NO, "yes/no prefix");
        check_int(oc_detect_intent("Tell me about your day"), OC_INTENT_GENERAL, "general fallback");
        check_int(oc_detect_intent(""), OC_INTENT_GENERAL, "an empty question is general");
        check_int(oc_detect_intent(NULL), OC_INTENT_GENERAL, "a null question is general");

        check_int(oc_detect_intent("ARE YOU IN PAIN?"), OC_INTENT_PAIN, "matching ignores case");
        check_int(oc_detect_intent("  Are you tired?"), OC_INTENT_TIRED, "leading space is ignored");

        /* "name" is checked before the yes/no prefix rule, or "Is your name
         * Anand?" would be classified as a bare yes/no question. */
        check_int(oc_detect_intent("Is your name Anand?"), OC_INTENT_NAME,
                  "name wins over the yes/no prefix");
    }

    group("phrasebook");
    {
        oc_options o;
        for (int i = 0; i < OC_INTENT_COUNT; i++) {
            oc_phrasebook((oc_intent)i, "Anand", &o);
            char label[96];
            int nonempty = 0, distinct = 1;
            for (int k = 0; k < OC_OPTION_COUNT; k++) if (o.text[k][0]) nonempty++;
            for (int a = 0; a < OC_OPTION_COUNT; a++)
                for (int b = a + 1; b < OC_OPTION_COUNT; b++)
                    if (strcmp(o.text[a], o.text[b]) == 0) distinct = 0;

            snprintf(label, sizeof label, "%s offers four answers", oc_intent_name(i));
            check_int(nonempty, OC_OPTION_COUNT, label);
            snprintf(label, sizeof label, "%s answers are all different", oc_intent_name(i));
            check(distinct, label);

            snprintf(label, sizeof label, "%s answers all pass the filter", oc_intent_name(i));
            int ok = 1;
            for (int k = 0; k < OC_OPTION_COUNT; k++)
                if (!oc_answer_acceptable(o.text[k])) ok = 0;
            check(ok, label);
        }

        oc_phrasebook(OC_INTENT_NAME, "Anand", &o);
        check_str(o.text[0], "My name is Anand", "the patient's name is substituted");

        /* An unnamed patient must not be offered "My name is " to say. */
        oc_phrasebook(OC_INTENT_NAME, NULL, &o);
        check(strstr(o.text[0], "name is") == NULL, "no dangling name when none is known");
        check(oc_answer_acceptable(o.text[0]), "and the replacement is still usable");
    }

    group("model output filtering");
    {
        check(oc_answer_acceptable("I am in pain"), "a normal answer passes");
        check(oc_answer_acceptable("Yes"), "a one-word answer passes");
        check(!oc_answer_acceptable(""), "an empty line is rejected");
        check(!oc_answer_acceptable("   "), "whitespace is rejected");
        check(!oc_answer_acceptable("Here are four answers:"), "a preamble is rejected");
        check(!oc_answer_acceptable("1. I am in pain"), "a numbered item is rejected");
        check(!oc_answer_acceptable("----"), "a separator is rejected");
        check(oc_answer_acceptable("- I am in pain"), "a bullet is stripped, not rejected");
        check(!oc_answer_acceptable("I would very much like you to please call the nurse now"),
              "a runaway sentence is rejected");
    }

    group("assembling the four buttons");
    {
        oc_options o;

        const char *good[] = { "I am in pain", "I feel fine right now",
                               "My back is hurting", "Please call the nurse" };
        int n = oc_options_from_model(good, 4, OC_INTENT_PAIN, "Anand", &o);
        check_int(n, 4, "four clean answers are all taken from the model");
        check_str(o.text[0], "I am in pain", "in order");

        /* The realistic failure: a preamble, a blank, a duplicate and a bullet. */
        const char *messy[] = { "Here are four options:", "", "I am in pain",
                                "- I am in pain", "My back is hurting" };
        n = oc_options_from_model(messy, 5, OC_INTENT_PAIN, "Anand", &o);
        check_int(n, 2, "junk and duplicates are dropped");
        check_str(o.text[0], "I am in pain", "the first real answer survives");
        check_str(o.text[1], "My back is hurting", "as does the second");
        for (int i = 0; i < OC_OPTION_COUNT; i++) {
            char label[64];
            snprintf(label, sizeof label, "button %d is filled", i + 1);
            check(o.text[i][0] != '\0', label);
        }

        /* The model produced nothing usable at all -- the patient must still be
         * able to answer. */
        const char *rubbish[] = { "Here are the options:", "1.", "----" };
        n = oc_options_from_model(rubbish, 3, OC_INTENT_WATER, "Anand", &o);
        check_int(n, 0, "nothing usable came from the model");
        check_str(o.text[0], "Yes I want water", "the phrasebook fills every button");
        check(o.text[3][0] != '\0', "including the last");

        n = oc_options_from_model(NULL, 0, OC_INTENT_TIRED, NULL, &o);
        check_int(n, 0, "no model output at all is handled");
        check(o.text[0][0] && o.text[3][0], "and four answers are still offered");
    }

    return oc_report("test_answers");
}
