/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Where the patient is pointing, in head mode.
 *
 * Without it, head mode gives only the highlighted button: the patient knows
 * which answer is selected but not how close they are to the edge of it, so a
 * highlight that keeps flicking between two options looks like the tracking is
 * broken rather than like they are aiming at the line between them.
 *
 * The position comes from oc_gaze_position(), which is the same arithmetic
 * oc_gaze_quadrant() reduces to a quadrant. That sharing is deliberate and
 * tested: a patient trusts the pointer, so a pointer that disagreed with the
 * highlighted button would be worse than no pointer at all.
 *
 * Transparent to the mouse, and shown only in head mode.
 */
#ifndef OPENNCOMM_NOSEPOINTER_H
#define OPENNCOMM_NOSEPOINTER_H

#include <QWidget>

class NosePointer : public QWidget {
    Q_OBJECT
public:
    explicit NosePointer(QWidget *parent = nullptr);

    /* x and y are 0..1 across the area. `tracked` is false when the face has
     * been lost, which fades the pointer rather than freezing it somewhere
     * misleading. */
    void setPosition(double x, double y, bool tracked);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double x_ = 0.5, y_ = 0.5;
    bool tracked_ = false;
};

#endif
