/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "morseview.h"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>

namespace {

/* A dash is this many dot-widths long. Real morse uses three; three looks
 * cramped at small sizes, and this is a legend rather than a transmission. */
constexpr double kDashUnits = 3.2;
/* Gap between symbols, in dot-widths. */
constexpr double kGapUnits = 0.85;

const QColor kAccent(110, 168, 255);
const QColor kMuted(125, 139, 166);
const QColor kIdleText(178, 194, 220);

} // namespace

namespace morse {

double codeWidth(const QString &code, double unit)
{
    double w = 0;
    for (int i = 0; i < code.size(); i++) {
        w += (code[i] == QLatin1Char('-')) ? unit * kDashUnits : unit;
        if (i + 1 < code.size()) w += unit * kGapUnits;
    }
    return w;
}

double paintCode(QPainter *g, const QRectF &into, const QString &code, double unit,
                 const QColor &colour, int preview_from)
{
    const double width = codeWidth(code, unit);
    if (!g || code.isEmpty()) return width;

    const double y = into.center().y();
    double x = into.left();

    g->save();
    g->setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < code.size(); i++) {
        const bool dash = code[i] == QLatin1Char('-');
        const double w = dash ? unit * kDashUnits : unit;

        /* A symbol still being held is hollow. The patient can see that the
         * blink has been noticed without being told it has been counted. */
        const bool held = preview_from >= 0 && i >= preview_from;
        if (held) {
            g->setBrush(Qt::NoBrush);
            g->setPen(QPen(colour, qMax(1.5, unit * 0.16)));
        } else {
            g->setBrush(colour);
            g->setPen(Qt::NoPen);
        }

        const double inset = held ? qMax(0.75, unit * 0.08) : 0.0;
        const QRectF r(x + inset, y - unit / 2 + inset,
                       w - inset * 2, unit - inset * 2);
        g->drawRoundedRect(r, unit / 2, unit / 2);

        x += w + unit * kGapUnits;
    }
    g->restore();
    return width;
}

} // namespace morse

MorseEntry::MorseEntry(QWidget *parent) : QWidget(parent) {}

void MorseEntry::setEntry(const QString &code, bool has_preview, QChar exact,
                          const QString &candidates)
{
    if (code == code_ && has_preview == has_preview_ && exact == exact_
        && candidates == candidates_)
        return;
    code_ = code;
    has_preview_ = has_preview;
    exact_ = exact;
    candidates_ = candidates;
    update();
}

