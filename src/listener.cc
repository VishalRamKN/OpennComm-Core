/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "listener.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QStandardPaths>
#include <QThread>

#include <cmath>
#include <vector>

#include "whisper.h"

namespace {

constexpr int kSampleRate = 16000; /* whisper accepts nothing else */

/* ffmpeg rather than parecord, for a measured reason: parecord ignores
 * --rate and hands back the device's native rate (48kHz here), which whisper
 * would transcribe as gibberish at three times speed. ffmpeg resamples
 * correctly and is present on essentially every desktop Linux. */
QStringList captureArgs()
{
    return { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("pulse"),
             QStringLiteral("-i"), QStringLiteral("default"),
             QStringLiteral("-ar"), QString::number(kSampleRate),
             QStringLiteral("-ac"), QStringLiteral("1"),
             QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-") };
}

/* whisper's own front-ends discard anything under a second, and a clip that
 * short is almost always a mis-press rather than a question. */
constexpr int kMinSamples = kSampleRate;

/* Whisper hallucinates confidently on silence, producing plausible sentences
 * from nothing. A question spoken at a bedside is quiet, so the floor is low --
 * this is meant to catch a muted or unplugged microphone, not a soft voice. */
constexpr float kSilenceFloor = 0.004f;

/* ---- knowing when the question has ended --------------------------------- */

/* The first chunks are treated as room tone and the speech threshold is set
 * above it. A fixed threshold fails in both directions -- a ward with a
 * ventilator running never falls below it, and a quiet room with a soft-spoken
 * caregiver never rises above it. */
constexpr int kNoiseProbeMs = 400;
constexpr float kMinSpeechRms = 0.012f;   /* floor under the adaptive threshold */

/* Long enough not to trip on the pause inside "Are you... in pain?", short
 * enough that the caregiver is not left wondering whether it heard them. */
constexpr int kTrailingSilenceMs = 900;

/* Once speech has started, staying in it is judged against a LOWER bar than
 * entering it was. Measured: with a single threshold the recording stopped
 * 2.3 s into a 3 s question, because the quiet consonants between words fell
 * back under the onset threshold for long enough to look like the end. Onset
 * should be decisive; continuation should be forgiving.
 *
 * Both bars are expressed against the measured noise floor rather than as a
 * fraction of each other. Making the hold bar a fraction of the onset bar put
 * it at 1.6x room tone, which room tone itself crosses as it fluctuates -- the
 * recording then never ended and whisper hallucinated a paragraph out of nine
 * seconds of nothing. The hold bar has to stay clear of the noise, not merely
 * below the onset. */
constexpr float kNoiseToOnset = 3.5f;
constexpr float kNoiseToHold  = 2.0f;
constexpr float kMinHoldRms   = 0.008f;

/* No question ends within this long of starting. Guards the same failure from
 * the other side: the first inter-word gap must not be mistaken for the end of
 * a sentence that has barely begun. */
constexpr int kMinSpeechMs = 1400;

/* Nothing but room tone for this long means the microphone is muted, the wrong
 * device is selected, or the press was an accident. Saying so beats recording
 * silence until the hard cap. */
constexpr int kNoSpeechGiveUpMs = 6000;

/* No question is this long. Catches a capture that never delivers silence --
 * a television in the room, a stuck process -- so the patient is never left
 * waiting on a recording that will not end. */
constexpr int kMaxRecordingMs = 25000;

/* Root-mean-square of a block of signed 16-bit samples. */
float chunkRms(const char *data, qsizetype bytes)
{
    const auto n = bytes / 2;
    if (n <= 0) return 0.0f;
    const auto *raw = reinterpret_cast<const qint16 *>(data);
    double energy = 0.0;
    for (qsizetype i = 0; i < n; i++) {
        const double s = static_cast<double>(raw[i]) / 32768.0;
        energy += s * s;
    }
    return static_cast<float>(std::sqrt(energy / static_cast<double>(n)));
}

QString cleanTranscript(QString text)
{
    text = text.trimmed();
    /* Whisper brackets non-speech events like [BLANK_AUDIO], (wind blowing) and
     * ♪music♪. None of them are a question. */
    if (text.startsWith(QLatin1Char('[')) || text.startsWith(QLatin1Char('(')))
        return QString();
    return text;
}

/* Drop the silence at the end of the recording.
 *
 * Whisper invents sentences out of silence -- a clean "Are you in pain right
 * now?" came back with "I am in pain right now but I can't do it." appended,
 * generated entirely from the second of room tone after the question. The
 * detector cannot avoid leaving that tail: it has to hear the silence before
 * it can know the question ended.
 *
 * Measured from the audio rather than from the clock, because the capture
 * process starts a little after the timer does and the two are not aligned
 * closely enough to cut on. */
void trimTrailingSilence(QByteArray &pcm, float bar)
{
    constexpr int kWindowMs = 50;
    constexpr int kKeepMs = 250;     /* breath and consonant tails */
    const qsizetype window = kSampleRate * kWindowMs / 1000 * 2;
    if (pcm.size() < window * 2) return;

    qsizetype end = pcm.size();
    while (end >= window) {
        if (chunkRms(pcm.constData() + end - window, window) >= bar) break;
        end -= window;
    }
    if (end < window) return;        /* all silence; leave it for the RMS check */

    end += kSampleRate * kKeepMs / 1000 * 2;
    if (end < pcm.size()) pcm.truncate(end);
}

} // namespace

struct Listener::Impl {
    QString model_path;
    whisper_context *ctx = nullptr;
    QProcess *capture = nullptr;
    QByteArray pcm;
    bool ready = false;

