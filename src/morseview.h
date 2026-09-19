/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Morse, drawn rather than typeset.
 *
 * The first version of this screen wrote dots and dashes as text, using the
 * middle dot and en dash. Those are punctuation: a middle dot set at 42px is
 * still a two-pixel speck, and an en dash is a hairline. The one thing on the
 * screen that changes with every blink -- what the patient is entering right
 * now -- was the smallest and faintest thing on it, while an empty message box
 * took up a third of the window.
 *
 * So the symbols are painted. A dot is a filled circle and a dash is a bar
 * several times its width, both sized from the widget rather than from a font,
 * which also means the chart and the live readout cannot drift apart.
 */
#ifndef OPENNCOMM_MORSEVIEW_H
#define OPENNCOMM_MORSEVIEW_H

#include <QString>
#include <QWidget>

class QPainter;

namespace morse {

/* Paint `code` (a string of '.' and '-') into `into`, vertically centred and
 * left-aligned, at the given unit size. Returns the width used, so a caller
 * can centre it. `preview_from` is the index of the first symbol that is still
 * being held -- drawn hollow, because it is not committed yet.
 *
 * Returns the width the code occupies; pass a null painter to measure without
 * drawing. */
double paintCode(QPainter *g, const QRectF &into, const QString &code, double unit,
                 const QColor &colour, int preview_from = -1);

double codeWidth(const QString &code, double unit);

} // namespace morse

/* What the patient is entering, and what it can still become. The largest
 * thing on the morse screen, because it is the only part that moves. */
class MorseEntry : public QWidget {
    Q_OBJECT
public:
    explicit MorseEntry(QWidget *parent = nullptr);

    /* `code` is the committed symbols plus, if `has_preview`, one more being
     * held. `exact` is the letter it decodes to now, or 0. `candidates` is
     * every letter still reachable. */
    void setEntry(const QString &code, bool has_preview, QChar exact,
                  const QString &candidates);

    QSize sizeHint() const override { return QSize(600, 210); }
    QSize minimumSizeHint() const override { return QSize(320, 150); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString code_;
    bool has_preview_ = false;
    QChar exact_;
    QString candidates_;
};

/* One letter of the reference chart: the character and its code, in the same
 * shapes the live readout uses. */
class MorseKey : public QWidget {
    Q_OBJECT
public:
    enum State { Idle, Dim, Exact };

    MorseKey(QChar letter, const QString &code, QWidget *parent = nullptr);

    void setState(State state);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(76, 34); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QChar letter_;
    QString code_;
    State state_ = Idle;
};

#endif
