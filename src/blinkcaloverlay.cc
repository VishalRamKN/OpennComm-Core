/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "blinkcaloverlay.h"

#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>

namespace {
const char *kButton =
    "background:#1b2740; border:1px solid #2c3d63; border-radius:9px;"
    "padding:12px 26px; color:#b9c8e4;";
const char *kPrimary =
    "background:#23407e; border:1px solid #6ea8ff; border-radius:9px;"
    "padding:12px 34px; font-size:17px; color:#e6edf7;";
}

BlinkCalOverlay::BlinkCalOverlay(QWidget *parent) : QWidget(parent)
{
    oc_blinkcal_init(&cal_);
    setAutoFillBackground(false);

    title_ = new QLabel(QStringLiteral("Set up blinks"), this);
    title_->setStyleSheet(QStringLiteral("font-size:28px; font-weight:700; color:#e6edf7;"));
    title_->setAlignment(Qt::AlignCenter);

    instruction_ = new QLabel(this);
    instruction_->setWordWrap(true);
    instruction_->setAlignment(Qt::AlignCenter);
    instruction_->setStyleSheet(QStringLiteral("font-size:19px; color:#b9c8e4;"));
    instruction_->setText(QStringLiteral(
        "Every patient blinks differently, so the application measures this one.\n"
        "You will be asked for four ordinary blinks, then four held blinks.\n\n"
        "Blink the way the patient actually will — not slower to be helpful. "
        "Takes under a minute."));

    hint_ = new QLabel(this);
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setStyleSheet(QStringLiteral("font-size:15px; color:#7d8ba6;"));

    start_ = new QPushButton(QStringLiteral("Start"), this);
    start_->setStyleSheet(QString::fromLatin1(kPrimary));
    connect(start_, &QPushButton::clicked, this, &BlinkCalOverlay::begin);

    cancel_ = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_->setStyleSheet(QString::fromLatin1(kButton));
    connect(cancel_, &QPushButton::clicked, this, [this] {
        running_ = false;
        emit cancelled();
    });

    undo_ = new QPushButton(QStringLiteral("Undo last blink"), this);
    undo_->setStyleSheet(QString::fromLatin1(kButton));
    undo_->hide();
    connect(undo_, &QPushButton::clicked, this, [this] {
        if (oc_blinkcal_undo(&cal_)) {
            last_ms_ = 0;
            last_rejected_ = false;
            refreshText();
            update();
        }
    });
}

void BlinkCalOverlay::begin()
{
    oc_blinkcal_start(&cal_);
    running_ = true;
    eye_closed_ = false;
    live_ms_ = last_ms_ = 0;
    last_rejected_ = false;
    start_->hide();
    title_->hide();
    instruction_->hide();
    undo_->show();
    refreshText();
    update();
}

void BlinkCalOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutWidgets();
}

void BlinkCalOverlay::layoutWidgets()
{
    const int w = width(), h = height();
    title_->setGeometry(w / 6, h / 2 - 190, w * 2 / 3, 40);
    instruction_->setGeometry(w / 6, h / 2 - 140, w * 2 / 3, 150);
    start_->setGeometry(w / 2 - 70, h / 2 + 40, 140, 48);
    /* Just under the pips rather than at the foot of the screen: this is the
     * instruction the patient is acting on, and it belongs where they are
     * already looking. */
    hint_->setGeometry(w / 6, h / 2 + 200, w * 2 / 3, 30);

    /* Out of the way in the corner. Both of these are caregiver controls and
     * the patient is looking at the middle of the screen. */
    undo_->setGeometry(24, h - 72, 190, 48);
    cancel_->setGeometry(w - 164, h - 72, 140, 48);
}

void BlinkCalOverlay::refreshText()
{
    if (cal_.phase == OC_BLINKCAL_SHORT)
        hint_->setText(QStringLiteral("Blink normally — %1 of %2")
                           .arg(cal_.short_count + 1).arg(OC_BLINKCAL_SAMPLES));
    else if (cal_.phase == OC_BLINKCAL_LONG)
        hint_->setText(QStringLiteral("Now hold each blink — %1 of %2")
                           .arg(cal_.long_count + 1).arg(OC_BLINKCAL_SAMPLES));
    else
        hint_->clear();
}

