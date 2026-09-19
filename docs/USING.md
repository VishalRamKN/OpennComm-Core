<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Using OpennComm

How the application behaves once it is running, and why it behaves that way.
For installing and building it, see the [README](../README.md).

## The window

It opens full screen, because the patient reads the four answers from a bed and
every unused pixel is one the answers could have been printed in. **F11**
toggles, **Escape** returns to a window, and `./run.sh --windowed` starts in
one.

## Who it is speaking for

On the first run it asks who it is speaking for — a name, and optionally an
age, a condition and anything that comes up daily. Every field is optional and
all of it stays in `~/.config/OpennComm/`; it is only ever read by the model
running on this machine. Reopen it any time from **Settings**.

Those details are worth filling in. Told that a patient is ventilated and has
constant right-shoulder pain, the model offered *"My right shoulder hurts"* and
*"I need more pain medication"* to questions that otherwise drew generic
answers. It costs roughly 0.8 s per question.

## Asking a question

Type a question, or press **Ask aloud** and say it — one press, then talk; it
stops listening when you stop talking, and asks the question itself.

## Answering it

Then answer it with your eyes:

  - **Blink mode** (default) — a short blink moves the highlight, a long blink
    (hold about half a second) speaks the highlighted answer. A white fill
    creeping across the button means keep holding and it will be chosen.
    Optionally the highlight can move by itself every few seconds
    (**Settings → Auto-scan**), so choosing costs one long blink instead of up
    to four short ones. Off by default: a moving target is harder to hit, and
    this is for patients where every blink is expensive. It never moves the
    highlight out from under a blink already being held.
  - **Head mode** — point your nose at one of the four options, then blink. A
    ring shows where the nose is aiming, so a highlight flicking between two
    answers reads as "you are on the line" rather than as broken tracking.
  - **Morse mode** — spell anything at all: short blink is a dot, long blink is
    a dash, a pause finishes a letter and a longer pause adds a space. The
    dots and dashes are drawn, large, as they are entered, with the letter they
    currently spell underneath and a hollow shape for the blink still being
    held. The reference chart stays on screen and dims every letter the symbols
    so far can no longer reach. Undo, Space, Clear and Speak are caregiver
    buttons — the patient only ever performs the same two blinks. It can also
    speak by itself after a few seconds of stillness (**Settings → Morse speaks
    by itself**), for when nobody is watching the screen; the countdown is
    visible for its whole length and any blink cancels it.

## While the model writes

While the model writes, the four buttons show that they are waiting rather than
filling with something else first. They used to show the built-in phrasebook
immediately and swap it for the model's answers a few seconds later, which
meant the options could change under a patient part-way through choosing one.
The phrasebook is still there — it is what appears if the model fails, is not
installed, or takes longer than twelve seconds.

## Setup screens

Two setup screens, both in **Settings**, both run once per patient and
remembered between runs:

  - **Head setup** teaches head mode where the corners of the screen are —
    five dots, about forty seconds. Head mode works without it, on a fallback.
  - **Blink setup** measures this patient's own short and long blinks — four of
    each, under a minute. The blink-speed presets are guesses, and people
    differ by more than a factor of two; if selections are being missed or
    triggered early, run this rather than guessing at the preset. If the two
    kinds of blink turn out too similar to tell apart, it says so and refuses
    rather than inventing a threshold.

## History

Every answer the patient gives is kept. **Settings → History** shows what has
been said, grouped by day and newest first, with a search box — so whoever
comes on shift can find out whether she has said she is in pain today without
asking her to spend four more words on it. It can be saved as a text file for
a handover note, and any of it can be deleted, one answer or all of it.

That history is the most sensitive thing the program holds: it is the person's
own words. It lives in `~/.local/share/openncomm/history.db`, it is never sent
anywhere, and **Delete everything** rewrites the file rather than just dropping
the rows, so the words are actually gone.

## What a mouse cannot do

Nothing a mouse does can speak an answer. The four buttons are display only —
only a blink, a head movement or morse commits one. An answer spoken in the
patient's voice should have been chosen by the patient, and a knock against a
touchscreen at a bedside should not be able to say anything at all.

## Settings

**Settings** holds the patient's details, the voice, both setup screens, the
camera preview, and two dials. **Blink
speed** sets how long a long blink must be held — only used when blink setup
has not been run. **Morse letter gap** sets how long a pause finishes a letter,
and it is the single setting that decides whether morse is usable for a given
patient.

Two voices ship, a woman's and a man's, and either can be chosen in Settings.
Which one a patient is given is not a cosmetic choice: it is the voice other
people will hear as theirs.

Settings also shows what the tracker sees: the camera preview, frame rate, eye
aspect ratio against its adaptive threshold, and nose position. If blinks are
not registering, watch the EAR value while you blink — it should fall below the
threshold shown beside it.

The way out is there too: **Exit full screen** and **Quit OpennComm**, next to
Done. Escape and F11 still work without opening anything.
