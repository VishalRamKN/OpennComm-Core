/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <cstdio>

#include "camera.h"
#include "generator.h"
#include "listener.h"
#include "speech.h"
#include "mainwindow.h"
#include "openncomm/answers.h"
#include "paths.h"

/* Set from the VERSION file by src/CMakeLists.txt. The fallback exists so the
 * file still compiles if it is ever built outside this project's CMake. */
#ifndef OPENNCOMM_VERSION
#define OPENNCOMM_VERSION "0.0.0-unknown"
#endif

/* The model lives beside the binary in an installed build and under the source
 * tree during development. Look in both rather than making the developer
 * remember a working directory. */
static QString findModel()
{
    const QString path = paths::model(QStringLiteral("face_landmarker.task"));
    return QFileInfo::exists(path) ? path : QString();
}

/* `openncomm --fetch-models` downloads what a packaged build deliberately does
 * not carry.
 *
 * Only the face model ships inside the bundle. The rest is about 1.2 GB against
 * roughly 200 MB of code, and bundling it would make every update a gigabyte
 * download. curl does the work rather than Qt Network: it is present
 * everywhere, it resumes a partial download, and it already shows a progress
 * bar that does not need reinventing.
 *
 * Nothing here is required to use the application. Without any of it the
 * patient still has morse spelling, the built-in phrasebook and espeak-ng. */
static int runFetchModels()
{
    struct Item { const char *file; const char *url; const char *what; };
    static const Item items[] = {
        { "ggml-base.en-q5_1.bin",
          "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin",
          "speech recognition (57 MB)" },
        { "qwen2.5-1.5b-instruct-q4_k_m.gguf",
          "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/"
          "qwen2.5-1.5b-instruct-q4_k_m.gguf",
          "answer generation (1.1 GB)" },
        { "voices/en_US-amy-medium.onnx",
          "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/"
          "en_US-amy-medium.onnx",
          "a woman's voice (61 MB)" },
        { "voices/en_US-amy-medium.onnx.json",
          "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/"
          "en_US-amy-medium.onnx.json",
          "voice settings" },
        { "voices/en_US-joe-medium.onnx",
          "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/joe/medium/"
          "en_US-joe-medium.onnx",
          "a man's voice (61 MB)" },
        { "voices/en_US-joe-medium.onnx.json",
          "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/joe/medium/"
          "en_US-joe-medium.onnx.json",
          "voice settings" },
    };

    const QString dir = paths::writableModelsDir();
    if (!QDir().mkpath(QDir(dir).filePath(QStringLiteral("voices")))) {
        fprintf(stderr, "Cannot create %s\n", qPrintable(dir));
        return 1;
    }
    printf("Downloading into %s\n\n", qPrintable(dir));

    int failures = 0;
    for (const Item &item : items) {
        const QString target = QDir(dir).filePath(QString::fromLatin1(item.file));
        if (QFileInfo::exists(target)) {
            printf("  have   %s\n", item.what);
            continue;
        }
        printf("  fetch  %s\n", item.what);
        QProcess curl;
        curl.setProcessChannelMode(QProcess::ForwardedChannels);
        /* -C - resumes, so an interrupted 1.1 GB download is not started over. */
        curl.start(QStringLiteral("curl"),
                   { QStringLiteral("-fL"), QStringLiteral("-C"), QStringLiteral("-"),
                     QStringLiteral("--progress-bar"), QStringLiteral("-o"), target,
                     QString::fromLatin1(item.url) });
        curl.waitForFinished(-1);
        if (curl.exitStatus() != QProcess::NormalExit || curl.exitCode() != 0) {
            fprintf(stderr, "  FAILED %s\n", item.what);
            QFile::remove(target);
            failures++;
        }
    }

    printf("\n%s\n", failures == 0 ? "All models installed. Run --check to verify."
                                   : "Some downloads failed. Re-run to resume.");
    return failures == 0 ? 0 : 1;
}

/* `openncomm --check` exercises every subsystem from the terminal and says
 * plainly which ones are up. The failure this exists for is quiet: a language
 * model that never loaded is indistinguishable, from the four buttons alone,
 * from a model that simply wrote plain answers. */
