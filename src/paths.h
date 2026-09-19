/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Where the models and the Piper binary live.
 *
 * Three layouts have to work: a development tree, an installed prefix, and an
 * AppImage whose own directory is read-only. The models are too large to sit
 * inside a bundle -- about 1.2 GB against roughly 200 MB of code -- so a
 * packaged build downloads them into the user's XDG data directory on first
 * use, and finds them there afterwards.
 *
 * What is NOT conditional on any of that: the application must be usable before
 * a single model has been downloaded. Morse spelling and the intent phrasebook
 * need nothing but the face model, which is small enough to bundle.
 */
#ifndef OPENNCOMM_PATHS_H
#define OPENNCOMM_PATHS_H

#include <QString>
#include <QStringList>

namespace paths {

/* The directory holding face_landmarker.task, the GGUF, the whisper weights and
 * voices/. First of: $OPENNCOMM_MODELS, the bundle, XDG data, the source tree. */
QString modelsDir();

/* Where downloads should be written -- always XDG data, never the bundle. */
QString writableModelsDir();

/* The Piper executable, or an empty string when it is not installed. */
QString piperBinary();

QString model(const QString &filename);

/* Every directory that may hold models, in resolution order. For scanning --
 * listing installed voices, say -- where model() cannot help because the
 * filename is not known in advance. */
QStringList modelsDirs();

} // namespace paths

#endif
