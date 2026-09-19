/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Two-blink morse entry: symbol timing, letter gaps, decoding. */
#include "oc_test.h"
#include "openncomm/morse.h"

typedef struct {
    oc_morse *m;
    int64_t now;
    int letters, undecodable, spaces, autosends;
    char last_letter;
} mdriver;

static oc_morse_event step(mdriver *d, int blink, int64_t dt)
{
    d->now += dt;
    oc_morse_event ev = oc_morse_update(d->m, blink, d->now);
    switch (ev.kind) {
    case OC_MORSE_LETTER:       d->letters++; d->last_letter = ev.letter; break;
    case OC_MORSE_UNDECODABLE:  d->undecodable++; break;
    case OC_MORSE_SPACE:        d->spaces++; break;
    case OC_MORSE_AUTOSEND:     d->autosends++; break;
    default: break;
    }
    return ev;
}

static void blink_for(mdriver *d, int64_t hold_ms)
{
    step(d, 1, 16);
    int64_t elapsed = 0;
    while (elapsed + 16 < hold_ms) { step(d, 1, 16); elapsed += 16; }
    step(d, 0, hold_ms - elapsed);
}

/* Hold the eyes open for `ms`, in frames, so pause-driven commits can fire. */
static void idle_for(mdriver *d, int64_t ms)
{
    int64_t elapsed = 0;
    while (elapsed < ms) { step(d, 0, 16); elapsed += 16; }
}

static mdriver fresh(oc_morse *m, int gap_ms, int autosend)
{
    oc_morse_init(m, oc_blink_bands_for(OC_SPEED_NORMAL), gap_ms, autosend);
    mdriver d = { .m = m, .now = 1000 };
    return d;
}

