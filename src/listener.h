/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Hearing the caregiver's question.
 *
 * Click to start, and it stops by itself when the question ends. Not an
 * always-on microphone: recording begins only on a deliberate press, because a
 * microphone that is always listening eventually transcribes a passing
 * conversation as a question and puts words in front of the patient that
 * nobody asked.
 *
 * Click-to-start rather than hold-to-talk. Holding means the caregiver has to
 * keep a finger down for the whole question, and the common failure is
 * releasing a moment early and cutting off the last word -- which then has to
 * be asked again, with the patient waiting through both attempts. Ending on
 * silence costs a three-quarter-second pause and removes that failure
 * entirely.
 *
 * Three limits bound it, so a stuck recording cannot hold the patient up:
 * trailing silence ends the question, a few seconds of nothing at all gives up
 * and says so, and a hard cap catches everything else.
 *
 * Audio is captured through ffmpeg and transcribed by whisper.cpp in-process.
 * Nothing is uploaded. That matters beyond principle: the usual hosted speech
 * APIs stream the caregiver's voice to a third party, and a bedside tool has
 * no business doing that.
 */
#ifndef OPENNCOMM_LISTENER_H
#define OPENNCOMM_LISTENER_H

#include <QObject>
#include <QString>

class Listener : public QObject {
    Q_OBJECT
public:
    explicit Listener(QString model_path, QObject *parent = nullptr);
    ~Listener() override;

public slots:
    void load();
    void startRecording();
    /* Stops capture and transcribes. Always ends in either transcribed() or
     * failed(), so the caller can re-enable its button unconditionally. */
    void stopAndTranscribe();

signals:
    void ready();
    void failed(const QString &message);
    void transcribed(const QString &text, double elapsed_ms);

    /* Capture has stopped and transcription is under way. Separate from
     * transcribed() because whisper takes a moment and the caregiver should
     * see that their question landed before the text appears. */
    void listeningEnded();

    /* Loudness of the last chunk, 0..1, roughly twenty times a second. For a
     * level meter: without one, a muted microphone looks exactly like a
     * working one until the transcription comes back empty. */
    void level(float rms);

private:
    /* Stop capture and drop what was recorded. */
    void discard();

    struct Impl;
    Impl *d;
};

#endif
