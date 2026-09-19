/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* A combo box that draws its own chevron.
 *
 * Qt style sheets can colour the drop-down well but cannot put a decent arrow
 * in it: `image:` needs a file, and the border trick that people reach for
 * instead draws an "L", because style sheets cannot rotate. Styling the well
 * and leaving the arrow alone is worse still -- the native arrow disappears
 * the moment ::drop-down is styled, leaving a button with nothing in it.
 *
 * So it is painted. Six lines, and the dropdowns stop looking like disabled
 * text fields.
 *
 * Deliberately no Q_OBJECT: nothing here adds signals or slots, and without it
 * the class still reports itself as a QComboBox, so every existing style sheet
 * rule keeps applying.
 */
#ifndef OPENNCOMM_COMBOBOX_H
#define OPENNCOMM_COMBOBOX_H

#include <QComboBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

class ComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);

        /* Matches the 32px well in the style sheet. */
        constexpr int kWell = 32, kArm = 5;
        const QPointF centre(width() - kWell / 2.0 - 1, height() / 2.0 - 1);

        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(QPen(isEnabled() ? QColor(159, 180, 216) : QColor(85, 98, 124),
                      2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g.drawPolyline(QPolygonF({ QPointF(centre.x() - kArm, centre.y() - kArm / 2.0),
                                   QPointF(centre.x(), centre.y() + kArm / 2.0),
                                   QPointF(centre.x() + kArm, centre.y() - kArm / 2.0) }));
    }
};

#endif