void BlinkCalOverlay::onFrame(bool blink, bool face, qint64 now_ms)
{
    if (!running_) {
        hint_->setText(face ? QStringLiteral("Face detected")
                            : QStringLiteral("No face detected — sit in view of the camera"));
        return;
    }

    /* A closure that began before the face was found has an unknown start, so
     * it is dropped rather than measured from whenever tracking resumed. */
    if (!face) {
        eye_closed_ = false;
        live_ms_ = 0;
        update();
        return;
    }

    if (blink && !eye_closed_) {
        eye_closed_ = true;
        closed_at_ = now_ms;
        live_ms_ = 0;
    } else if (blink && eye_closed_) {
        live_ms_ = static_cast<int>(now_ms - closed_at_);
    } else if (!blink && eye_closed_) {
        eye_closed_ = false;
        const int duration = static_cast<int>(now_ms - closed_at_);
        live_ms_ = 0;

        const bool accepted = oc_blinkcal_record(&cal_, duration) == 1;
        last_ms_ = duration;
        last_rejected_ = !accepted;

        if (cal_.phase == OC_BLINKCAL_DONE) {
            running_ = false;
            undo_->hide();
            emit completed(cal_.bands);
            return;
        }
        if (cal_.phase == OC_BLINKCAL_FAILED) {
            running_ = false;
            undo_->hide();
            title_->setText(QStringLiteral("Too close to tell apart"));
            instruction_->setText(QStringLiteral(
                "The held blinks were only %1 ms longer than the ordinary ones, which is "
                "not enough to tell them apart reliably.\n\n"
                "Try again, holding the long blinks for a full second — or leave blink "
                "setup and use head mode, which does not depend on blink length.")
                    .arg(cal_.measured_gap_ms));
            title_->show();
            instruction_->show();
            start_->setText(QStringLiteral("Try again"));
            start_->show();
            hint_->clear();
        }
        refreshText();
    }
    update();
}

void BlinkCalOverlay::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.fillRect(rect(), QColor(6, 9, 18, 246));

    if (!running_) return;

    const bool longs = cal_.phase == OC_BLINKCAL_LONG;
    const int done = longs ? cal_.long_count : cal_.short_count;
    const QPointF centre(width() / 2.0, height() / 2.0 - 40);

    /* One big eye-shaped indicator: filled while the eye is shut, so the
     * patient gets confirmation at the place they are already looking. */
    const QColor live = eye_closed_ ? QColor(110, 168, 255) : QColor(70, 86, 120);
    g.setPen(QPen(live, 6));
    g.setBrush(eye_closed_ ? QColor(35, 64, 126) : QColor(15, 22, 38));
    g.drawEllipse(centre, 92, 92);

    /* While a long blink is being held, a ring closes over the second that the
     * patient is being asked for. Without it "hold it" is guesswork. */
    if (longs && eye_closed_) {
        const double want = 1000.0;
        const double frac = qBound(0.0, live_ms_ / want, 1.0);
        g.setPen(QPen(QColor(73, 224, 139), 8, Qt::SolidLine, Qt::RoundCap));
        g.setBrush(Qt::NoBrush);
        g.drawArc(QRectF(centre.x() - 108, centre.y() - 108, 216, 216),
                  90 * 16, -static_cast<int>(frac * 360 * 16));
    }

    g.setPen(QColor(233, 240, 250));
    QFont f = g.font();
    f.setPointSize(30);
    f.setBold(true);
    g.setFont(f);
    g.drawText(QRectF(centre.x() - 92, centre.y() - 30, 184, 60), Qt::AlignCenter,
               eye_closed_ ? QString::number(live_ms_) + QStringLiteral(" ms")
                           : (longs ? QStringLiteral("hold") : QStringLiteral("blink")));

    /* Four pips per group, so how many are left is countable at a glance. */
    const double pip_y = centre.y() + 150;
    for (int i = 0; i < OC_BLINKCAL_SAMPLES; i++) {
        const double x = centre.x() + (i - (OC_BLINKCAL_SAMPLES - 1) / 2.0) * 44;
        g.setPen(Qt::NoPen);
        g.setBrush(i < done ? QColor(73, 224, 139) : QColor(45, 58, 88));
        g.drawEllipse(QPointF(x, pip_y), 11, 11);
    }

    if (last_ms_ > 0) {
        f.setPointSize(14);
        f.setBold(false);
        g.setFont(f);
        g.setPen(last_rejected_ ? QColor(224, 138, 110) : QColor(125, 139, 166));
        g.drawText(QRectF(centre.x() - 260, pip_y + 26, 520, 30), Qt::AlignCenter,
                   last_rejected_
                       ? QStringLiteral("%1 ms — not counted, blink again").arg(last_ms_)
                       : QStringLiteral("%1 ms").arg(last_ms_));
    }
}
