/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "openncomm/morse.h"

#include <string.h>

typedef struct { char ch; const char *code; } oc_morse_entry;

static const oc_morse_entry k_table[OC_MORSE_TABLE_SIZE] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."},
    {'?', "..--.."},{'.', ".-.-.-"},{',', "--..--"},{'/', "-..-."},
};

const char *oc_morse_code_for(char c)
{
    for (int i = 0; i < OC_MORSE_TABLE_SIZE; i++)
        if (k_table[i].ch == c) return k_table[i].code;
    return NULL;
}

char oc_morse_decode(const char *buf)
{
    if (!buf || !*buf) return 0;
    for (int i = 0; i < OC_MORSE_TABLE_SIZE; i++)
        if (strcmp(k_table[i].code, buf) == 0) return k_table[i].ch;
    return 0;
}

int oc_morse_reachable(const char *buf, char *out, int cap)
{
    int n = 0;
    if (out && cap > 0) out[0] = '\0';
    if (!buf || !*buf) return 0;

    size_t len = strlen(buf);
    for (int i = 0; i < OC_MORSE_TABLE_SIZE; i++) {
        if (strncmp(k_table[i].code, buf, len) == 0) {
            if (out && n < cap - 1) out[n] = k_table[i].ch;
            n++;
        }
    }
    if (out && cap > 0) out[n < cap - 1 ? n : cap - 1] = '\0';
    return n;
}

const char *oc_morse_table_chars(void)
{
    static char chars[OC_MORSE_TABLE_SIZE + 1];
    if (!chars[0]) {
        for (int i = 0; i < OC_MORSE_TABLE_SIZE; i++) chars[i] = k_table[i].ch;
        chars[OC_MORSE_TABLE_SIZE] = '\0';
    }
    return chars;
}

/* -------------------------------------------------------------------------- */

void oc_morse_init(oc_morse *m, oc_bands bands, int letter_gap_ms, int autosend_seconds)
{
    memset(m, 0, sizeof(*m));
    oc_blink_tracker_init(&m->tracker, bands);
    m->letter_gap_ms = letter_gap_ms > 0 ? letter_gap_ms : 1500;
    m->autosend_seconds = autosend_seconds > 0 ? autosend_seconds : 0;
}

/* A word break is simply a longer version of the letter break, so the caregiver
 * has one dial rather than two that can be set inconsistently. */
int oc_morse_word_gap_ms(const oc_morse *m)
{
    return (int)((double)m->letter_gap_ms * 2.2);
}

static void append_text(oc_morse *m, char c)
{
    size_t n = strlen(m->text);
    if (n + 1 >= OC_MORSE_MAX_TEXT) return;
    m->text[n] = c;
    m->text[n + 1] = '\0';
}

static void append_symbol(oc_morse *m, char sym)
{
    size_t n = strlen(m->buffer);
    if (n >= OC_MORSE_MAX_SYMBOLS) return;
    m->buffer[n] = sym;
    m->buffer[n + 1] = '\0';
}

void oc_morse_cancel_autosend(oc_morse *m) { m->send_at_ms = 0; }

void oc_morse_undo(oc_morse *m)
{
    size_t n = strlen(m->buffer);
    if (n > 0) { m->buffer[n - 1] = '\0'; return; }
    n = strlen(m->text);
    if (n > 0) m->text[n - 1] = '\0';
}

void oc_morse_space(oc_morse *m)
{
    size_t n = strlen(m->text);
    if (n == 0 || m->text[n - 1] == ' ') return;
    append_text(m, ' ');
}

void oc_morse_clear(oc_morse *m)
{
    m->buffer[0] = '\0';
    m->text[0] = '\0';
    m->last_symbol_ms = 0;
    m->spaced = false;
    m->send_at_ms = 0;
    oc_blink_tracker_reset(&m->tracker);
}

oc_morse_event oc_morse_update(oc_morse *m, int blink, int64_t now_ms)
{
    oc_morse_event out = { .kind = OC_MORSE_NOTHING, .symbol = 0, .letter = 0,
                           .preview = 0, .held_ms = 0, .autosend_remaining_ms = -1 };

    if (m->input_locked) {
        oc_blink_tracker_reset(&m->tracker);
        return out;
    }

    bool was_closed = m->tracker.eye_closed;
    oc_blink_event ev = oc_blink_tracker_update(&m->tracker, blink, now_ms);
    out.held_ms = ev.held_ms;

    if (blink) {
        /* A closure starting cancels any autosend countdown -- any blink at all
         * must be able to stop the machine speaking on its own. */
        if (!was_closed) m->send_at_ms = 0;

        const oc_bands *b = &m->tracker.bands;
        if (ev.held_ms >= b->long_ms)            out.preview = '-';
        else if (ev.held_ms > b->short_max_ms)   out.preview = 0;  /* dead band */
        else if (ev.held_ms >= b->ignore_ms)     out.preview = '.';
        return out;
    }

    /* The closure just ended.
     *
     * A long closure already resolved on the *closing* edge inside the tracker,
     * which reports OC_GESTURE_NONE on release so blink mode cannot commit
     * twice. Morse still wants the dash here, so classify on held_ms directly
     * rather than on the gesture. */
    if (was_closed) {
        if (ev.held_ms >= m->tracker.bands.long_ms) {
            append_symbol(m, '-');
            m->last_symbol_ms = now_ms;
            m->spaced = false;
            out.kind = OC_MORSE_SYMBOL;
            out.symbol = '-';
        } else if (ev.gesture == OC_GESTURE_SHORT) {
            append_symbol(m, '.');
            m->last_symbol_ms = now_ms;
            m->spaced = false;
            out.kind = OC_MORSE_SYMBOL;
            out.symbol = '.';
        }
        /* Anything else fell in the dead band or below ignore_ms and is dropped
         * for the same reason blink mode drops it: guessing between a dot and a
         * dash silently spells the wrong word. */
        return out;
    }

    /* Eyes open and steady -- pauses do the structural work. */
    if (!m->last_symbol_ms) return out;
    int64_t idle = now_ms - m->last_symbol_ms;

    if (m->buffer[0] && idle >= m->letter_gap_ms) {
        char letter = oc_morse_decode(m->buffer);
        if (letter) {
            append_text(m, letter);
            out.kind = OC_MORSE_LETTER;
            out.letter = letter;
        } else {
            out.kind = OC_MORSE_UNDECODABLE;
        }
        m->buffer[0] = '\0';
        return out;
    }

    if (!m->buffer[0] && !m->spaced && m->text[0] && idle >= oc_morse_word_gap_ms(m)) {
        oc_morse_space(m);
        m->spaced = true;
        out.kind = OC_MORSE_SPACE;
        return out;
    }

    /* Autosend: speak the message after a period of stillness, always behind a
     * visible countdown that any blink cancels. */
    if (m->autosend_seconds > 0 && m->text[0] && !m->buffer[0]) {
        int64_t window = (int64_t)m->autosend_seconds * 1000;
        if (!m->send_at_ms) m->send_at_ms = now_ms;
        int64_t elapsed = now_ms - m->send_at_ms;
        if (elapsed >= window) {
            m->send_at_ms = 0;
            out.kind = OC_MORSE_AUTOSEND;
        } else {
            out.autosend_remaining_ms = (int)(window - elapsed);
        }
    }

    return out;
}
