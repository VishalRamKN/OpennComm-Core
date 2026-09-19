/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "nosepointer.h"

#include <QPainter>
#include <QPaintEvent>

NosePointer::NosePointer(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void NosePointer::setPosition(double x, double y, bool tracked)
{
    if (qFuzzyCompare(x_, x) && qFuzzyCompare(y_, y) && tracked_ == tracked) return;
    x_ = x;
    y_ = y;
    tracked_ = tracked;
    update();
}

void NosePointer::paintEvent(QPaintEvent *)
{
    if (!tracked_) return;

    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);

    /* Kept a ring's width inside the edges. The position is already clamped to
     * the screen, so a patient who turns further than they calibrated pins the
     * pointer to the edge -- and a pointer half cut off by the edge reads as a
     * drawing bug rather than as "you are at the limit". */
    constexpr double kRadius = 16.0, kMargin = 22.0;
    const QPointF at(qBound(kMargin, x_ * width(), width() - kMargin),
                     qBound(kMargin, y_ * height(), height() - kMargin));

    /* A ring rather than a filled disc: it sits on top of the answer text, and
     * a solid dot would cover the very words it is helping the patient pick. */
    g.setPen(QPen(QColor(10, 14, 24, 190), 6));
    g.setBrush(Qt::NoBrush);
    g.drawEllipse(at, kRadius, kRadius);

    g.setPen(QPen(QColor(110, 168, 255, 235), 3));
    g.drawEllipse(at, kRadius, kRadius);

    g.setPen(Qt::NoPen);
    g.setBrush(QColor(110, 168, 255, 235));
    g.drawEllipse(at, 4, 4);
}
