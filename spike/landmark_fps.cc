/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Risk retirement: can we reproduce MediaPipe's 478-point mesh natively, at a
 * usable frame rate, and feed it straight into core/ ?
 *
 * This matters more than it looks. Every tuned constant in core/ear.h is an
 * INDEX into MediaPipe's refined 478-point mesh, and the 0.65 adaptive
 * threshold was validated against that mesh's numbers. Any other landmark model
 * means re-tuning blink detection from scratch on a tool where a missed blink
 * means a patient cannot speak.
 *
 * MediaPipe 1.0.x ships an official C API in libmediapipe.so, so we get the
 * refined mesh straight from upstream -- no ONNX conversion, no accuracy
 * drift, nothing to re-tune.
 *
 * Run:  ./build/spike/landmark_fps [seconds]
 */
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include <opencv2/opencv.hpp>

extern "C" {
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"
}

#include "openncomm/blink.h"
#include "openncomm/ear.h"
#include "openncomm/gaze.h"

static int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char **argv)
{
    const int run_seconds = argc > 1 ? atoi(argv[1]) : 10;

    MpFaceLandmarkerOptions opts{};
    opts.base_options.model_asset_path = "models/face_landmarker.task";
    opts.base_options.delegate = MP_DELEGATE_CPU;
    opts.running_mode = MP_RUNNING_MODE_VIDEO;
    opts.num_faces = 1;
    opts.min_face_detection_confidence = 0.5f;
    opts.min_face_presence_confidence = 0.5f;
    opts.min_tracking_confidence = 0.5f;
    opts.output_face_blendshapes = false;
    opts.output_facial_transformation_matrixes = false;
    opts.result_callback = nullptr;

    MpFaceLandmarkerPtr lm = nullptr;
    char *err = nullptr;
    if (MpFaceLandmarkerCreate(&opts, &lm, &err) != kMpOk) {
        fprintf(stderr, "landmarker create failed: %s\n", err ? err : "(no message)");
        return 1;
    }
    printf("landmarker ready (CPU delegate)\n");

    cv::VideoCapture cap(0, cv::CAP_V4L2);
    if (!cap.isOpened()) { fprintf(stderr, "cannot open /dev/video0\n"); return 1; }
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 30);

    /* Webcams default to "aperture priority" auto-exposure, which in anything
     * less than bright light lengthens the exposure and HALVES the frame rate --
     * this camera drops from 29fps to 16fps, and the drop is silent.
     *
     * That is not a cosmetic problem here. Frame rate is the sampling
     * resolution of every blink threshold in core/blink.h: at 16fps a frame is
     * 62ms, so the 110ms ignore floor is under two frames and the 180ms
     * ambiguous band is barely three. Blink classification gets coarse exactly
     * when the room is dim -- which is to say, at a bedside, at night.
     *
     * Forcing manual exposure restores 29fps. Reaching the V4L2 control
     * directly is the whole reason this runs natively. */
    const int exposure = argc > 2 ? atoi(argv[2]) : 0;   /* 0 = leave on auto */
    const int gain      = argc > 3 ? atoi(argv[3]) : 0;
    if (exposure > 0) {
        cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);  /* 1 = manual, 3 = aperture priority */
        cap.set(cv::CAP_PROP_EXPOSURE, exposure);
        /* Gain buys back the brightness that the shorter exposure gave up. It
         * costs sensor noise, which the adaptive EAR threshold absorbs far
         * better than it absorbs a halved frame rate. */
        if (gain > 0) cap.set(cv::CAP_PROP_GAIN, gain);
    }

    printf("camera %dx%d @ %.0f fps requested\n",
           (int)cap.get(cv::CAP_PROP_FRAME_WIDTH),
           (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
           cap.get(cv::CAP_PROP_FPS));

    oc_ear_history hist;
    oc_ear_history_init(&hist);
    oc_blink_tracker tracker;
    oc_blink_tracker_init(&tracker, oc_blink_bands_for(OC_SPEED_NORMAL));

    std::vector<oc_point> mesh(478);
    std::vector<double> latencies;
    double smooth_x = 0.5, smooth_y = 0.5;
    int frames = 0, detected = 0, shorts = 0, longs = 0, discards = 0;

    const int64_t t0 = now_ms();
    cv::Mat bgr, rgb;

    while (now_ms() - t0 < run_seconds * 1000) {
        if (!cap.read(bgr) || bgr.empty()) continue;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        frames++;

        MpImagePtr img = nullptr;
        if (MpImageCreateFromUint8Data(kMpImageFormatSrgb, rgb.cols, rgb.rows,
                                       rgb.data, (int)(rgb.total() * rgb.elemSize()),
                                       &img, &err) != kMpOk) {
            fprintf(stderr, "image create failed: %s\n", err ? err : "?"); break;
        }

        MpFaceLandmarkerResult res{};
        int64_t t_in = now_ms();
        MpStatus st = MpFaceLandmarkerDetectForVideo(lm, img, nullptr, t_in, &res, &err);
        double latency = (double)(now_ms() - t_in);

        if (st == kMpOk && res.face_landmarks_count > 0 &&
            res.face_landmarks[0].landmarks_count >= 478) {
            detected++;
            latencies.push_back(latency);

            const MpNormalizedLandmark *pts = res.face_landmarks[0].landmarks;
            for (int i = 0; i < 478; i++) mesh[i] = { pts[i].x, pts[i].y };

            double ear = oc_ear_both(mesh.data());
            int closed = oc_ear_push(&hist, ear);
            oc_blink_event ev = oc_blink_tracker_update(&tracker, closed, now_ms());
            if (ev.gesture == OC_GESTURE_SHORT)   shorts++;
            if (ev.gesture == OC_GESTURE_LONG)    longs++;
            if (ev.gesture == OC_GESTURE_DISCARD) discards++;

            const oc_point nose = mesh[OC_LANDMARK_NOSE];
            smooth_x = oc_smooth_step(smooth_x, nose.x);
            smooth_y = oc_smooth_step(smooth_y, nose.y);

            if (frames % 15 == 0) {
                int q = oc_gaze_quadrant(nullptr, true, smooth_x, smooth_y);
                printf("\r nose(%.3f,%.3f) quad %d | EAR %.3f avg %.3f %s | "
                       "%.0fms | short %d long %d discard %d   ",
                       smooth_x, smooth_y, q, ear, oc_ear_average(&hist),
                       closed ? "SHUT" : "open", latency, shorts, longs, discards);
                fflush(stdout);
            }
        }

        MpFaceLandmarkerCloseResult(&res);
        MpImageFree(img);
    }

    double wall = (double)(now_ms() - t0) / 1000.0;
    double sum = 0, worst = 0;
    for (double l : latencies) { sum += l; if (l > worst) worst = l; }

    printf("\n\n--- results over %.1fs ---\n", wall);
    printf("  frames captured   %d  (%.1f fps)\n", frames, frames / wall);
    printf("  face detected     %d  (%.0f%% of frames)\n", detected,
           frames ? 100.0 * detected / frames : 0.0);
    printf("  inference mean    %.1f ms\n", latencies.empty() ? 0 : sum / latencies.size());
    printf("  inference worst   %.0f ms\n", worst);
    printf("  gestures          %d short, %d long, %d discarded\n", shorts, longs, discards);
    printf("\n  30 fps needs inference under 33ms. %s\n",
           (!latencies.empty() && sum / latencies.size() < 33.0) ? "PASS" : "see above");

    MpFaceLandmarkerClose(lm, &err);
    return 0;
}
