/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Where the models and the Piper binary live.
 *
 * Four layouts have to work: a development tree, an installed Unix prefix, an
 * AppImage whose own directory is read-only, and a Windows install -- which is
 * one flat directory rather than a prefix, because Windows has no FHS and
 * because that is the layout that still works when it is copied to a USB stick.
 *
 * A .deb, .rpm or Windows package carries every model it needs -- face tracking, speech
 * recognition, answer generation and both voices -- so an installed system
 * finds all of them beside the binary and never has to reach the network. The
 * XDG data directory is searched all the same, and is still where
 * `--fetch-models` writes: it is how a source tree and the AppImage get their
 * models, and how anybody adds a Piper voice of their own without root.
 *
 * Each file is resolved on its own, never from one shared directory, because
 * those two sources mix: a packaged install with a third voice dropped into
 * ~/.local/share is a normal thing to have.
 *
 * What is NOT conditional on any of that: the application must be usable with
 * whatever subset of the models is actually present. Morse spelling and the
 * intent phrasebook need nothing but the face model.
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
