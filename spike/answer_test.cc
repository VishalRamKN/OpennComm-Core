/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Risk retirement: does a 1.5B model under a GBNF grammar produce answers a
 * paralysed patient would actually want to say?
 *
 * The grammar guarantees the SHAPE -- four lines, three to seven words. It
 * cannot guarantee the content is useful, on topic, or distinct. That is what
 * this measures, and it is the question that decides whether local generation
 * is worth shipping at all.
 *
 * Run:  ./build/spike/answer_test
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>

#include "generator.h"
#include "openncomm/answers.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString model = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                   : QStringLiteral("models/qwen2.5-1.5b-instruct-q4_k_m.gguf");

    const QStringList questions = {
        QStringLiteral("Are you in pain?"),
        QStringLiteral("Do you want some water?"),
        QStringLiteral("Did you sleep well last night?"),
        QStringLiteral("Should I call the nurse?"),
        QStringLiteral("Would you like to listen to music?"),
        QStringLiteral("Is the room too cold?"),
    };

    Generator gen(model);
    int index = 0;
    double total_ms = 0;
    int total_from_model = 0;

    QObject::connect(&gen, &Generator::failed, [&](const QString &m) {
        printf("FAILED: %s\n", qPrintable(m));
        app.quit();
    });

    QObject::connect(&gen, &Generator::loaded, [&](const QString &name) {
        printf("model: %s\n\n", qPrintable(name));
        gen.generate(1, questions[0], QStringLiteral("Anand"), {}, {});
    });

    QObject::connect(&gen, &Generator::produced,
                     [&](quint64, const QStringList &lines, double ms) {
        printf("Q: %s\n", qPrintable(questions[index]));
        printf("   raw model output (%d lines):\n", (int)lines.size());
        for (const QString &l : lines) printf("     | %s\n", qPrintable(l));

        std::vector<QByteArray> storage;
        std::vector<const char *> raw;
        for (const QString &l : lines) storage.push_back(l.toUtf8());
        for (const QByteArray &b : storage) raw.push_back(b.constData());

        oc_options out;
        const int from_model = oc_options_from_model(
            raw.empty() ? nullptr : raw.data(), (int)raw.size(),
            oc_detect_intent(questions[index].toUtf8().constData()), "Anand", &out);

        for (int i = 0; i < OC_OPTION_COUNT; i++) printf("   %d. %s\n", i + 1, out.text[i]);
        printf("   [%d/4 from model, %.0f ms]\n\n", from_model, ms);

        total_ms += ms;
        total_from_model += from_model;

        if (++index < questions.size()) {
            /* Queued, never direct: produced() is emitted from inside
             * generate(), so calling generate() here would re-enter it on the
             * same llama_context and corrupt the decode in progress. */
            QMetaObject::invokeMethod(&gen, [&] {
                gen.generate(index + 1, questions[index], QStringLiteral("Anand"), {}, {});
            }, Qt::QueuedConnection);
        } else {
            printf("--- %d questions: %.0f ms mean, %d/%d answers from the model ---\n",
                   index, total_ms / index, total_from_model, index * 4);
            app.quit();
        }
    });

    QMetaObject::invokeMethod(&gen, "load", Qt::QueuedConnection);
    return app.exec();
}
