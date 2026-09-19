/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "historydialog.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

/* "Today" and "Yesterday" rather than a date, because that is how someone on a
 * ward talks about it. */
QString dayLabel(const QDate &date)
{
    const QDate today = QDate::currentDate();
    if (date == today) return QStringLiteral("Today");
    if (date == today.addDays(-1)) return QStringLiteral("Yesterday");
    return date.toString(QStringLiteral("dddd d MMMM yyyy"));
}

QString howLabel(const Exchange &e)
{
    if (e.mode == QLatin1String("morse")) return QStringLiteral("spelled");
    if (e.mode == QLatin1String("head"))  return QStringLiteral("head");
    return QStringLiteral("blink");
}

} // namespace

HistoryDialog::HistoryDialog(History *history, QWidget *parent)
    : QDialog(parent), history_(history)
{
    setWindowTitle(QStringLiteral("History"));
    setMinimumSize(880, 560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(12);

    auto *heading = new QLabel(QStringLiteral("What has been said"), this);
    heading->setObjectName(QStringLiteral("status"));
    root->addWidget(heading);

    auto *note = new QLabel(
        QStringLiteral("Kept on this computer only, and never sent anywhere. "
                       "These are the patient's own words — treat them as you would "
                       "anything else in their notes."),
        this);
    note->setObjectName(QStringLiteral("sectionHint"));
    note->setWordWrap(true);
    root->addWidget(note);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("Search what was asked or answered…"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, &HistoryDialog::reload);
    root->addWidget(search_);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({ QStringLiteral("Time"), QStringLiteral("Asked"),
                             QStringLiteral("Answered"), QStringLiteral("How") });
    tree_->setRootIsDecorated(false);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setAlternatingRowColors(false);
    tree_->setUniformRowHeights(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    root->addWidget(tree_, 1);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("sectionHint"));

    delete_ = new QPushButton(QStringLiteral("Delete selected"), this);
    delete_->setMinimumHeight(38);
    delete_->setEnabled(false);
    connect(delete_, &QPushButton::clicked, this, &HistoryDialog::onDelete);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this,
            [this] { delete_->setEnabled(!selectedIds().isEmpty()); });

    export_ = new QPushButton(QStringLiteral("Save as text…"), this);
    export_->setMinimumHeight(38);
    export_->setToolTip(QStringLiteral("For a handover note. It writes a file on this computer."));
    connect(export_, &QPushButton::clicked, this, &HistoryDialog::onExport);

    clear_ = new QPushButton(QStringLiteral("Delete everything…"), this);
    clear_->setObjectName(QStringLiteral("exitfs"));
    clear_->setMinimumHeight(38);
    connect(clear_, &QPushButton::clicked, this, &HistoryDialog::onClear);

    auto *close = new QPushButton(QStringLiteral("Done"), this);
    close->setObjectName(QStringLiteral("primary"));
    close->setMinimumHeight(38);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(10);
    footer->addWidget(summary_);
    footer->addStretch(1);
    footer->addWidget(delete_);
    footer->addWidget(export_);
    footer->addWidget(clear_);
    footer->addWidget(close);
    root->addLayout(footer);

    reload();
}

void HistoryDialog::reload()
{
    tree_->clear();

    if (!history_ || !history_->available()) {
        summary_->setText(QStringLiteral("History is unavailable — %1")
                              .arg(history_ ? history_->error() : QStringLiteral("not opened")));
        delete_->setEnabled(false);
        export_->setEnabled(false);
        clear_->setEnabled(false);
        return;
    }

    const QString filter = search_->text();
    const QList<Exchange> rows = history_->recent(2000, filter);

    QTreeWidgetItem *day = nullptr;
    QDate day_date;
    for (const Exchange &e : rows) {
        const QDateTime when = QDateTime::fromMSecsSinceEpoch(e.asked_at);
        if (!day || when.date() != day_date) {
            day_date = when.date();
            day = new QTreeWidgetItem(tree_, { dayLabel(day_date) });
            day->setFirstColumnSpanned(true);
            /* A heading, not a row: it has no id and must not be deletable. */
            day->setFlags(Qt::ItemIsEnabled);
            QFont bold = day->font(0);
            bold.setBold(true);
            day->setFont(0, bold);
            day->setExpanded(true);
        }

        auto *item = new QTreeWidgetItem(day, {
            when.toString(QStringLiteral("HH:mm")),
            e.question.isEmpty() ? QStringLiteral("—") : e.question,
            e.answer,
            howLabel(e),
        });
        item->setData(0, kIdRole, e.id);
        item->setToolTip(1, e.question);
        item->setToolTip(2, e.answer);
    }

    const int total = history_->count();
    if (rows.isEmpty() && !filter.trimmed().isEmpty())
        summary_->setText(QStringLiteral("Nothing matching “%1”, of %2 kept.")
                              .arg(filter.trimmed()).arg(total));
    else if (rows.isEmpty())
        summary_->setText(QStringLiteral("Nothing said yet."));
    else if (rows.size() == total)
        summary_->setText(QStringLiteral("%1 answered, all of them kept here.").arg(total));
    else
        summary_->setText(QStringLiteral("Showing %1 of %2.").arg(rows.size()).arg(total));

    delete_->setEnabled(false);
}

QList<qint64> HistoryDialog::selectedIds() const
{
    QList<qint64> ids;
    for (QTreeWidgetItem *item : tree_->selectedItems()) {
        const QVariant id = item->data(0, kIdRole);
        if (id.isValid()) ids << id.toLongLong();
    }
    return ids;
}

void HistoryDialog::onDelete()
{
    const QList<qint64> ids = selectedIds();
    if (ids.isEmpty()) return;

    const QString question = ids.size() == 1
        ? QStringLiteral("Delete this one answer?")
        : QStringLiteral("Delete these %1 answers?").arg(ids.size());

    if (QMessageBox::question(this, QStringLiteral("Delete"),
                              question + QStringLiteral("\n\nThis cannot be undone."),
                              QMessageBox::Cancel | QMessageBox::Yes,
                              QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    for (qint64 id : ids) history_->remove(id);
    reload();
}

void HistoryDialog::onClear()
{
    const int total = history_->count();
    if (total == 0) return;

    /* Spelled out rather than "are you sure": this deletes a record of what a
     * person who cannot speak managed to say, and the number is the point. */
    if (QMessageBox::question(
            this, QStringLiteral("Delete everything"),
            QStringLiteral("Delete all %1 answers?\n\nThis erases everything the patient has "
                           "said on this computer. It cannot be undone, and there is no copy "
                           "anywhere else.").arg(total),
            QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    if (!history_->clear())
        QMessageBox::warning(this, QStringLiteral("Delete everything"),
                             QStringLiteral("Some of it could not be deleted: %1")
                                 .arg(history_->error()));
    reload();
}

void HistoryDialog::onExport()
{
    const QList<Exchange> rows = history_->recent(5000, search_->text());
    if (rows.isEmpty()) return;

    const QString suggested = QStringLiteral("openncomm-history-%1.txt")
                                  .arg(QDate::currentDate().toString(Qt::ISODate));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save history"), suggested,
        QStringLiteral("Text files (*.txt)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Save history"),
                             QStringLiteral("Could not write %1.").arg(path));
        return;
    }
    QTextStream out(&file);
    out << History::asText(rows);
    file.close();

    summary_->setText(QStringLiteral("Saved %1 to %2.").arg(rows.size()).arg(path));
}
