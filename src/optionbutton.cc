/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "optionbutton.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace {
const QColor kIdleFill     (22,  32,  58);
const QColor kIdleEdge     (34,  49,  84);
const QColor kActiveFill   (35,  64, 126);
const QColor kActiveEdge   (110, 168, 255);
const QColor kCommitFill   ( 28, 122,  70);
const QColor kCommitEdge   ( 73, 224, 139);
const QColor kText         (233, 240, 250);
const QColor kNumber       (125, 139, 166);
}

OptionButton::OptionButton(int index, QWidget *parent)
    : QAbstractButton(parent), index_(index)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void OptionButton::setHighlighted(bool on)
{
    if (highlighted_ == on) return;
    highlighted_ = on;
    update();
}

void OptionButton::setCommitted(bool on)
{
    if (committed_ == on) return;
    committed_ = on;
    update();
}

void OptionButton::setHoldProgress(double progress)
{
    const double clamped = progress < 0 ? -1.0 : qBound(0.0, progress, 1.0);
    /* Repainting on every frame for an unchanged value would redraw all four
     * buttons thirty times a second for nothing. */
    if (qFuzzyCompare(hold_ + 2.0, clamped + 2.0)) return;
    hold_ = clamped;
    update();
}

void OptionButton::setShowNumber(bool on)
{
    if (show_number_ == on) return;
    show_number_ = on;
    update();
}

QRectF OptionButton::textBox() const
{
    return QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5).adjusted(28, 34, -28, -34);
}

double OptionButton::fitPointSize(const QRectF &box) const
{
    QFont label = font();
    label.setWeight(QFont::DemiBold);
    for (qreal pt = qBound(16.0, height() * 0.16, 44.0); pt > 12.0; pt -= 1.0) {
        label.setPointSizeF(pt);
        const QRectF needed = QFontMetrics(label).boundingRect(
            box.toRect(), Qt::AlignCenter | Qt::TextWordWrap, text());
        if (needed.height() <= box.height() && needed.width() <= box.width()) return pt;
    }
    return 12.0;
}

double OptionButton::fittingPointSize() const
{
    return fitPointSize(textBox());
}

void OptionButton::setUniformPointSize(double pt)
{
    if (qFuzzyCompare(forced_pt_ + 1.0, pt + 1.0)) return;
    forced_pt_ = pt;
    update();
}

void OptionButton::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.setRenderHint(QPainter::TextAntialiasing);

    const QRectF box = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    const qreal radius = 20.0;

    const QColor fill = committed_ ? kCommitFill : (highlighted_ ? kActiveFill : kIdleFill);
    const QColor edge = committed_ ? kCommitEdge : (highlighted_ ? kActiveEdge : kIdleEdge);

    QPainterPath shape;
    shape.addRoundedRect(box, radius, radius);
    g.fillPath(shape, fill);

    /* The highlighted option carries a heavier border. It has to be obvious at
     * a glance and from across a room which answer a blink would choose. */
    g.setPen(QPen(edge, highlighted_ || committed_ ? 4.0 : 2.0));
    g.drawPath(shape);

    if (show_number_) {
        QFont number = font();
        number.setPointSizeF(qMax(11.0, height() * 0.075));
        number.setBold(true);
        g.setFont(number);
        g.setPen(highlighted_ ? kActiveEdge : kNumber);
        g.drawText(box.adjusted(18, 12, -18, -12), Qt::AlignTop | Qt::AlignLeft,
                   QString::number(index_ + 1));
    }

    /* Sized to the box rather than fixed, so "Yes" and "I would like to sit up
     * please" both fill the button -- but at the size the window chose for all
     * four, never this button's own best fit. */
    const QRectF tb = textBox();
    QFont label = font();
    label.setWeight(QFont::DemiBold);
    label.setPointSizeF(forced_pt_ > 0.0 ? forced_pt_ : fitPointSize(tb));
    g.setFont(label);
    g.setPen(kText);
    g.drawText(tb, Qt::AlignCenter | Qt::TextWordWrap, text());

    if (hold_ < 0.0) return;

    /* The hold bar runs along the bottom edge, inside the border, growing left
     * to right. It only ever appears once the closure has passed the
     * short-blink window, which is what makes it a promise rather than a
     * progress bar: what it fills towards is this answer being spoken. */
    const qreal barHeight = 10.0;
    const QRectF track(box.left() + radius * 0.6,
                       box.bottom() - barHeight - 12.0,
                       box.width() - radius * 1.2, barHeight);

    QPainterPath trackPath;
    trackPath.addRoundedRect(track, barHeight / 2, barHeight / 2);
    g.fillPath(trackPath, QColor(255, 255, 255, 28));

    QRectF filled = track;
    filled.setWidth(qMax(barHeight, track.width() * hold_));
    QPainterPath fillPath;
    fillPath.addRoundedRect(filled, barHeight / 2, barHeight / 2);
    g.fillPath(fillPath, hold_ >= 1.0 ? kCommitEdge : QColor(255, 255, 255, 235));
}
