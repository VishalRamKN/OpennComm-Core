/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* The full-screen calibration overlay.
 *
 * Nothing begins until the caregiver presses Start. Opening straight into a
 * countdown rushes the patient and, for calibration specifically, corrupts the
 * result -- the first point gets measured while they are still working out what
 * is being asked of them.
 *
 * While this overlay is up, NOTHING the patient does may count as an answer.
 * The window stops routing frames to the selectors for as long as it is
 * visible, exactly as it must for any setup screen.
 */
#ifndef OPENNCOMM_CALIBOVERLAY_H
#define OPENNCOMM_CALIBOVERLAY_H

#include <QWidget>

#include "openncomm/calibrate.h"

class QLabel;
class QPushButton;

class CalibOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CalibOverlay(QWidget *parent = nullptr);

    /* Feed a camera frame. Ignored unless a run is in progress. */
    void onFrame(double nose_x, double nose_y, bool face, qint64 now_ms);

signals:
    void completed(const oc_calib &result);
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void begin();
    void layoutWidgets();

    oc_calibrator calib_{};
    bool running_ = false;
    double progress_ = 0.0;
    int countdown_ = 0;
    bool face_ = false;

    QLabel *title_ = nullptr;
    QLabel *instruction_ = nullptr;
    QLabel *face_hint_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *cancel_ = nullptr;
};

#endif