int main(void)
{
    oc_morse m;

    group("table");
    {
        check_str(oc_morse_code_for('S'), "...", "S is three dots");
        check_str(oc_morse_code_for('O'), "---", "O is three dashes");
        check_int(oc_morse_decode("...."), 'H', "decodes a known code");
        check_int(oc_morse_decode("......"), 0, "returns null for an unknown code");
        check(oc_morse_code_for('!') == NULL, "a character outside the table has no code");

        /* Two characters sharing a code would make decoding order-dependent. */
        const char *chars = oc_morse_table_chars();
        int dupes = 0;
        for (int i = 0; chars[i]; i++)
            for (int j = i + 1; chars[j]; j++)
                if (strcmp(oc_morse_code_for(chars[i]), oc_morse_code_for(chars[j])) == 0) dupes++;
        check_int(dupes, 0, "every code in the table is unique");
    }

    group("reachability");
    {
        char out[64];
        oc_morse_reachable(".", out, sizeof out);
        check(strchr(out, 'E') != NULL, "E is reachable from a single dot");
        check(strchr(out, 'S') != NULL, "S is reachable from a single dot");
        check(strchr(out, 'T') == NULL, "T is not reachable from a dot");

        int n_one = oc_morse_reachable(".", out, sizeof out);
        int n_two = oc_morse_reachable("..", out, sizeof out);
        check(n_two < n_one, "reachable set narrows as symbols arrive");

        oc_morse_reachable("....", out, sizeof out);
        check(strchr(out, 'H') && strchr(out, '4') && strchr(out, '5'),
              "four dots still reach H, 4 and 5");
        check_int(oc_morse_decode("...."), 'H', "four dots decode to H right now");

        check_int(oc_morse_reachable("", out, sizeof out), 0, "an empty buffer reaches nothing");
    }

    group("symbols");
    {
        mdriver d = fresh(&m, 1500, 0);
        blink_for(&d, 50);
        check_str(m.buffer, "", "involuntary blink adds no symbol");

        blink_for(&d, 200);
        check_str(m.buffer, ".", "short blink is a dot");

        blink_for(&d, 700);
        check_str(m.buffer, ".-", "long blink is a dash");

        blink_for(&d, 450);
        check_str(m.buffer, ".-", "ambiguous blink is discarded, not guessed");
    }

    group("letters and pauses");
    {
        mdriver d = fresh(&m, 1500, 0);
        blink_for(&d, 200); idle_for(&d, 300);
        blink_for(&d, 200); idle_for(&d, 300);
        blink_for(&d, 200);
        check_str(m.buffer, "...", "symbols accumulate while pauses are short");
        check_str(m.text, "", "nothing committed yet");

        idle_for(&d, 1600);
        check_int(d.last_letter, 'S', "pause commits the letter");
        check_str(m.text, "S", "the letter lands in the message");
        check_str(m.buffer, "", "buffer clears after commit");
    }

    group("spelling");
    {
        /* Y = -.--  E = .  S = ... */
        mdriver d = fresh(&m, 1500, 0);
        blink_for(&d, 700); blink_for(&d, 200); blink_for(&d, 700); blink_for(&d, 700);
        idle_for(&d, 1600);
        blink_for(&d, 200);
        idle_for(&d, 1600);
        blink_for(&d, 200); blink_for(&d, 200); blink_for(&d, 200);
        idle_for(&d, 1600);
        check_str(m.text, "YES", "spells YES");

        /* E = .  T = -  with a word gap between them */
        d = fresh(&m, 1500, 0);
        blink_for(&d, 200);
        idle_for(&d, 1600);
        check_str(m.text, "E", "first letter committed");
        idle_for(&d, 2000);   /* total idle now past the 3300ms word gap */
        check_int(d.spaces, 1, "a longer pause adds a space");
        check_str(m.text, "E ", "the space lands in the message");
        idle_for(&d, 4000);
        check_int(d.spaces, 1, "waiting even longer adds only one space");
        blink_for(&d, 700);
        idle_for(&d, 1600);
        check_str(m.text, "E T", "spelling resumes after the space");
    }

    group("undecodable patterns");
    {
        mdriver d = fresh(&m, 1500, 0);
        for (int i = 0; i < 6; i++) blink_for(&d, 200);  /* "......" is not a letter */
        idle_for(&d, 1600);
        check_int(d.undecodable, 1, "an undecodable pattern is reported");
        check_str(m.text, "", "an undecodable pattern adds nothing");
        check_str(m.buffer, "", "and clears the buffer to recover");
    }

    group("caregiver controls");
    {
        mdriver d = fresh(&m, 1500, 0);
        blink_for(&d, 200); idle_for(&d, 1600);          /* E */
        blink_for(&d, 700); idle_for(&d, 1600);          /* T */
        check_str(m.text, "ET", "spelled ET");

        oc_morse_undo(&m);
        check_str(m.text, "E", "undo removes the last letter");

        blink_for(&d, 200); blink_for(&d, 200);
        check_str(m.buffer, "..", "two dots pending");
        oc_morse_undo(&m);
        check_str(m.buffer, ".", "undo trims the in-progress symbols first");
        check_str(m.text, "E", "and leaves committed text alone");

        oc_morse_space(&m);
        check_str(m.text, "E ", "caregiver can insert a space");
        oc_morse_space(&m);
        check_str(m.text, "E ", "and cannot double it");

        oc_morse_clear(&m);
        check_str(m.text, "", "clear empties everything");
        check_str(m.buffer, "", "including the buffer");
    }

    group("timing dial");
    {
        /* A slow patient set to a 3000ms gap must not be cut off at the default. */
        mdriver d = fresh(&m, 3000, 0);
        check_int(oc_morse_word_gap_ms(&m), 6600, "the word gap is derived from the letter gap");
        blink_for(&d, 200);
        idle_for(&d, 1600);
        check_str(m.text, "", "a slow patient is not cut off at the default gap");
        idle_for(&d, 1600);
        check_str(m.text, "E", "and their letter commits at their own gap");
    }

    group("playback lock");
    {
        mdriver d = fresh(&m, 1500, 0);
        blink_for(&d, 200); idle_for(&d, 1600);
        m.input_locked = true;
        blink_for(&d, 200); blink_for(&d, 700);
        check_str(m.buffer, "", "blinks during playback are ignored");
        m.input_locked = false;
        blink_for(&d, 200);
        check_str(m.buffer, ".", "input resumes after playback");
    }

    group("autosend");
    {
        mdriver d = fresh(&m, 1500, 8);
        blink_for(&d, 200); idle_for(&d, 1600);
        check_str(m.text, "E", "message ready");
        idle_for(&d, 4000);
        check_int(d.autosends, 0, "autosend does not fire early");
        idle_for(&d, 6000);
        check_int(d.autosends, 1, "autosend fires after the still period");

        d = fresh(&m, 1500, 8);
        blink_for(&d, 200); idle_for(&d, 1600);
        idle_for(&d, 4000);
        blink_for(&d, 200);                 /* any blink cancels the countdown */
        idle_for(&d, 5000);
        check_int(d.autosends, 0, "a blink cancels the autosend countdown");
    }

    return oc_report("test_morse");
}
