/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Local answer generation.
 *
 * Runs a small instruct model in-process through libllama. No server, no
 * socket, no network: the patient's conversation never leaves the machine,
 * which is the whole argument for doing this natively.
 *
 * Generation takes seconds on a CPU, so this object lives on its own thread and
 * NOTHING waits for it. The phrasebook has already filled the four buttons by
 * the time a request is issued; answers arriving later replace them. If the
 * model is missing, still loading, or produces nonsense, the patient simply
 * keeps the phrasebook answers and never knows anything was pending.
 *
 * Requests carry an id. A reply whose id is stale -- because the caregiver
 * asked a second question while the first was still generating -- is discarded
 * rather than overwriting the newer question's answers.
 */
#ifndef OPENNCOMM_GENERATOR_H
#define OPENNCOMM_GENERATOR_H

#include <QObject>
#include <QString>
#include <QStringList>

class Generator : public QObject {
    Q_OBJECT
public:
    explicit Generator(QString model_path, QObject *parent = nullptr);
    ~Generator() override;

public slots:
    void load();
    void generate(quint64 request_id, const QString &question,
                  const QString &patient_name, const QString &about,
                  const QStringList &history);

signals:
    void loaded(const QString &model_name);
    void failed(const QString &message);
    void produced(quint64 request_id, const QStringList &lines, double elapsed_ms);

private:
    struct Impl;
    Impl *d;
};

#endif
