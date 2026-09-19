/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "camera.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

#include <opencv2/opencv.hpp>
#include <vector>

extern "C" {
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"
}

#include "openncomm/ear.h"
#include "openncomm/gaze.h"

namespace {
/* Below this the frame interval becomes comparable with the blink thresholds
 * themselves. At 16fps a frame is 62ms, so the 110ms ignore floor is under two
 * frames and the 180ms ambiguous band is barely three -- blink classification
 * gets coarse exactly where it matters. Webcams reach this state silently, by
 * lengthening exposure in dim light. */
constexpr double kMinUsableFps = 20.0;
}

struct CameraWorker::Impl {
    QString model_path;
    cv::VideoCapture cap;
    MpFaceLandmarkerPtr landmarker = nullptr;
    QTimer *timer = nullptr;
    oc_ear_history ear_history{};
    std::vector<oc_point> mesh;
    QElapsedTimer clock;
    int frames = 0;
    double fps = 0.0;
    qint64 fps_window_start = 0;
    bool warned_slow = false;
    bool running = false;
};

CameraWorker::CameraWorker(QString model_path, QObject *parent)
    : QObject(parent), d(new Impl)
{
    d->model_path = std::move(model_path);
    d->mesh.resize(478);
    oc_ear_history_init(&d->ear_history);
}

CameraWorker::~CameraWorker()
{
    closeAll();
    delete d;
}

void CameraWorker::start()
{
    MpFaceLandmarkerOptions opts{};
    const QByteArray model = d->model_path.toUtf8();
    opts.base_options.model_asset_path = model.constData();
    opts.base_options.delegate = MP_DELEGATE_CPU;
    opts.running_mode = MP_RUNNING_MODE_VIDEO;
    opts.num_faces = 1;
    opts.min_face_detection_confidence = 0.5f;
    opts.min_face_presence_confidence = 0.5f;
    opts.min_tracking_confidence = 0.5f;
    opts.result_callback = nullptr;

    char *err = nullptr;
    if (MpFaceLandmarkerCreate(&opts, &d->landmarker, &err) != kMpOk) {
        emit failed(QStringLiteral("Could not load the face model from %1: %2")
                        .arg(d->model_path, err ? QString::fromUtf8(err) : QStringLiteral("unknown error")));
        return;
    }

    openCamera();
    if (!d->cap.isOpened()) {
        emit failed(QStringLiteral("Could not open the camera. Is another application using it?"));
        return;
    }

    d->clock.start();
    d->fps_window_start = 0;
    d->running = true;

    /* Drive capture from a zero-interval timer rather than a while loop so the
     * worker's event loop still processes stop(). */
    d->timer = new QTimer(this);
    d->timer->setInterval(0);
    connect(d->timer, &QTimer::timeout, this, &CameraWorker::processOnce);
    d->timer->start();
}

void CameraWorker::openCamera()
{
    d->cap.open(0, cv::CAP_V4L2);
    if (!d->cap.isOpened()) return;
    d->cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    d->cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    d->cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    d->cap.set(cv::CAP_PROP_FPS, 30);
    d->cap.set(cv::CAP_PROP_BUFFERSIZE, 1); /* latency matters more than smoothness */
}

void CameraWorker::stop()
{
    d->running = false;
    if (d->timer) d->timer->stop();
    closeAll();
}

void CameraWorker::closeAll()
{
    if (d->cap.isOpened()) d->cap.release();
    if (d->landmarker) {
        char *err = nullptr;
        MpFaceLandmarkerClose(d->landmarker, &err);
        d->landmarker = nullptr;
    }
}

void CameraWorker::processOnce()
{
    if (!d->running) return;

    cv::Mat bgr;
    if (!d->cap.read(bgr) || bgr.empty()) return;

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    FaceFrame out;

    char *err = nullptr;
    MpImagePtr image = nullptr;
    if (MpImageCreateFromUint8Data(kMpImageFormatSrgb, rgb.cols, rgb.rows, rgb.data,
                                   static_cast<int>(rgb.total() * rgb.elemSize()),
                                   &image, &err) != kMpOk) {
        return;
    }

    MpFaceLandmarkerResult res{};
    const qint64 t_in = d->clock.elapsed();
    MpStatus st = MpFaceLandmarkerDetectForVideo(d->landmarker, image, nullptr, t_in, &res, &err);
    out.inference_ms = static_cast<double>(d->clock.elapsed() - t_in);

    if (st == kMpOk && res.face_landmarks_count > 0 &&
        res.face_landmarks[0].landmarks_count >= 478) {
        const MpNormalizedLandmark *pts = res.face_landmarks[0].landmarks;
        for (int i = 0; i < 478; i++) d->mesh[i] = { pts[i].x, pts[i].y };

        out.face_detected = true;
        out.nose_x = d->mesh[OC_LANDMARK_NOSE].x;
        out.nose_y = d->mesh[OC_LANDMARK_NOSE].y;
        out.ear = oc_ear_both(d->mesh.data());
        out.blink = oc_ear_push(&d->ear_history, out.ear);
        out.ear_average = oc_ear_average(&d->ear_history);
    }
    MpFaceLandmarkerCloseResult(&res);
    MpImageFree(image);

    /* Mirror for display only. The landmark coordinates stay raw; core/ owns
     * the decision about which way round the feed runs. */
    cv::Mat shown;
    cv::flip(rgb, shown, 1);
    out.preview = QImage(shown.data, shown.cols, shown.rows,
                         static_cast<int>(shown.step), QImage::Format_RGB888).copy();

    d->frames++;
    const qint64 now = d->clock.elapsed();
    if (now - d->fps_window_start >= 1000) {
        d->fps = d->frames * 1000.0 / static_cast<double>(now - d->fps_window_start);
        d->frames = 0;
        d->fps_window_start = now;
        if (!d->warned_slow && d->fps > 0 && d->fps < kMinUsableFps && now > 3000) {
            d->warned_slow = true;
            emit lowFrameRate(d->fps);
        }
    }
    out.fps = d->fps;

    emit frameReady(out);
}
