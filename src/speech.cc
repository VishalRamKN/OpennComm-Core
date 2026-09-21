/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "speech.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "audio.h"
#include "paths.h"
#include <QTimer>

namespace {

constexpr int kEspeakWordsPerMinute = 145; /* slower than the 175 default: this
                                              is an answer a caregiver must
                                              catch first time */

/* Never trust a speech engine to report completion. The watchdog is what
 * guarantees the patient gets their input back. Generous enough not to cut a
 * long answer short, capped so a hung process cannot lock them out for good. */
int watchdogMsFor(const QString &text)
{
    return qMin(20000, 5000 + static_cast<int>(text.length()) * 180);
}

} // namespace

Speech::Speech(QObject *parent) : QObject(parent)
{
    espeak_available_ = !QStandardPaths::findExecutable(QStringLiteral("espeak-ng")).isEmpty();

    piper_binary_ = paths::piperBinary();

    if (!piper_binary_.isEmpty()) {
        piper_lib_dir_ = QFileInfo(piper_binary_).absolutePath();
        if (!PcmSink::available()) piper_binary_.clear(); /* nothing can play it */
    }

    /* Whatever was chosen last time, else the first voice installed. Falling
     * back rather than failing matters: a settings file can name a voice that
     * has since been deleted, and the patient must still be able to speak. */
    const QStringList installed = voices();
    const QString saved = QSettings().value(QStringLiteral("voice")).toString();
    if (!loadVoice(saved) && !installed.isEmpty()) loadVoice(installed.first());
    if (piper_voice_.isEmpty()) piper_binary_.clear();

    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, [this] { settle(generation_); });
}

bool Speech::available() const
{
    return !piper_binary_.isEmpty() || espeak_available_;
}

/* Scanned from disk rather than hard-coded, so dropping another voice into
 * models/voices is all it takes to offer it. */
QStringList Speech::voices() const
{
    if (piper_binary_.isEmpty() && piper_voice_.isEmpty()) return {};
    QStringList ids;
    for (const QString &dir : paths::modelsDirs()) {
        QDir voices(QDir(dir).filePath(QStringLiteral("voices")));
        const QStringList files =
            voices.entryList({ QStringLiteral("*.onnx") }, QDir::Files, QDir::Name);
        for (const QString &f : files) {
            const QString id = QFileInfo(f).completeBaseName();
            if (!ids.contains(id)) ids << id;
        }
    }
    return ids;
}

QString Speech::currentVoice() const
{
    return piper_voice_.isEmpty() ? QString()
                                  : QFileInfo(piper_voice_).completeBaseName();
}

bool Speech::loadVoice(const QString &id)
{
    if (id.trimmed().isEmpty()) return false;
    const QString path = paths::model(QStringLiteral("voices/") + id + QStringLiteral(".onnx"));
    if (!QFileInfo::exists(path)) return false;

    piper_voice_ = path;

    /* The sample rate belongs to the voice, not to Piper. Reading it from the
     * voice's own config means a different voice cannot be played at the wrong
     * speed -- which is what a shared default would eventually do. */
    piper_rate_ = 22050;
    QFile config(piper_voice_ + QStringLiteral(".json"));
    if (config.open(QIODevice::ReadOnly)) {
        const QJsonObject audio =
            QJsonDocument::fromJson(config.readAll()).object()
                .value(QStringLiteral("audio")).toObject();
        const int rate = audio.value(QStringLiteral("sample_rate")).toInt();
        if (rate > 0) piper_rate_ = rate;
    }
    return true;
}

bool Speech::setVoice(const QString &id)
{
    if (id == currentVoice()) return true;
    const QString previous = piper_voice_;
    if (!loadVoice(id)) return false;
    /* Mid-sentence a voice change would finish the utterance in the other
     * voice, so stop first. */
    if (previous != piper_voice_) stop();
    QSettings().setValue(QStringLiteral("voice"), id);
    return true;
}

