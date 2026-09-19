/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "caliboverlay.h"

#include <QDateTime>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>

CalibOverlay::CalibOverlay(QWidget *parent) : QWidget(parent)
{
    oc_calibrator_init(&calib_);
    setAutoFillBackground(false);

    title_ = new QLabel(QStringLiteral("Calibrate head tracking"), this);
    title_->setStyleSheet(QStringLiteral("font-size:28px; font-weight:700; color:#e6edf7;"));
    title_->setAlignment(Qt::AlignCenter);

    instruction_ = new QLabel(this);
    instruction_->setWordWrap(true);
    instruction_->setAlignment(Qt::AlignCenter);
    instruction_->setStyleSheet(QStringLiteral("font-size:19px; color:#b9c8e4;"));
    instruction_->setText(QStringLiteral(
        "Five dots will appear one at a time. Point your nose at each one and hold still.\n"
        "Move your head, not just your eyes — it is the nose that is tracked.\n\n"
        "Sit the way you normally would. Takes about forty seconds."));

    face_hint_ = new QLabel(this);
    face_hint_->setAlignment(Qt::AlignCenter);
    face_hint_->setStyleSheet(QStringLiteral("font-size:15px; color:#7d8ba6;"));

    start_ = new QPushButton(QStringLiteral("Start"), this);
    start_->setStyleSheet(QStringLiteral(
        "background:#23407e; border:1px solid #6ea8ff; border-radius:9px;"
        "padding:12px 34px; font-size:17px; color:#e6edf7;"));
    connect(start_, &QPushButton::clicked, this, &CalibOverlay::begin);

    cancel_ = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_->setStyleSheet(QStringLiteral(
        "background:#1b2740; border:1px solid #2c3d63; border-radius:9px;"
        "padding:12px 26px; color:#b9c8e4;"));
    connect(cancel_, &QPushButton::clicked, this, [this] {
        running_ = false;
        emit cancelled();
    });
}

void CalibOverlay::begin()
{
    oc_calibrator_start(&calib_, QDateTime::currentMSecsSinceEpoch());
    running_ = true;
    start_->hide();
    title_->hide();
    instruction_->hide();
    update();
}

void CalibOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutWidgets();
}

void CalibOverlay::layoutWidgets()
{
    const int w = width(), h = height();
    title_->setGeometry(w / 6, h / 2 - 150, w * 2 / 3, 40);
    instruction_->setGeometry(w / 6, h / 2 - 100, w * 2 / 3, 130);
    start_->setGeometry(w / 2 - 70, h / 2 + 60, 140, 48);

    /* Cancel sits at the bottom edge, centred.
     *
     * It used to sit beside Start in the middle of the screen, which is also
     * where the FIRST calibration dot appears -- so the moment the run began
     * the patient was asked to stare at a point occupied by a button. Bottom
     * centre is the one place clear of all five dots: the corners are at 8%
     * and 92% horizontally, the centre dot is halfway up. */
    cancel_->setGeometry(w / 2 - 70, h - 62, 140, 44);

    /* Moved up out of the way of Cancel. */
    face_hint_->setGeometry(w / 6, static_cast<int>(h * 0.06) + 72, w * 2 / 3, 30);
}

void CalibOverlay::onFrame(double nose_x, double nose_y, bool face, qint64 now_ms)
{
    face_ = face;
    if (!running_) {
        face_hint_->setText(face ? QStringLiteral("Face detected")
                                 : QStringLiteral("No face detected — sit in view of the camera"));
        return;
    }

    const oc_calib_progress p = oc_calibrator_update(&calib_, nose_x, nose_y, face, now_ms);
    progress_ = p.progress;
    countdown_ = p.countdown;

    face_hint_->setText(face ? QStringLiteral("Point %1 of %2")
                                   .arg(calib_.step + 1).arg(OC_CALIB_POINTS)
                             : QStringLiteral("Face lost — this point will be measured again"));

    if (p.finished) {
        running_ = false;
        if (calib_.phase == OC_CALIB_DONE) {
            emit completed(calib_.result);
        } else {
            /* Too little movement to trust. Say so and offer another go rather
             * than accepting bounds that could map the patient's gaze to the
             * wrong side of the screen. */
            title_->setText(QStringLiteral("Not enough movement"));
            instruction_->setText(QStringLiteral(
                "Your head barely moved between the dots, so the corners cannot be told "
                "apart reliably.\n\nTry again, turning your head further towards each dot — "
                "or stay with the uncalibrated tracking, which still works."));
            title_->show();
            instruction_->show();
            start_->setText(QStringLiteral("Try again"));
            start_->show();
        }
    }
    update();
}

void CalibOverlay::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.fillRect(rect(), QColor(6, 9, 18, 246));

    if (!running_) return;

    const oc_calib_target *target = oc_calib_target_at(calib_.step);
    const QPointF centre(width() * target->x_pct / 100.0, height() * target->y_pct / 100.0);

    /* A ring that closes as the point is measured, so progress is visible at the
     * dot the patient is already looking at rather than somewhere else. */
    const bool sampling = calib_.phase == OC_CALIB_SAMPLE;
    const bool confirmed = calib_.phase == OC_CALIB_CONFIRM;

    QColor colour = confirmed ? QColor(73, 224, 139)
                              : (sampling ? QColor(110, 168, 255) : QColor(148, 163, 184));

    g.setPen(QPen(QColor(colour.red(), colour.green(), colour.blue(), 70), 5));
    g.drawEllipse(centre, 46, 46);

    if (sampling || confirmed) {
        g.setPen(QPen(colour, 5, Qt::SolidLine, Qt::RoundCap));
        const int span = static_cast<int>((confirmed ? 1.0 : progress_) * 360 * 16);
        g.drawArc(QRectF(centre.x() - 46, centre.y() - 46, 92, 92), 90 * 16, -span);
    }

    g.setBrush(colour);
    g.setPen(Qt::NoPen);
    g.drawEllipse(centre, confirmed ? 20 : 14, confirmed ? 20 : 14);

    g.setPen(QColor(230, 237, 247));
    QFont f = g.font();

    if (calib_.phase == OC_CALIB_SETTLE && countdown_ > 0) {
        f.setPointSize(40);
        f.setBold(true);
        g.setFont(f);

        /* Below the dot normally, above it for the bottom two.
         *
         * Fixed at "below" the countdown for the bottom-left and bottom-right
         * dots was drawn past the edge of the screen: those sit at 92% of the
         * height, and the number needs another hundred pixels. The patient was
         * being counted down by a number they could not see. Clamped
         * horizontally for the same reason. */
        const double kOffset = 56, kBoxW = 120, kBoxH = 60;
        const bool below = centre.y() + kOffset + kBoxH < height();
        const double y = below ? centre.y() + kOffset : centre.y() - kOffset - kBoxH;
        const double x = qBound(8.0, centre.x() - kBoxW / 2, width() - kBoxW - 8.0);
        g.drawText(QRectF(x, y, kBoxW, kBoxH), Qt::AlignCenter, QString::number(countdown_));
    }

    f.setPointSize(17);
    f.setBold(false);
    g.setFont(f);
    g.drawText(QRectF(0, height() * 0.06, width(), 40), Qt::AlignCenter,
               QString::fromUtf8(target->label));

    f.setPointSize(13);
    g.setFont(f);
    g.setPen(QColor(125, 139, 166));
    g.drawText(QRectF(0, height() * 0.06 + 40, width(), 28), Qt::AlignCenter,
               sampling ? QStringLiteral("Hold still — measuring")
                        : (confirmed ? QStringLiteral("Got it")
                                     : QStringLiteral("Move your head to the dot")));
}
