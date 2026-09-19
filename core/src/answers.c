/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/answers.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *k_intent_names[OC_INTENT_COUNT] = {
    "name", "food", "water", "tired", "pain", "help", "activity", "yes_no", "general"
};

const char *oc_intent_name(oc_intent intent)
{
    if (intent < 0 || intent >= OC_INTENT_COUNT) intent = OC_INTENT_GENERAL;
    return k_intent_names[intent];
}

static bool contains_ci(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == needle[i]) i++;
        if (i == nl) return true;
    }
    return false;
}

static bool starts_with_ci(const char *s, const char *prefix)
{
    for (size_t i = 0; prefix[i]; i++) {
        if (!s[i] || tolower((unsigned char)s[i]) != prefix[i]) return false;
    }
    return true;
}

static bool any_of(const char *q, const char *const *words, int n)
{
    for (int i = 0; i < n; i++) if (contains_ci(q, words[i])) return true;
    return false;
}

oc_intent oc_detect_intent(const char *question)
{
    if (!question) return OC_INTENT_GENERAL;
    while (*question == ' ') question++;

    static const char *food[]     = { "hungry", "eat", "food" };
    static const char *water[]    = { "water", "drink", "thirsty" };
    static const char *tired[]    = { "tired", "sleep", "rest", "fatigue" };
    static const char *pain[]     = { "pain", "hurt", "ache", "discomfort" };
    static const char *help[]     = { "help", "call", "emergency", "nurse", "doctor" };
    static const char *activity[] = { "movie", "watch", "music", "entertain" };
    static const char *yesno[]    = { "is ", "are ", "do ", "can ", "will ", "should " };

    /* Order matters and is inherited: "name" is checked first so "What is your
     * name?" is not swallowed by the yes/no prefix rule below. */
    if (contains_ci(question, "name")) return OC_INTENT_NAME;
    if (any_of(question, food, 3))     return OC_INTENT_FOOD;
    if (any_of(question, water, 3))    return OC_INTENT_WATER;
    if (any_of(question, tired, 4))    return OC_INTENT_TIRED;
    if (any_of(question, pain, 4))     return OC_INTENT_PAIN;
    if (any_of(question, help, 5))     return OC_INTENT_HELP;
    if (any_of(question, activity, 4)) return OC_INTENT_ACTIVITY;

    for (int i = 0; i < 6; i++)
        if (starts_with_ci(question, yesno[i])) return OC_INTENT_YES_NO;

    return OC_INTENT_GENERAL;
}

/* Four answers per intent: yes / no / a specific need / an alternative.
 *
 * Written in the first person and kept short, because they are spoken aloud by
 * a synthesiser and heard by someone who must act on them. "%s" is the
 * patient's name where one is known. */
static const char *k_phrasebook[OC_INTENT_COUNT][OC_OPTION_COUNT] = {
    [OC_INTENT_NAME] = {
        "My name is %s", "Please ask me again", "My caregiver can tell you",
        "I would rather not say",
    },
    [OC_INTENT_FOOD] = {
        "Yes I am hungry", "No I am not hungry", "Just a little please",
        "Maybe in a little while",
    },
    [OC_INTENT_WATER] = {
        "Yes I want water", "No thank you", "Just a small sip",
        "I would like something else",
    },
    [OC_INTENT_TIRED] = {
        "Yes I am tired", "No I am awake", "I want to sleep now",
        "I would like to sit up",
    },
    [OC_INTENT_PAIN] = {
        "Yes I am in pain", "No I feel alright", "Please call the nurse",
        "It is getting better",
    },
    [OC_INTENT_HELP] = {
        "Yes please help me", "No I am alright", "Please call the nurse",
        "Please stay with me",
    },
    [OC_INTENT_ACTIVITY] = {
        "Yes I would like that", "No not right now", "Maybe a little later",
        "I would prefer some music",
    },
    [OC_INTENT_YES_NO] = {
        "Yes", "No", "I am not sure", "Please ask me again",
    },
    [OC_INTENT_GENERAL] = {
        "Yes please", "No thank you", "I am not sure", "Please ask me again",
    },
};

