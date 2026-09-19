/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* One of the four answers the patient chooses between.
 *
 * Painted rather than styled, for two reasons. The text has to fill whatever
 * space the window gives it -- these are read from a bed, at an angle, often by
 * someone whose sight is not what it was -- so the font size is computed from
 * the widget, not fixed in a stylesheet. And the hold indicator has to be drawn
 * per frame against a value coming from the blink tracker, which no stylesheet
 * can express.
 *
 * The hold indicator is not decoration. It appears only once a closure passes
 * the short-blink threshold, so the bar appearing IS the signal: keep holding
 * and this answer will be spoken. Without it the patient has to hold and hope.
 */
#ifndef OPENNCOMM_OPTIONBUTTON_H
#define OPENNCOMM_OPTIONBUTTON_H

#include <QAbstractButton>
#include <QRectF>

class OptionButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit OptionButton(int index, QWidget *parent = nullptr);

    void setHighlighted(bool on);
    void setCommitted(bool on);
    /* 0..1 fills the bar; anything negative hides it. */
    void setHoldProgress(double progress);
    /* Head mode points at quadrants, so the number tells the patient which
     * corner is which. Blink mode scans in order and does not need it. */
    void setShowNumber(bool on);

    /* The four answers must be set in ONE size, or a short answer next to a
     * long one looks like it is being shouted. The window finds the largest
     * size that fits every button and applies it to all of them. */
    double fittingPointSize() const;
    void setUniformPointSize(double pt);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override { return QSize(320, 200); }

private:
    int index_;
    bool highlighted_ = false;
    bool committed_ = false;
    bool show_number_ = false;
    double hold_ = -1.0;
    double forced_pt_ = 0.0;

    QRectF textBox() const;
    double fitPointSize(const QRectF &box) const;
};

#endif