void MorseEntry::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);

    const QRectF box = rect().adjusted(1, 1, -1, -1);
    g.setPen(QPen(QColor(42, 58, 94), 2));
    g.setBrush(QColor(15, 22, 38));
    g.drawRoundedRect(box, 16, 16);

    /* The symbols get the top two thirds, the letter the rest. Sized from the
     * widget so the same layout works in a window and on a full screen. */
    const double unit = qBound(14.0, qMin(box.height() * 0.17, box.width() / 26.0), 34.0);
    const QRectF symbols(box.left(), box.top() + box.height() * 0.12,
                         box.width(), box.height() * 0.34);

    if (code_.isEmpty()) {
        /* Rest has to look like rest, so this is a legend rather than an empty
         * box: one dot, one dash, and what each is worth. */
        const double legend_unit = unit * 0.8;
        const double dot_w = morse::codeWidth(QStringLiteral("."), legend_unit);
        const double dash_w = morse::codeWidth(QStringLiteral("-"), legend_unit);
        const double gap = legend_unit * 4.5;
        const double total = dot_w + gap + dash_w;
        double x = symbols.center().x() - total / 2;

        morse::paintCode(&g, QRectF(x, symbols.top(), dot_w, symbols.height()),
                         QStringLiteral("."), legend_unit, QColor(60, 76, 110));
        morse::paintCode(&g, QRectF(x + dot_w + gap, symbols.top(), dash_w, symbols.height()),
                         QStringLiteral("-"), legend_unit, QColor(60, 76, 110));

        /* The two labels sit directly under their shapes and the invitation
         * well below both. Measured from the same box, because the first
         * version derived them from three different fractions of the height
         * and they collided. */
        QFont f = g.font();
        f.setPointSizeF(qMax(10.0, unit * 0.52));
        g.setFont(f);
        g.setPen(QColor(92, 107, 136));
        const QRectF under(box.left(), box.top() + box.height() * 0.46,
                           box.width(), box.height() * 0.14);
        g.drawText(QRectF(x - legend_unit * 1.6, under.top(), dot_w + legend_unit * 3.2, under.height()),
                   Qt::AlignCenter, QStringLiteral("short"));
        g.drawText(QRectF(x + dot_w + gap - legend_unit * 1.2, under.top(),
                          dash_w + legend_unit * 2.4, under.height()),
                   Qt::AlignCenter, QStringLiteral("long"));

        f.setPointSizeF(qMax(12.0, unit * 0.66));
        g.setFont(f);
        g.setPen(QColor(107, 122, 150));
        g.drawText(QRectF(box.left(), box.top() + box.height() * 0.70,
                          box.width(), box.height() * 0.22),
                   Qt::AlignCenter, QStringLiteral("blink to start spelling"));
        return;
    }

    const int preview_from = has_preview_ ? code_.size() - 1 : -1;
    const double width = morse::codeWidth(code_, unit);
    morse::paintCode(&g, QRectF(symbols.center().x() - width / 2, symbols.top(),
                                width, symbols.height()),
                     code_, unit, kAccent, preview_from);

    /* What it is, right now. Big, because this is the answer to the only
     * question the patient has while spelling. */
    const QRectF letter_box(box.left(), box.top() + box.height() * 0.54,
                            box.width(), box.height() * 0.30);
    QFont f = g.font();
    f.setBold(true);
    f.setPointSizeF(qMax(20.0, letter_box.height() * 0.72));
    g.setFont(f);

    if (!exact_.isNull() && exact_ != QChar('\0')) {
        /* Dimmer while a blink is still being held: this is what the letter
         * WOULD be, not what it is. Full brightness is reserved for symbols
         * the patient has actually finished. */
        g.setPen(has_preview_ ? QColor(150, 170, 200) : QColor(233, 240, 250));
        g.drawText(letter_box, Qt::AlignCenter, QString(exact_));
    } else if (candidates_.isEmpty()) {
        f.setPointSizeF(qMax(13.0, letter_box.height() * 0.34));
        f.setBold(false);
        g.setFont(f);
        g.setPen(QColor(224, 138, 110));
        g.drawText(letter_box, Qt::AlignCenter,
                   QStringLiteral("not a letter — pause to clear"));
    } else {
        f.setPointSizeF(qMax(16.0, letter_box.height() * 0.5));
        g.setFont(f);
        g.setPen(kMuted);
        g.drawText(letter_box, Qt::AlignCenter, QStringLiteral("–"));
    }

    /* Still reachable. Not the same as "what you have typed": morse is not
     * prefix-free, so this narrowing set is the only warning that a pause now
     * would commit the wrong letter. */
    if (!candidates_.isEmpty()) {
        f.setBold(false);
        f.setPointSizeF(qMax(11.0, box.height() * 0.075));
        f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
        g.setFont(f);
        g.setPen(kMuted);
        const QString label = candidates_.size() == 1
            ? QStringLiteral("only letter left")
            : QStringLiteral("could still be:  ") + candidates_;
        g.drawText(QRectF(box.left(), box.top() + box.height() * 0.84,
                          box.width(), box.height() * 0.14),
                   Qt::AlignCenter, label);
    }
}

MorseKey::MorseKey(QChar letter, const QString &code, QWidget *parent)
    : QWidget(parent), letter_(letter), code_(code)
{
}

void MorseKey::setState(State state)
{
    if (state_ == state) return;
    state_ = state;
    update();
}

QSize MorseKey::sizeHint() const
{
    /* Wide enough for the longest code in the table plus its letter. */
    return QSize(int(26 + morse::codeWidth(QStringLiteral("-----"), 7.0)) + 22, 40);
}

void MorseKey::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);

    QColor background, text, symbol;
    switch (state_) {
    case Exact:
        background = QColor(35, 64, 126); text = QColor(255, 255, 255); symbol = QColor(174, 205, 255);
        break;
    case Dim:
        background = QColor(14, 21, 36); text = QColor(52, 62, 84); symbol = QColor(45, 55, 76);
        break;
    case Idle:
    default:
        background = QColor(19, 28, 48); text = kIdleText; symbol = kMuted;
        break;
    }

    const QRectF box = rect().adjusted(1, 1, -1, -1);
    g.setPen(Qt::NoPen);
    g.setBrush(background);
    g.drawRoundedRect(box, 7, 7);

    QFont f = g.font();
    f.setPointSizeF(qMax(11.0, box.height() * 0.40));
    f.setBold(state_ == Exact);
    g.setFont(f);
    g.setPen(text);

    const double letter_w = box.height() * 0.85;
    g.drawText(QRectF(box.left() + 6, box.top(), letter_w, box.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QString(letter_));

    const double unit = qBound(4.0, box.height() * 0.19, 9.0);
    const double available = box.width() - letter_w - 14;
    double scaled = unit;
    while (scaled > 3.0 && morse::codeWidth(code_, scaled) > available) scaled -= 0.4;

    const double width = morse::codeWidth(code_, scaled);
    morse::paintCode(&g, QRectF(box.right() - width - 8, box.top(), width, box.height()),
                     code_, scaled, symbol);
}
