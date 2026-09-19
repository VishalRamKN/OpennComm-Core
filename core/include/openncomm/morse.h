/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Morse spelling: the escape hatch from four fixed options.
 *
 * The patient performs exactly two gestures and nothing else -- the same short
 * and long blink that blink mode uses, so the skill transfers and one setup
 * check covers both modes. Everything structural (letter breaks, word breaks)
 * comes from pauses; everything corrective (undo, space, clear, speak) is a
 * caregiver button. Do not add a third patient gesture without a strong reason:
 * crowding more bands into eyelid timing is where this mode becomes unusable.
 *
 * Why a pause and not auto-commit: morse is not prefix-free. "." is E, ".." is
 * I, "..." is S. The symbols alone can never tell you a letter is finished, so
 * a pause is unavoidable. That is also why oc_morse_reachable() exists -- the
 * patient watches the candidate set narrow instead of spelling blind.
 *
 * An undecodable pattern is DROPPED with a message, never resolved to a nearest
 * guess. Spelling the wrong word on a patient's behalf is worse than making
 * them repeat a letter.
 */
#ifndef OPENNCOMM_MORSE_H
#define OPENNCOMM_MORSE_H

#include <stdbool.h>
#include <stdint.h>

#include "openncomm/blink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_MORSE_MAX_SYMBOLS 8   /* longest code in the table is 6 */
#define OC_MORSE_MAX_TEXT    512

/* A-Z, 0-9 and ? . , / -- 40 entries. */
#define OC_MORSE_TABLE_SIZE 40

const char *oc_morse_code_for(char c);          /* NULL if not in the table */
char        oc_morse_decode(const char *buf);   /* 0 if the pattern is unknown */

/* Letters still reachable from the symbols entered so far, written into `out`
 * (NUL-terminated). Returns the count. An empty buffer reaches nothing. */
int oc_morse_reachable(const char *buf, char *out, int cap);

/* The whole table, for drawing the always-on reference chart. */
const char *oc_morse_table_chars(void);

/* ---- input machine ------------------------------------------------------- */

typedef enum {
    OC_MORSE_NOTHING = 0,
    OC_MORSE_SYMBOL,      /* a dot or dash was appended to the buffer */
    OC_MORSE_LETTER,      /* the buffer committed as .letter */
    OC_MORSE_UNDECODABLE, /* the buffer was dropped; tell the patient */
    OC_MORSE_SPACE,       /* a word break was added */
    OC_MORSE_AUTOSEND     /* stillness elapsed; speak the message */
} oc_morse_event_kind;

typedef struct {
    oc_blink_tracker tracker;

    char buffer[OC_MORSE_MAX_SYMBOLS + 1]; /* symbols for the letter in progress */
    char text[OC_MORSE_MAX_TEXT];          /* message decoded so far */

    int64_t last_symbol_ms; /* 0 = nothing entered yet */
    bool spaced;            /* a space was already added for this pause */
    int64_t send_at_ms;     /* autosend countdown start; 0 = not running */

    /* The one timing dial the caregiver tunes. The word gap is derived from it
     * (x 2.2) so there is only ever one number to think about. */
    int letter_gap_ms;
    int autosend_seconds; /* 0 = off, caregiver presses Speak */

    bool input_locked;
} oc_morse;

typedef struct {
    oc_morse_event_kind kind;
    char symbol;  /* '.' or '-' for OC_MORSE_SYMBOL */
    char letter;  /* for OC_MORSE_LETTER */
    /* Live preview of the closure in progress: '.', '-', or 0 inside the dead
     * band. Lets the patient feel the dot/dash boundary instead of guessing. */
    char preview;
    int64_t held_ms;
    int autosend_remaining_ms; /* -1 when no countdown is running */
} oc_morse_event;

void oc_morse_init(oc_morse *m, oc_bands bands, int letter_gap_ms, int autosend_seconds);
int  oc_morse_word_gap_ms(const oc_morse *m);

oc_morse_event oc_morse_update(oc_morse *m, int blink, int64_t now_ms);

/* Caregiver controls. */
void oc_morse_undo(oc_morse *m);   /* trims in-progress symbols first, then text */
void oc_morse_space(oc_morse *m);  /* never doubles a space */
void oc_morse_clear(oc_morse *m);
void oc_morse_cancel_autosend(oc_morse *m);

#ifdef __cplusplus
}
#endif
#endif