static int runCheck()
{
    /* Each model is resolved on its own, never from one shared directory. In a
     * packaged build the face model comes from inside the bundle while the
     * large ones have been downloaded to XDG data, so "the models directory" is
     * not a single place. Getting this wrong reports a perfectly good install
     * as broken. */
    int failures = 0;
    auto report = [&](const char *name, bool ok, const QString &detail) {
        printf("  %-12s %-4s %s\n", name, ok ? "ok" : "FAIL", qPrintable(detail));
        if (!ok) failures++;
    };

    printf("OpennComm %s self-check\n\n  models: %s\n\n",
           OPENNCOMM_VERSION, qPrintable(paths::modelsDir()));

    {
        /* Actually speak, rather than just finding the binary. Audio has more
         * ways to be silently broken than missing -- no sound server, a muted
         * sink, a voice at the wrong sample rate -- and all of them look
         * identical from a `which`. The operator should hear this. */
        Speech speech;
        bool spoke = false;
        if (speech.available()) {
            QEventLoop loop;
            QTimer::singleShot(25000, &loop, &QEventLoop::quit);
            QObject::connect(&speech, &Speech::finished, &loop, [&] { spoke = true; loop.quit(); });
            speech.speak(QStringLiteral("OpennComm speech check. You should hear this sentence."));
            loop.exec();
        }
        report("speech out", spoke,
               spoke ? speech.engineName()
                     : QStringLiteral("%1 — no audio was produced").arg(speech.engineName()));
    }

    const bool ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    report("microphone", ffmpeg, ffmpeg ? QStringLiteral("ffmpeg")
                                        : QStringLiteral("ffmpeg not installed"));

    {
        QThread thread;
        Listener listener(paths::model(QStringLiteral("ggml-base.en-q5_1.bin")));
        listener.moveToThread(&thread);
        bool ok = false;
        QString detail;
        QEventLoop loop;
        QObject::connect(&listener, &Listener::ready, &loop, [&] {
            ok = true; detail = QStringLiteral("whisper base.en"); loop.quit();
        });
        QObject::connect(&listener, &Listener::failed, &loop,
                         [&](const QString &m) { detail = m; loop.quit(); });
        QObject::connect(&thread, &QThread::started, &listener, &Listener::load);
        thread.start();
        loop.exec();
        thread.quit();
        thread.wait(5000);
        report("speech in", ok, detail);
    }

    {
        QThread thread;
        Generator gen(paths::model(QStringLiteral("qwen2.5-1.5b-instruct-q4_k_m.gguf")));
        gen.moveToThread(&thread);
        bool loaded = false, answered = false;
        QString detail;
        QEventLoop loop;

        QObject::connect(&gen, &Generator::failed, &loop,
                         [&](const QString &m) { detail = m; loop.quit(); });
        QObject::connect(&gen, &Generator::loaded, &gen, [&](const QString &name) {
            loaded = true;
            detail = name;
            QMetaObject::invokeMethod(&gen, "generate", Qt::QueuedConnection,
                                      Q_ARG(quint64, 1),
                                      Q_ARG(QString, QStringLiteral("Are you in pain?")),
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QStringList, QStringList()));
        });
        QObject::connect(&gen, &Generator::produced, &loop,
                         [&](quint64, const QStringList &lines, double ms) {
            std::vector<QByteArray> storage;
            std::vector<const char *> raw;
            for (const QString &l : lines) storage.push_back(l.toUtf8());
            for (const QByteArray &b : storage) raw.push_back(b.constData());
            oc_options out;
            const int n = oc_options_from_model(raw.empty() ? nullptr : raw.data(),
                                                (int)raw.size(), OC_INTENT_PAIN, nullptr, &out);
            answered = n > 0;
            detail = QStringLiteral("%1, %2 of 4 written in %3 ms")
                         .arg(detail).arg(n).arg(ms, 0, 'f', 0);
            printf("\n  sample answers to \"Are you in pain?\"\n");
            for (int i = 0; i < OC_OPTION_COUNT; i++) printf("    %d. %s\n", i + 1, out.text[i]);
            printf("\n");
            loop.quit();
        });
        QObject::connect(&thread, &QThread::started, &gen, &Generator::load);
        thread.start();
        loop.exec();
        thread.quit();
        thread.wait(20000);
        report("answers", loaded && answered, detail);
    }

    printf("\n%s\n", failures == 0
        ? "Everything is working."
        : "Some subsystems are unavailable. OpennComm still runs: the patient keeps\n"
          "four selectable answers and morse spelling whatever is missing.");
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    /* --check and --fetch-models need no window, so they must not construct a
     * QApplication: that loads a QPA platform plugin and aborts outright when
     * there is no display. Diagnosing an installation over SSH, or inside a
     * container, is exactly when a self-check is most wanted. Scanned before
     * the application object exists, which is why this is raw argv. */
    bool headless = false;
    for (int i = 1; i < argc; i++) {
        const QLatin1StringView arg(argv[i]);
        /* Answered before anything else is constructed, and without a
         * QApplication. Someone reporting that an answer was spoken wrongly
         * needs to be able to say which build did it, and they may be reading
         * it out over the phone from a ward with no display attached. */
        if (arg == QLatin1StringView("--version") || arg == QLatin1StringView("-v")) {
            std::printf("OpennComm %s\n", OPENNCOMM_VERSION);
            return 0;
        }
        if (arg == QLatin1StringView("--check") || arg == QLatin1StringView("--fetch-models"))
            headless = true;
    }

    if (headless) {
        QCoreApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("OpennComm"));
        app.setOrganizationName(QStringLiteral("OpennComm"));
        app.setApplicationVersion(QStringLiteral(OPENNCOMM_VERSION));
        for (int i = 1; i < argc; i++) {
            if (QLatin1StringView(argv[i]) == QLatin1StringView("--fetch-models"))
                return runFetchModels();
        }
        return runCheck();
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OpennComm"));
    app.setOrganizationName(QStringLiteral("OpennComm"));
    app.setApplicationVersion(QStringLiteral(OPENNCOMM_VERSION));
    qRegisterMetaType<FaceFrame>("FaceFrame");

    const QString model = findModel();
    if (model.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("OpennComm"),
            QStringLiteral("The face model is missing.\n\nRun scripts/fetch-deps.sh first."));
        return 1;
    }

    MainWindow w(model);
    /* Set the windowed geometry first: it is what Escape and F11 restore to,
     * and without it leaving full screen lands on whatever size Qt guesses. */
    w.resize(1280, 820);

    /* Full screen by default. The patient reads the four answers from a bed,
     * often at a distance and often without their glasses, so every pixel the
     * window does not use is one the answers could have been printed in.
     * --windowed is for running it beside something else; F11 and Escape
     * toggle at any time. */
    bool windowed = false;
    for (int i = 1; i < argc; i++) {
        if (QLatin1StringView(argv[i]) == QLatin1StringView("--windowed"))
            windowed = true;
    }
    if (windowed) w.show(); else w.showFullScreen();
    return app.exec();
}
