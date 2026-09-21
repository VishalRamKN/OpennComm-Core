/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "audio.h"

#include <QProcess>

#ifdef Q_OS_WIN
#  include <QAudioDevice>
#  include <QAudioFormat>
#  include <QAudioSink>
#  include <QAudioSource>
#  include <QMediaDevices>
#  include <QTimer>
#else
#  include <QStandardPaths>
#endif

/* ===========================================================================
 * Capture
 * ======================================================================== */

#ifdef Q_OS_WIN

namespace {
/* Qt hands over whatever the device produced since the last wakeup, which on
 * Windows can be a couple of milliseconds at a time. listener.cc reads the RMS
 * of each chunk to find the edges of the question, and the RMS of five
 * milliseconds of audio is mostly noise about noise -- a single consonant can
 * swing it far enough to look like the start of speech.
 *
 * So capture is coalesced to roughly the size ffmpeg delivers on Linux before
 * being handed on. This is what lets the end-of-speech thresholds in
 * listener.cc, which were measured against real recordings, mean the same thing
 * on both platforms rather than needing a second set of numbers for Windows. */
constexpr int kChunkMs = 64;
} // namespace

struct MicSource::Impl {
    int rate;
    QAudioSource *source = nullptr;
    QIODevice *io = nullptr;
    QByteArray pending;
    qsizetype chunk_bytes = 0;
};

QString MicSource::unavailableReason()
{
    if (QMediaDevices::defaultAudioInput().isNull())
        return QStringLiteral("No microphone was found — type the question instead.");
    return QString();
}

QString MicSource::backendName()
{
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    return device.isNull() ? QStringLiteral("no input device") : device.description();
}

MicSource::MicSource(int sample_rate, QObject *parent) : QObject(parent), d(new Impl)
{
    d->rate = sample_rate;
    d->chunk_bytes = static_cast<qsizetype>(sample_rate) * kChunkMs / 1000 * 2;
}

MicSource::~MicSource()
{
    stop();
    delete d;
}

bool MicSource::start()
{
    if (d->source) return true;

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) return false;

    QAudioFormat fmt;
    fmt.setSampleRate(d->rate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    /* Not checked against isFormatSupported(). A shared-mode Windows device
     * reports only the rate the mixer happens to be running at -- 48 kHz on
     * essentially every machine -- and whisper accepts 16 kHz and nothing else.
     * Qt's Windows backend resamples to the requested format, which is exactly
     * the job ffmpeg does on Linux, and refusing here on the strength of
     * isFormatSupported() would turn a working capture into "no microphone". */
    d->source = new QAudioSource(device, fmt, this);
    d->pending.clear();

    d->io = d->source->start();
    if (!d->io) {
        delete d->source;
        d->source = nullptr;
        return false;
    }

    connect(d->io, &QIODevice::readyRead, this, [this] {
        d->pending.append(d->io->readAll());
        while (d->pending.size() >= d->chunk_bytes) {
            emit chunk(d->pending.left(d->chunk_bytes));
            d->pending.remove(0, d->chunk_bytes);
        }
    });
    return true;
}

QByteArray MicSource::stop()
{
    if (!d->source) return {};
    if (d->io) d->io->disconnect(this);

    /* Whatever the device produced since the last wakeup, plus the part-chunk
     * being accumulated. Both are real audio and the second of them is the end
     * of the question more often than not, because the caregiver stops talking
     * and the recording stops a fraction of a second later. */
    QByteArray tail = d->pending;
    if (d->io) tail.append(d->io->readAll());

    d->source->stop();
    d->source->deleteLater();
    d->source = nullptr;
    d->io = nullptr;
    d->pending.clear();
    return tail;
}

bool MicSource::isActive() const { return d->source != nullptr; }

#else /* Unix */

namespace {
/* ffmpeg rather than parecord, for a measured reason: parecord ignores --rate
 * and hands back the device's native rate (48 kHz here), which whisper would
 * transcribe as gibberish at three times speed. ffmpeg resamples correctly and
 * is present on essentially every desktop Linux. */
QStringList captureArgs(int rate)
{
    return { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("pulse"),
             QStringLiteral("-i"), QStringLiteral("default"),
             QStringLiteral("-ar"), QString::number(rate),
             QStringLiteral("-ac"), QStringLiteral("1"),
             QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-") };
}
} // namespace

struct MicSource::Impl {
    int rate;
    QProcess *capture = nullptr;
};

QString MicSource::unavailableReason()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        return QStringLiteral("ffmpeg is not installed — type the question instead.");
    return QString();
}

QString MicSource::backendName() { return QStringLiteral("ffmpeg"); }

MicSource::MicSource(int sample_rate, QObject *parent) : QObject(parent), d(new Impl)
{
    d->rate = sample_rate;
}

MicSource::~MicSource()
{
    if (d->capture) { d->capture->kill(); d->capture->waitForFinished(500); }
    delete d;
}

bool MicSource::start()
{
    if (d->capture) return true;
    d->capture = new QProcess(this);
    connect(d->capture, &QProcess::readyReadStandardOutput, this,
            [this] { emit chunk(d->capture->readAllStandardOutput()); });
    d->capture->start(QStringLiteral("ffmpeg"), captureArgs(d->rate));
    return true;
}

QByteArray MicSource::stop()
{
    if (!d->capture) return {};
    d->capture->disconnect(this);

    /* terminate, not kill: ffmpeg flushes its remaining buffer on SIGTERM, and
     * the tail of that buffer is usually the end of the question. */
    d->capture->terminate();
    if (!d->capture->waitForFinished(1500)) d->capture->kill();
    const QByteArray tail = d->capture->readAllStandardOutput();

    d->capture->deleteLater();
    d->capture = nullptr;
    return tail;
}

