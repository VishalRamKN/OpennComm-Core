/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Render the window to a PNG without a display, so the layout can be looked at
 * rather than reasoned about. Offscreen on purpose: it needs no desktop and
 * captures nothing but this application.
 *
 * Run:  ./build/spike/ui_shot out.png [mode] [--options "a|b|c|d"]
 */
#include <QApplication>
#include <QTimer>
#include <cstdio>

#include "camera.h"
#include "mainwindow.h"
#include "paths.h"

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OpennComm"));
    app.setOrganizationName(QStringLiteral("OpennComm"));
    qRegisterMetaType<FaceFrame>("FaceFrame");

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                 : QStringLiteral("ui.png");
    const int mode = argc > 2 ? atoi(argv[2]) : 0;

    MainWindow w(paths::model(QStringLiteral("face_landmarker.task")));
    w.resize(1280, 860);
    w.show();

    /* Let the models load and the first paint settle before grabbing. */
    QTimer::singleShot(mode >= 0 ? 1200 : 1200, &w, [&] {
        if (mode > 0) w.selectMode(mode);
        if (argc > 3) w.previewHold(0, atof(argv[3]));
        QTimer::singleShot(400, &w, [&] {
            const QPixmap shot = w.grab();
            if (shot.save(out)) printf("wrote %s (%dx%d)\n", qPrintable(out),
                                       shot.width(), shot.height());
            else fprintf(stderr, "failed to write %s\n", qPrintable(out));
            app.quit();
        });
    });

    return app.exec();
}