QString Speech::engineName() const
{
    if (!piper_binary_.isEmpty())
        return QStringLiteral("Piper (%1)").arg(QFileInfo(piper_voice_).completeBaseName());
    if (espeak_available_) return QStringLiteral("espeak-ng");
    return QStringLiteral("none");
}

void Speech::settle(quint64 generation)
{
    /* A superseded utterance must not settle the one that replaced it, and one
     * utterance must not settle twice -- a process can deliver finished() and
     * errorOccurred() for the same failure. */
    if (generation != generation_) return;
    if (settled_generation_ == generation) return;
    settled_generation_ = generation;

    watchdog_->stop();
    emit finished();
}

void Speech::teardown()
{
    /* The player goes first. On Linux the synthesiser's stdout is wired
     * straight into it, and killing the far end of that pipe first leaves the
     * synthesiser writing into a closed descriptor. */
    if (player_) {
        player_->disconnect(this);
        player_->stop();
        player_->deleteLater();
        player_ = nullptr;
    }
    if (synth_) {
        synth_->disconnect(this);
        synth_->kill();
        synth_->deleteLater();
        synth_ = nullptr;
    }
}

bool Speech::speakWithPiper(const QString &text, quint64 generation)
{
    player_ = new PcmSink(this);
    synth_ = new QProcess(this);

#ifndef Q_OS_WIN
    /* Piper's own onnxruntime and espeak-ng sit beside its binary, and on Linux
     * the loader will not look there unless it is told to. On Windows it always
     * searches the directory the .exe was loaded from, so there is nothing to
     * set and setting it would only be a way to get it wrong. */
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LD_LIBRARY_PATH"),
               piper_lib_dir_ + QLatin1Char(':')
                   + env.value(QStringLiteral("LD_LIBRARY_PATH")));
    synth_->setProcessEnvironment(env);
#endif

    /* Wired up before either process starts: on Linux play() connects the two
     * standard streams together, which QProcess only allows beforehand. */
    if (!player_->play(synth_, piper_rate_)) { teardown(); return false; }

    /* The sound stopping is what ends the utterance, not the synthesis
     * finishing -- releasing input early would let a blink during playback be
     * read as the patient's next answer. */
    connect(player_, &PcmSink::finished, this, [this, generation] { settle(generation); });
    /* If Piper cannot start at all, nothing will ever be played and the sink
     * would wait on samples that are not coming. */
    connect(synth_, &QProcess::errorOccurred, this, [this, generation] {
        if (player_) player_->stop();
        settle(generation);
    });

    synth_->start(piper_binary_, { QStringLiteral("--model"), piper_voice_,
                                   QStringLiteral("--output_raw") });

    if (!synth_->waitForStarted(3000)) { teardown(); return false; }

    synth_->write(text.toUtf8());
    synth_->write("\n");
    synth_->closeWriteChannel();
    return true;
}

bool Speech::speakWithEspeak(const QString &text, quint64 generation)
{
    if (!espeak_available_) return false;

    synth_ = new QProcess(this);
    connect(synth_, &QProcess::finished, this, [this, generation] { settle(generation); });
    connect(synth_, &QProcess::errorOccurred, this, [this, generation] { settle(generation); });
    synth_->start(QStringLiteral("espeak-ng"),
                  { QStringLiteral("-s"), QString::number(kEspeakWordsPerMinute), text });
    return true;
}

bool Speech::speak(const QString &text)
{
    const quint64 gen = ++generation_;

    if (text.trimmed().isEmpty() || !available()) {
        /* Settle immediately rather than returning silently: the caller is
         * holding the patient's input until finished() arrives. */
        settle(gen);
        return false;
    }

    teardown();

    bool started = false;
    if (!piper_binary_.isEmpty()) started = speakWithPiper(text, gen);
    /* Falling back rather than failing: a Piper that will not start must not
     * cost the patient their answer. */
    if (!started) started = speakWithEspeak(text, gen);

    if (!started) { settle(gen); return false; }

    watchdog_->start(watchdogMsFor(text));
    return true;
}

void Speech::stop()
{
    generation_++;
    watchdog_->stop();
    teardown();
}