void oc_phrasebook(oc_intent intent, const char *patient_name, oc_options *out)
{
    if (intent < 0 || intent >= OC_INTENT_COUNT) intent = OC_INTENT_GENERAL;
    const bool named = patient_name && patient_name[0];

    for (int i = 0; i < OC_OPTION_COUNT; i++) {
        const char *t = k_phrasebook[intent][i];
        if (strstr(t, "%s")) {
            if (named) snprintf(out->text[i], OC_OPTION_MAX, t, patient_name);
            else       snprintf(out->text[i], OC_OPTION_MAX, "%s", "Please ask my caregiver");
        } else {
            snprintf(out->text[i], OC_OPTION_MAX, "%s", t);
        }
    }
}

static void trim(const char *in, char *out, size_t cap)
{
    while (*in && (isspace((unsigned char)*in) || *in == '-' || *in == '*' ||
                   *in == '.' || *in == ')')) in++;
    size_t n = strlen(in);
    while (n > 0 && isspace((unsigned char)in[n - 1])) n--;
    if (n >= cap) n = cap - 1;
    memcpy(out, in, n);
    out[n] = '\0';
}

static int word_count(const char *s)
{
    int n = 0;
    bool in_word = false;
    for (; *s; s++) {
        if (isspace((unsigned char)*s)) { in_word = false; }
        else if (!in_word) { in_word = true; n++; }
    }
    return n;
}

bool oc_answer_acceptable(const char *line)
{
    if (!line) return false;

    char buf[OC_OPTION_MAX];
    trim(line, buf, sizeof buf);

    size_t len = strlen(buf);
    if (len == 0) return false;
    if (len >= OC_OPTION_MAX - 1) return false;

    /* A trailing colon is the signature of a preamble: "Here are four answers:" */
    if (buf[len - 1] == ':') return false;

    /* A list marker means the model ignored the format and started enumerating. */
    if (isdigit((unsigned char)buf[0])) return false;

    int words = word_count(buf);
    if (words < 1 || words > 8) return false;

    /* Must contain at least one letter -- rules out "----" and "1." leftovers. */
    for (const char *p = buf; *p; p++) if (isalpha((unsigned char)*p)) return true;
    return false;
}

static bool same_answer(const char *a, const char *b)
{
    for (;; a++, b++) {
        while (*a == ' ') a++;
        while (*b == ' ') b++;
        if (!*a && !*b) return true;
        if (!*a || !*b) return false;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
}

int oc_options_from_model(const char *const *lines, int line_count,
                          oc_intent intent, const char *patient_name,
                          oc_options *out)
{
    memset(out, 0, sizeof(*out));
    int filled = 0;

    for (int i = 0; i < line_count && filled < OC_OPTION_COUNT; i++) {
        if (!lines[i] || !oc_answer_acceptable(lines[i])) continue;

        char candidate[OC_OPTION_MAX];
        trim(lines[i], candidate, sizeof candidate);

        bool duplicate = false;
        for (int j = 0; j < filled; j++)
            if (same_answer(out->text[j], candidate)) { duplicate = true; break; }
        if (duplicate) continue;

        snprintf(out->text[filled], OC_OPTION_MAX, "%s", candidate);
        filled++;
    }

    const int from_model = filled;

    /* Top up from the phrasebook. The patient is never shown a blank button, and
     * never shown fewer than four choices, whatever the model did. */
    if (filled < OC_OPTION_COUNT) {
        oc_options book;
        oc_phrasebook(intent, patient_name, &book);
        for (int i = 0; i < OC_OPTION_COUNT && filled < OC_OPTION_COUNT; i++) {
            bool duplicate = false;
            for (int j = 0; j < filled; j++)
                if (same_answer(out->text[j], book.text[i])) { duplicate = true; break; }
            if (duplicate) continue;
            snprintf(out->text[filled], OC_OPTION_MAX, "%s", book.text[i]);
            filled++;
        }
    }

    return from_model;
}
