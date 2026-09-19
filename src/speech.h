/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Speaking the patient's chosen answer.
 *
 * Two engines. Piper is a neural voice and is what the patient should sound
 * like; espeak-ng is robotic but is installed everywhere, needs no model, and
 * never fails. Piper is used when its binary and a voice are present, and
 * espeak-ng otherwise -- because "the patient must always be able to say
 * something" outranks how pleasant it sounds.
 *
 * Two rules here are load-bearing and must survive any change of engine:
 *
 *  - finished() fires EXACTLY ONCE per speak(), on success, on error, and under
 *    a watchdog. The caller releases the patient's input on that signal, so a
 *    dropped callback strands them unable to answer again. A watchdog is not
 *    belt-and-braces; it is the only guarantee.
 *
 *  - A superseded utterance cannot fire finished() for the one that replaced
 *    it. speak() bumps a generation counter and late signals from an older
 *    generation are dropped.
 *
 * With Piper the signal is keyed on the PLAYER exiting, not the synthesiser:
 * synthesis finishes well before the sound does, and releasing input early
 * would let a blink during playback be read as the next answer.
 */
#ifndef OPENNCOMM_SPEECH_H
#define OPENNCOMM_SPEECH_H

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

class Speech : public QObject {
    Q_OBJECT
public:
    explicit Speech(QObject *parent = nullptr);

    /* Returns false when nothing can be spoken, having already emitted
     * finished() -- the caller must not be left waiting on a signal that will
     * never come just because the machine has no voice installed. */
    bool speak(const QString &text);

    /* Silence anything in progress. Every screen that stops talking must call
     * this: relying on the next speak() to cancel the last one only works if
     * there IS a next one. */
    void stop();

    bool available() const;
    QString engineName() const;

    /* Every Piper voice installed under models/voices, by id
     * ("en_US-amy-medium"). Empty when Piper is not usable at all. */
    QStringList voices() const;
    QString currentVoice() const;
    /* Switches voice by id. Returns false and keeps the current voice if that
     * one is not installed -- a settings file naming a deleted voice must not
     * leave the patient mute. */
    bool setVoice(const QString &id);

signals:
    void finished();

private:
    void settle(quint64 generation);
    bool loadVoice(const QString &id);
    void teardown();
    bool speakWithPiper(const QString &text, quint64 generation);
    bool speakWithEspeak(const QString &text, quint64 generation);

    QProcess *synth_ = nullptr;   /* piper, or espeak-ng */
    QProcess *player_ = nullptr;  /* paplay, only on the piper path */
    QTimer *watchdog_ = nullptr;

    quint64 generation_ = 0;
    quint64 settled_generation_ = 0;

    bool espeak_available_ = false;
    QString piper_binary_;
    QString piper_voice_;
    QString piper_lib_dir_;
    int piper_rate_ = 22050;
};

#endif
