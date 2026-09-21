/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

/* The install libdir, as GNUInstallDirs spells it on the distribution this was
 * built for. Only a default so that a build without install rules still
 * compiles; the real value comes from src/CMakeLists.txt. */
#ifndef OPENNCOMM_LIBDIR
#define OPENNCOMM_LIBDIR "lib"
#endif

namespace {

QString firstDirContaining(const QStringList &dirs, const QString &probe)
{
    for (const QString &d : dirs)
        if (!d.isEmpty() && QFileInfo::exists(QDir(d).filePath(probe)))
            return QDir(d).absolutePath();
    return QString();
}

QStringList candidateModelDirs()
{
    const QString bin = QCoreApplication::applicationDirPath();
    return {
        qEnvironmentVariable("OPENNCOMM_MODELS"),
#ifdef Q_OS_WIN
        /* Windows has no FHS and no prefix to be relative to. Both the
         * installer and the portable .zip lay the application out as one
         * directory -- openncomm.exe at the top, models\ and piper\ beside it
         * -- because that is the layout a person can copy to a USB stick and
         * still have work, and because Program Files is read-only to the
         * patient who will be using it. */
        QDir(bin).filePath(QStringLiteral("models")),
#endif
        QDir(bin).filePath(QStringLiteral("../share/openncomm/models")), /* installed / AppImage */
        paths::writableModelsDir(),                                      /* fetched, or added by hand */
        QDir(bin).filePath(QStringLiteral("../../models")),              /* build tree */
        QStringLiteral("models"),                                        /* cwd, for dev */
    };
}

} // namespace

namespace paths {

QString writableModelsDir()
{
    /* Deliberately not AppDataLocation. With the organisation and application
     * both named OpennComm, Qt returns ~/.local/share/OpennComm/OpennComm --
     * the name doubled. Build the XDG path directly so it is the one the
     * documentation claims and the one a person would guess. */
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("openncomm/models"));
}

QString modelsDir()
{
    /* Probed on the face model because it is the one that is always present:
     * every layout carries it, and without it there is no input at all. */
    const QString found = firstDirContaining(candidateModelDirs(),
                                             QStringLiteral("face_landmarker.task"));
    return found.isEmpty() ? writableModelsDir() : found;
}

QStringList modelsDirs()
{
    QStringList dirs;
    for (const QString &d : candidateModelDirs()) {
        if (d.isEmpty() || !QDir(d).exists()) continue;
        const QString abs = QDir(d).absolutePath();
        if (!dirs.contains(abs)) dirs << abs;
    }
    return dirs;
}

QString model(const QString &filename)
{
    /* A voice added by hand may sit in XDG data while everything else comes
     * from the package, so each file is resolved independently. */
    for (const QString &d : candidateModelDirs()) {
        if (d.isEmpty()) continue;
        const QString path = QDir(d).filePath(filename);
        if (QFileInfo::exists(path)) return QFileInfo(path).absoluteFilePath();
    }
    return QDir(writableModelsDir()).filePath(filename);
}

QString piperBinary()
{
    const QString bin = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("piper.exe");
#else
    const QString exe = QStringLiteral("piper");
#endif
    const QStringList candidates = {
        qEnvironmentVariable("OPENNCOMM_PIPER"),
#ifdef Q_OS_WIN
        /* Beside the application, like the models. Piper's onnxruntime and
         * espeak-ng DLLs sit in this same directory and Windows resolves them
         * from there automatically, which is why -- unlike on Linux -- nothing
         * has to be told where to look. See speech.cc. */
        QDir(bin).filePath(QStringLiteral("piper/") + exe),
#endif
        /* Installed. Piper is a binary with its own shared libraries beside it,
         * so it belongs in libdir, not share/ -- and libdir is spelled
         * differently per distribution (lib64 on Fedora, lib/x86_64-linux-gnu
         * on Debian), which is why it is baked in at build time. Still relative
         * to the executable, so an installed tree stays relocatable. */
        QDir(bin).filePath(QStringLiteral("../" OPENNCOMM_LIBDIR "/openncomm/piper/") + exe),
        /* AppImage, which puts everything under share/ regardless. */
        QDir(bin).filePath(QStringLiteral("../share/openncomm/piper/") + exe),
        QDir(bin).filePath(QStringLiteral("../../third_party/piper/") + exe),
        QStringLiteral("third_party/piper/") + exe,
    };
    for (const QString &c : candidates)
        if (!c.isEmpty() && QFileInfo(c).isExecutable())
            return QFileInfo(c).absoluteFilePath();
    return QString();
}

} // namespace paths
