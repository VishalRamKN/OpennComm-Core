/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* The two places sound crosses the edge of this program: a microphone coming
 * in, and Piper's synthesis going out.
 *
 * Everywhere else the application is the same on Linux and on Windows. These
 * two are not, and the difference is not cosmetic:
 *
 *   Linux has no audio API that can be relied on -- ALSA, PulseAudio and
 *   PipeWire are all present on some machines and absent on others -- so the
 *   working approach there is to shell out to programs that already know which
 *   one is running: ffmpeg for capture, paplay or pw-play for playback. That
 *   code is tuned and tested against real hardware and is left exactly as it
 *   was.
 *
 *   Windows has one audio API that is always there, and no ffmpeg or paplay to
 *   shell out to. Qt Multimedia speaks it directly, including the resampling
 *   from whatever rate the device is running at to the 16 kHz whisper insists
 *   on, so there is no subprocess at all.
 *
 * Both are hidden behind these two classes so that listener.cc and speech.cc --
 * which hold the logic about what a question is and when an answer has finished
 * being spoken -- contain no #ifdef and did not have to be rewritten for a
 * second platform. That logic is the part that decides what a patient is
 * understood to have said, and it must not fork per operating system.
 */
#ifndef OPENNCOMM_AUDIO_H
#define OPENNCOMM_AUDIO_H

#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;

/* Microphone capture as signed 16-bit mono at a fixed sample rate.
 *
 * Emits chunk() as audio arrives. Chunks are a few tens of milliseconds each:
 * listener.cc measures the RMS of every chunk to decide where speech starts and
 * stops, so they have to be short enough to resolve a pause between words and
 * long enough for the RMS to mean something.
 */
class MicSource : public QObject {
    Q_OBJECT
public:
    explicit MicSource(int sample_rate, QObject *parent = nullptr);
    ~MicSource() override;

    /* Empty when capture is possible; otherwise a sentence that can be shown to
     * a caregiver as-is. Checked before recording rather than after, because
     * "the microphone produced nothing" and "there is no way to record at all"
     * need different things said about them. */
    static QString unavailableReason();

    /* What is doing the recording, for the self-check to print. Names the
     * thing an operator could go and look at when it is not working. */
    static QString backendName();

    bool start();

    /* Stop capturing, returning whatever audio was still held and has not been
     * emitted through chunk().
     *
     * That remainder is not a detail. On Linux the capture is an ffmpeg process
     * which flushes its buffer on shutdown, and the tail of that buffer is
     * usually the last word of the question -- dropping it truncates what
     * whisper is asked to transcribe. Returned rather than emitted so that
     * stopping never re-enters the caller's chunk handler.
     *
     * Safe to call when not started. */
    QByteArray stop();

    bool isActive() const;

signals:
    void chunk(const QByteArray &pcm);

private:
    struct Impl;
    Impl *d;
};

/* Playback of raw PCM produced on a process's standard output.
 *
 * Written around Piper rather than around audio in general: it takes the
 * already-started QProcess whose stdout carries the samples, so that on Linux
 * the audio can go straight from one process to the next without passing
 * through this one at all.
 */
class PcmSink : public QObject {
    Q_OBJECT
public:
    explicit PcmSink(QObject *parent = nullptr);
    ~PcmSink() override;

    static bool available();

    /* Play `source`'s standard output at `sample_rate`, mono signed-16.
     * `source` must not have been started yet: on Linux it is connected to the
     * player's stdin before either runs. Returns false if playback could not be
     * set up, in which case the caller still owns `source`. */
    bool play(QProcess *source, int sample_rate);

    /* Stop immediately, dropping anything not yet heard. */
    void stop();

signals:
    /* The sound has stopped -- not merely that synthesis finished. The
     * difference matters: releasing the patient's input while their answer is
     * still being spoken lets a blink during playback be read as their next
     * answer. */
    void finished();

private:
    struct Impl;
    Impl *d;
};

#endif
