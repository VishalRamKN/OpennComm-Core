/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Reading back what the patient has said.
 *
 * Written for whoever is sitting with them: a relative, or the nurse who has
 * just come on shift and wants to know what the last one heard. So it is
 * grouped by day, newest first, and says nothing about intents, models or
 * confidence -- only the time, the question and the answer.
 *
 * Nothing here can speak. The mouse lost that ability deliberately, and a
 * history view full of replayable sentences would hand it straight back.
 */
#ifndef OPENNCOMM_HISTORYDIALOG_H
#define OPENNCOMM_HISTORYDIALOG_H

#include <QDialog>

#include "history.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class HistoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit HistoryDialog(History *history, QWidget *parent = nullptr);

private:
    void reload();
    void onDelete();
    void onClear();
    void onExport();
    QList<qint64> selectedIds() const;

    History *history_;

    QLineEdit *search_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QLabel *summary_ = nullptr;
    QPushButton *delete_ = nullptr;
    QPushButton *export_ = nullptr;
    QPushButton *clear_ = nullptr;
};

#endif
