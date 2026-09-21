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
#include <vector>

#include "audio.h"
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

#ifdef Q_OS_WIN
#include <windows.h>

/* Put --version and --check output where the person who asked for it is
 * looking.
 *
 * openncomm.exe is a GUI-subsystem binary -- it has to be, or every launch
 * would flash up a console window behind a full-screen application used by
 * somebody who cannot dismiss it. The cost is that it starts with no standard
 * output at all, so `openncomm.exe --version` typed into a terminal prints
 * nothing and returns 0, which reads exactly like a broken install.
 *
 * Attaching to the console that started it fixes that without giving the
 * normal path a console it does not want. Nothing is allocated when there is
 * no parent console -- a double-click, or a Start Menu launch -- because a
 * window that appears and vanishes is worse than silence. */
static bool hasRealHandle(DWORD which)
{
    const HANDLE h = GetStdHandle(which);
    return h != nullptr && h != INVALID_HANDLE_VALUE;
}

static void attachParentConsole()
{
    /* Asked before attaching, because attaching gives the process console
     * handles and the answer would then always be yes. */
    const bool out_redirected = hasRealHandle(STD_OUTPUT_HANDLE);
    const bool err_redirected = hasRealHandle(STD_ERROR_HANDLE);

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;

    /* Only a stream with nowhere to go is pointed at the console. Doing it
     * unconditionally breaks redirection: `openncomm.exe --check > log.txt`
     * would write to the terminal and leave the file empty, and a caregiver
     * asked to send that file would send nothing. */
    FILE *unused = nullptr;
    if (!out_redirected) freopen_s(&unused, "CONOUT$", "w", stdout);
    if (!err_redirected) freopen_s(&unused, "CONOUT$", "w", stderr);
}
#else
static void attachParentConsole() {}
#endif

/* The model lives beside the binary in an installed build and under the source
 * tree during development. Look in both rather than making the developer
 * remember a working directory. */
static QString findModel()
{
    const QString path = paths::model(QStringLiteral("face_landmarker.task"));
    return QFileInfo::exists(path) ? path : QString();
}

/* `openncomm --fetch-models` downloads the models into the user's own data
 * directory.
 *
 * Nothing installed from a .deb or an .rpm needs this: those carry every model
 * and both voices. It is for the layouts that do not -- a source tree where
 * scripts/fetch-deps.sh has not been run, and the AppImage, which bundles only
 * the face model -- and for adding a model to an installed system without
 * root.
 *
 * curl does the work rather than Qt Network: it resumes a partial download,
 * it already shows a progress bar that does not need reinventing, and it is
 * present on every system this runs on -- including Windows, which has shipped
 * curl.exe since Windows 10 1803.
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

    /* What is actually missing, worked out before anything is created or
     * announced. Looked for everywhere the application would look, not just in
     * the download directory: a .deb or an .rpm carries all of this already,
     * and quietly fetching a second 1.3 GB copy into the home directory of
     * somebody who ran this out of caution is not a harmless no-op. */
    std::vector<const Item *> missing;
    for (const Item &item : items) {
        const QString found = paths::model(QString::fromLatin1(item.file));
        if (QFileInfo::exists(found))
            printf("  have   %-28s %s\n", item.what, qPrintable(found));
        else
            missing.push_back(&item);
    }
    if (missing.empty()) {
        /* No directory is created in this case, on purpose. An installed
         * package needs nothing, and leaving an empty ~/.local/share behind
         * suggests otherwise. */
        printf("\nEverything is already installed. Run --check to verify.\n");
        return 0;
    }

    /* Checked once, before anything is created, rather than discovered six
     * times over as a failed download. A machine old enough to have no curl is
     * a real thing to run into, and "FAILED speech recognition" would send the
     * person looking at their network. */
    if (QStandardPaths::findExecutable(QStringLiteral("curl")).isEmpty()) {
        fprintf(stderr, "curl was not found, and is needed to download models.\n");
        return 1;
    }

    const QString dir = paths::writableModelsDir();
    if (!QDir().mkpath(QDir(dir).filePath(QStringLiteral("voices")))) {
        fprintf(stderr, "Cannot create %s\n", qPrintable(dir));
        return 1;
    }
    printf("\nDownloading into %s\n\n", qPrintable(dir));

    int failures = 0;
    for (const Item *itemp : missing) {
        const Item &item = *itemp;
        const QString target = QDir(dir).filePath(QString::fromLatin1(item.file));
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
    /* Each model is resolved on its own, never from one shared directory. A
     * packaged install finds everything beside the binary, but a source tree,
     * an AppImage, or a package with a voice added by hand mixes the two, so
     * "the models directory" is not a single place. Getting this wrong reports
     * a perfectly good install as broken. */
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

    {
        /* Asked of the capture backend rather than probed for here. What
         * recording needs differs per platform -- an ffmpeg binary on Linux, a
         * sound device on Windows -- and a self-check that reports the wrong
         * missing thing sends the operator looking for a program that was
         * never going to be there. See src/audio.h. */
        const QString why = MicSource::unavailableReason();
        report("microphone", why.isEmpty(),
               why.isEmpty() ? MicSource::backendName() : why);
    }

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
            attachParentConsole();
            std::printf("OpennComm %s\n", OPENNCOMM_VERSION);
            std::fflush(stdout);
            return 0;
        }
        if (arg == QLatin1StringView("--check") || arg == QLatin1StringView("--fetch-models"))
            headless = true;
    }

    if (headless) {
        attachParentConsole();
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
#ifdef Q_OS_WIN
            QStringLiteral("The face model is missing.\n\n"
                           "Reinstall OpennComm, or run scripts\\fetch-deps.ps1 "
                           "if you are running from a source tree."));
#else
            QStringLiteral("The face model is missing.\n\nRun scripts/fetch-deps.sh first."));
#endif
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