    /* End-of-speech detection. */
    QTimer *watch = nullptr;
    QElapsedTimer since_start;
    qint64 last_loud_ms = 0;
    qint64 speech_began_ms = 0;
    bool heard_speech = false;
    double noise_sum = 0.0;
    int noise_chunks = 0;
    float threshold = kMinSpeechRms;
    float hold_threshold = kMinHoldRms;
};



Listener::Listener(QString model_path, QObject *parent)
    : QObject(parent), d(new Impl)
{
    d->model_path = std::move(model_path);
}

Listener::~Listener()
{
    if (d->capture) { d->capture->kill(); d->capture->waitForFinished(500); }
    if (d->ctx) whisper_free(d->ctx);
    delete d;
}

void Listener::load()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        emit failed(QStringLiteral("ffmpeg is not installed — type the question instead."));
        return;
    }
    if (!QFileInfo::exists(d->model_path)) {
        emit failed(QStringLiteral("No speech model installed — type the question instead."));
        return;
    }

    whisper_log_set([](ggml_log_level, const char *, void *) {}, nullptr);

    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    d->ctx = whisper_init_from_file_with_params(d->model_path.toUtf8().constData(), cp);
    if (!d->ctx) {
        emit failed(QStringLiteral("The speech model could not be loaded — type the question instead."));
        return;
    }

    d->ready = true;
    emit ready();
}

void Listener::startRecording()
{
    if (!d->ready) return;
    if (d->capture) return; /* already recording */

    d->pcm.clear();
    d->heard_speech = false;
    d->noise_sum = 0.0;
    d->noise_chunks = 0;
    d->threshold = kMinSpeechRms;
    d->hold_threshold = kMinHoldRms;
    d->since_start.start();
    d->last_loud_ms = 0;
    d->speech_began_ms = 0;

    d->capture = new QProcess(this);
    connect(d->capture, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray chunk = d->capture->readAllStandardOutput();
        d->pcm.append(chunk);

        const float rms = chunkRms(chunk.constData(), chunk.size());
        emit level(rms);

        const qint64 elapsed = d->since_start.elapsed();

        /* Room tone first, threshold second. */
        if (elapsed < kNoiseProbeMs) {
            d->noise_sum += rms;
            d->noise_chunks++;
            return;
        }
        if (d->noise_chunks > 0) {
            const float noise = static_cast<float>(d->noise_sum / d->noise_chunks);
            d->threshold = qMax(kMinSpeechRms, noise * kNoiseToOnset);
            d->hold_threshold = qMax(kMinHoldRms, noise * kNoiseToHold);
            d->noise_chunks = 0;
        }

        const float bar = d->heard_speech ? d->hold_threshold : d->threshold;
        if (rms >= bar) {
            if (!d->heard_speech) {
                d->heard_speech = true;
                d->speech_began_ms = elapsed;
            }
            d->last_loud_ms = elapsed;
        }
    });

    /* A timer rather than deciding inside readyRead: when the caregiver stops
     * talking the chunks keep arriving, but if capture stalls entirely they do
     * not, and the end of the question would never be noticed. */
    if (!d->watch) {
        d->watch = new QTimer(this);
        d->watch->setInterval(100);
        connect(d->watch, &QTimer::timeout, this, [this] {
            if (!d->capture) return;
            const qint64 elapsed = d->since_start.elapsed();

            if (d->heard_speech
                && elapsed - d->speech_began_ms >= kMinSpeechMs
                && elapsed - d->last_loud_ms >= kTrailingSilenceMs) {
                stopAndTranscribe();
                return;
            }
            if (!d->heard_speech && elapsed >= kNoSpeechGiveUpMs) {
                discard();
                emit failed(QStringLiteral(
                    "I heard nothing \u2014 check the microphone, or type the question."));
                return;
            }
            if (elapsed >= kMaxRecordingMs) stopAndTranscribe();
        });
    }
    d->watch->start();

    d->capture->start(QStringLiteral("ffmpeg"), captureArgs());
}

