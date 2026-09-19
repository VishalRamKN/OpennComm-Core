/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Camera capture and face landmarking, on a worker thread.
 *
 * Inference is 15-20ms per frame. Doing that on the GUI thread would stall the
 * window for half of every frame interval, so this object is moved onto its own
 * QThread and hands the main thread finished results only.
 *
 * What it emits is deliberately NOT raw landmarks. The 478-point mesh is an
 * implementation detail of this file; the rest of the application sees a nose
 * position, an eye-closed flag and a preview image. That keeps every consumer
 * independent of the landmark model.
 */
#ifndef OPENNCOMM_CAMERA_H
#define OPENNCOMM_CAMERA_H

#include <QImage>
#include <QObject>
#include <QString>

struct FaceFrame {
    QImage preview;      /* already mirrored for display */
    bool face_detected = false;
    double nose_x = 0.5; /* raw, un-mirrored, normalised */
    double nose_y = 0.5;
    int blink = 0;       /* 1 while the eyes read as closed */
    double ear = 0.0;
    double ear_average = 0.0;
    double fps = 0.0;
    double inference_ms = 0.0;
};
Q_DECLARE_METATYPE(FaceFrame)

class CameraWorker : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(QString model_path, QObject *parent = nullptr);
    ~CameraWorker() override;

public slots:
    void start();
    void stop();

signals:
    void frameReady(const FaceFrame &frame);
    void failed(const QString &message);
    /* Raised once when the achieved frame rate is too low to classify blink
     * durations reliably -- see the note in camera.cc. */
    void lowFrameRate(double fps);

private:
    void openCamera();
    void closeAll();
    void processOnce();

    struct Impl;
    Impl *d;
};

#endif
