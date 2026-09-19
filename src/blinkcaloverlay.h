/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* The full-screen blink calibration screen.
 *
 * Head calibration teaches the application where this patient's gaze reaches.
 * This teaches it what their blinks mean, which matters just as much and was
 * missing: the three presets in blink.h are guesses, and a patient whose long
 * blink falls inside another patient's short band cannot use the application
 * at all.
 *
 * Like the head overlay, nothing starts until the caregiver presses Start, and
 * nothing the patient does while this is up may count as an answer.
 *
 * Closure timing is measured here rather than through oc_blink_tracker,
 * because the tracker classifies against bands and bands are precisely what is
 * not known yet.
 */
#ifndef OPENNCOMM_BLINKCALOVERLAY_H
#define OPENNCOMM_BLINKCALOVERLAY_H

#include <QWidget>

#include "openncomm/blinkcal.h"

class QLabel;
class QPushButton;

class BlinkCalOverlay : public QWidget {
    Q_OBJECT
public:
    explicit BlinkCalOverlay(QWidget *parent = nullptr);

    /* Feed a camera frame. Ignored unless a run is in progress. */
    void onFrame(bool blink, bool face, qint64 now_ms);

signals:
    void completed(const oc_bands &bands);
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void begin();
    void layoutWidgets();
    void refreshText();

    oc_blinkcal cal_{};
    bool running_ = false;

    /* Raw closure tracking. */
    bool eye_closed_ = false;
    qint64 closed_at_ = 0;
    int live_ms_ = 0;        /* duration of the closure in progress */
    int last_ms_ = 0;        /* duration of the last accepted sample */
    bool last_rejected_ = false;

    QLabel *title_ = nullptr;
    QLabel *instruction_ = nullptr;
    QLabel *hint_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *undo_ = nullptr;
};

#endif