/* Stop capturing and throw the audio away. For the paths that have already
 * decided there is no question in it. */
void Listener::discard()
{
    if (d->watch) d->watch->stop();
    if (!d->capture) return;
    d->capture->terminate();
    if (!d->capture->waitForFinished(1000)) d->capture->kill();
    d->capture->deleteLater();
    d->capture = nullptr;
    d->pcm.clear();
    emit listeningEnded();
}

void Listener::stopAndTranscribe()
{
    if (!d->ready) return;
    if (d->watch) d->watch->stop();
    if (!d->capture) { emit failed(QStringLiteral("Nothing was recorded.")); return; }

    /* terminate, not kill: ffmpeg flushes its remaining buffer on SIGTERM, and
     * the tail of that buffer is usually the end of the question. */
    d->capture->terminate();
    if (!d->capture->waitForFinished(1500)) d->capture->kill();
    d->pcm.append(d->capture->readAllStandardOutput());
    d->capture->deleteLater();
    d->capture = nullptr;
    emit listeningEnded();

    if (d->heard_speech) trimTrailingSilence(d->pcm, d->hold_threshold);

    const int n = static_cast<int>(d->pcm.size() / 2);
    if (n < kMinSamples) {
        emit failed(QStringLiteral("That was too short — press and ask the question."));
        return;
    }

    std::vector<float> audio(n);
    const auto *raw = reinterpret_cast<const qint16 *>(d->pcm.constData());
    double energy = 0.0;
    for (int i = 0; i < n; i++) {
        audio[i] = static_cast<float>(raw[i]) / 32768.0f;
        energy += static_cast<double>(audio[i]) * audio[i];
    }
    const float rms = static_cast<float>(std::sqrt(energy / n));
    if (rms < kSilenceFloor) {
        emit failed(QStringLiteral("I heard nothing — check the microphone."));
        return;
    }

    QElapsedTimer clock;
    clock.start();

    whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wp.print_progress = false;
    wp.print_realtime = false;
    wp.print_timestamps = false;
    wp.translate = false;
    wp.language = "en";
    wp.n_threads = qBound(2, QThread::idealThreadCount() - 2, 8);
    /* One question, spoken once. Splitting it into timestamped segments would
     * only give us pieces to reassemble. */
    wp.single_segment = false;
    wp.no_context = true;

    if (whisper_full(d->ctx, wp, audio.data(), n) != 0) {
        emit failed(QStringLiteral("The question could not be transcribed."));
        return;
    }

    QString text;
    const int segments = whisper_full_n_segments(d->ctx);
    for (int i = 0; i < segments; i++)
        text += QString::fromUtf8(whisper_full_get_segment_text(d->ctx, i));

    text = cleanTranscript(text);
    if (text.isEmpty()) {
        emit failed(QStringLiteral("I did not catch a question — please try again."));
        return;
    }

    emit transcribed(text, static_cast<double>(clock.elapsed()));
}