bool MicSource::isActive() const { return d->capture != nullptr; }

#endif

/* ===========================================================================
 * Playback
 * ======================================================================== */

#ifdef Q_OS_WIN

namespace {
/* Deep enough that a pause in synthesis does not become a gap in the answer --
 * Piper produces a sentence in bursts, not at a steady rate -- and shallow
 * enough that stop() takes effect while the patient is still in the moment they
 * pressed it. */
constexpr int kSinkBufferMs = 400;
constexpr int kPumpMs = 20;
} // namespace

struct PcmSink::Impl {
    QProcess *source = nullptr;
    QAudioSink *sink = nullptr;
    QIODevice *dev = nullptr;
    QTimer *pump = nullptr;
    QByteArray pending;
    bool source_done = false;
    bool wrote_anything = false;
};

bool PcmSink::available()
{
    return !QMediaDevices::defaultAudioOutput().isNull();
}

PcmSink::PcmSink(QObject *parent) : QObject(parent), d(new Impl) {}

PcmSink::~PcmSink()
{
    stop();
    delete d;
}

bool PcmSink::play(QProcess *source, int sample_rate)
{
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) return false;

    QAudioFormat fmt;
    fmt.setSampleRate(sample_rate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    d->source = source;
    d->sink = new QAudioSink(device, fmt, this);
    d->sink->setBufferSize(sample_rate * 2 * kSinkBufferMs / 1000);

    d->dev = d->sink->start();
    if (!d->dev) {
        delete d->sink;
        d->sink = nullptr;
        d->source = nullptr;
        return false;
    }

    /* Guarded on d->source rather than trusting the lifetime: stop() clears it
     * while this connection is still live, and a queued readyRead arriving in
     * between would dereference null. */
    connect(source, &QProcess::readyReadStandardOutput, this, [this] {
        if (!d->source) return;
        d->pending.append(d->source->readAllStandardOutput());
    });
    /* finished() rather than readChannelFinished(): the samples still buffered
     * here have to be played out after Piper has exited. */
    connect(source, &QProcess::finished, this, [this] {
        if (d->source) d->pending.append(d->source->readAllStandardOutput());
        d->source_done = true;
    });

    /* A timer rather than writing from readyRead: the sink accepts only what it
     * has room for, so there is always a remainder to push later, and the
     * moment the last of it has been heard arrives with no signal of its own. */
    d->pump = new QTimer(this);
    d->pump->setInterval(kPumpMs);
    connect(d->pump, &QTimer::timeout, this, [this] {
        if (!d->sink || !d->dev) return;

        const qsizetype room = d->sink->bytesFree();
        if (room > 0 && !d->pending.isEmpty()) {
            const qsizetype n = qMin(room, d->pending.size());
            const qint64 written = d->dev->write(d->pending.constData(), n);
            if (written > 0) {
                d->pending.remove(0, written);
                d->wrote_anything = true;
            }
        }

        /* Idle means the sink has played everything handed to it. On its own
         * that is also true before the first sample arrives and during any
         * underrun mid-sentence, so it only ends the utterance once Piper has
         * exited and nothing is left waiting to be written. */
        if (d->source_done && d->pending.isEmpty() && d->wrote_anything
            && d->sink->state() == QAudio::IdleState) {
            d->pump->stop();
            emit finished();
        }
    });
    d->pump->start();

    return true;
}

void PcmSink::stop()
{
    if (d->pump) { d->pump->stop(); d->pump->deleteLater(); d->pump = nullptr; }
    if (d->sink) {
        d->sink->stop();
        d->sink->deleteLater();
        d->sink = nullptr;
    }
    d->dev = nullptr;
    d->source = nullptr;
    d->pending.clear();
    d->source_done = false;
    d->wrote_anything = false;
}

#else /* Unix */

namespace {
/* aplay is not usable here: it routes through an ALSA plugin that is not
 * installed on a stock PipeWire desktop and fails with "No such device".
 * paplay talks to the sound server directly and honours the raw format flags. */
QString findPlayer()
{
    for (const char *name : { "paplay", "pw-play" }) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!path.isEmpty()) return path;
    }
    return QString();
}
} // namespace

struct PcmSink::Impl {
    QProcess *player = nullptr;
};

bool PcmSink::available() { return !findPlayer().isEmpty(); }

PcmSink::PcmSink(QObject *parent) : QObject(parent), d(new Impl) {}

PcmSink::~PcmSink()
{
    stop();
    delete d;
}

bool PcmSink::play(QProcess *source, int sample_rate)
{
    const QString player = findPlayer();
    if (player.isEmpty()) return false;

    d->player = new QProcess(this);

    /* Piper streams raw PCM on stdout straight into the player, so nothing
     * touches the disk, nothing is copied through this process, and the answer
     * starts as soon as it is synthesised. */
    source->setStandardOutputProcess(d->player);

    connect(d->player, &QProcess::finished, this, [this] { emit finished(); });
    connect(d->player, &QProcess::errorOccurred, this, [this] { emit finished(); });

    d->player->start(player, { QStringLiteral("--raw"),
                               QStringLiteral("--rate=%1").arg(sample_rate),
                               QStringLiteral("--format=s16le"),
                               QStringLiteral("--channels=1") });
    return true;
}

void PcmSink::stop()
{
    if (!d->player) return;
    d->player->disconnect(this);
    d->player->kill();
    d->player->deleteLater();
    d->player = nullptr;
}

#endif
