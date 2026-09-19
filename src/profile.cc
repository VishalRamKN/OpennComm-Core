/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "profile.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int kMaxNoteChars = 240;   /* Prompt length is latency. */
}

PatientProfile PatientProfile::load()
{
    QSettings s;
    PatientProfile p;
    p.answered  = s.value(QStringLiteral("patient/answered"), false).toBool();
    p.name      = s.value(QStringLiteral("patient/name")).toString();
    p.pronouns  = s.value(QStringLiteral("patient/pronouns")).toString();
    p.age       = s.value(QStringLiteral("patient/age"), 0).toInt();
    p.condition = s.value(QStringLiteral("patient/condition")).toString();
    p.notes     = s.value(QStringLiteral("patient/notes")).toString();
    p.caregiver = s.value(QStringLiteral("patient/caregiver")).toString();
    return p;
}

void PatientProfile::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("patient/answered"), answered);
    s.setValue(QStringLiteral("patient/name"), name.trimmed());
    s.setValue(QStringLiteral("patient/pronouns"), pronouns.trimmed());
    s.setValue(QStringLiteral("patient/age"), age);
    s.setValue(QStringLiteral("patient/condition"), condition.trimmed());
    s.setValue(QStringLiteral("patient/notes"), notes.trimmed().left(kMaxNoteChars));
    s.setValue(QStringLiteral("patient/caregiver"), caregiver.trimmed());
}

QString PatientProfile::displayName() const
{
    const QString n = name.trimmed();
    return n.isEmpty() ? QStringLiteral("the patient") : n;
}

/* Facts on their own lines rather than a written-out paragraph. Prose would
 * mean choosing verbs and pronouns for the caregiver's words, and a 1.5B model
 * follows a short labelled list at least as well as it follows a sentence. */
QString PatientProfile::brief() const
{
    QStringList lines;
    const auto add = [&lines](const char *label, const QString &value) {
        const QString v = value.trimmed();
        if (!v.isEmpty()) lines << QLatin1String(label) + v;
    };

    add("Name: ", name);
    if (age > 0) lines << QStringLiteral("Age: %1").arg(age);
    add("Pronouns: ", pronouns);
    add("Condition: ", condition);
    add("Worth knowing: ", notes.left(kMaxNoteChars));
    add("Cared for by: ", caregiver);

    if (lines.isEmpty()) return QString();
    return QStringLiteral("About the patient:\n") + lines.join(QLatin1Char('\n'));
}

struct PatientDialog::Fields {
    QLineEdit     *name = nullptr;
    QComboBox     *pronouns = nullptr;
    QSpinBox      *age = nullptr;
    QLineEdit     *condition = nullptr;
    QPlainTextEdit *notes = nullptr;
    QLineEdit     *caregiver = nullptr;
};

PatientDialog::PatientDialog(const PatientProfile &current, QWidget *parent)
    : QDialog(parent), f(new Fields)
{
    setWindowTitle(QStringLiteral("About the patient"));
    setModal(true);
    setMinimumWidth(620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 24, 26, 20);
    root->setSpacing(14);

    auto *heading = new QLabel(QStringLiteral("Who is this for?"), this);
    heading->setObjectName(QStringLiteral("status"));
    root->addWidget(heading);

    auto *blurb = new QLabel(
        QStringLiteral(
            "These details shape the answers offered on screen. Everything is "
            "optional — skip anything you would rather not record.\n\n"
            "This stays on this computer. It is never uploaded, and nothing in "
            "OpennComm sends it anywhere."),
        this);
    blurb->setObjectName(QStringLiteral("sectionHint"));
    blurb->setWordWrap(true);
    root->addWidget(blurb);

    auto *form = new QFormLayout;
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    f->name = new QLineEdit(current.name, this);
    f->name->setPlaceholderText(QStringLiteral("What they like to be called"));
    form->addRow(QStringLiteral("Name"), f->name);

    /* Editable rather than a fixed list: three common sets are offered so the
     * usual case is one click, and anything else can simply be typed. */
    f->pronouns = new QComboBox(this);
    f->pronouns->setEditable(true);
    f->pronouns->addItems({ QStringLiteral("they/them"), QStringLiteral("she/her"),
                            QStringLiteral("he/him") });
    /* Index -1, not the first item: an untouched box must stay genuinely
     * empty. Pre-filling it would record a guess as though it had been
     * answered, which is the one thing this field exists to avoid. */
    if (current.pronouns.trimmed().isEmpty())
        f->pronouns->setCurrentIndex(-1);
    else
        f->pronouns->setCurrentText(current.pronouns);
    f->pronouns->lineEdit()->setPlaceholderText(QStringLiteral("they/them"));
    /* The combo and the spin box both ask for more room than a line edit of
     * the same text, which pushes them past the dialog margin. */
    f->pronouns->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    f->pronouns->setMinimumContentsLength(12);
    form->addRow(QStringLiteral("Pronouns"), f->pronouns);

    f->age = new QSpinBox(this);
    f->age->setRange(0, 120);
    f->age->setValue(current.age);
    f->age->setSpecialValueText(QStringLiteral("—"));   /* 0 reads as blank. */
    form->addRow(QStringLiteral("Age"), f->age);

    f->condition = new QLineEdit(current.condition, this);
    f->condition->setPlaceholderText(
        QStringLiteral("e.g. locked-in syndrome after a brainstem stroke"));
    form->addRow(QStringLiteral("Condition"), f->condition);

    f->notes = new QPlainTextEdit(current.notes, this);
    f->notes->setPlaceholderText(QStringLiteral(
        "Anything that comes up daily — pain in the left shoulder, ventilated, "
        "fed by tube, hates the blind closed"));
    f->notes->setFixedHeight(88);
    form->addRow(QStringLiteral("Worth knowing"), f->notes);

    f->caregiver = new QLineEdit(current.caregiver, this);
    f->caregiver->setPlaceholderText(QStringLiteral("Whoever is usually asking"));
    form->addRow(QStringLiteral("Caregiver"), f->caregiver);

    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(this);
    /* "Skip for now" rather than "Cancel": on first run there is nothing to
     * cancel, and the application works without any of this. */
    buttons->addButton(QStringLiteral("Skip for now"), QDialogButtonBox::RejectRole);
    /* Constructed here rather than by addButton: a name set on a button the
     * box has already created and polished does not reach the style sheet. */
    auto *save = new QPushButton(QStringLiteral("Save"), this);
    save->setObjectName(QStringLiteral("primary"));
    save->setMinimumHeight(38);
    save->setDefault(true);
    buttons->addButton(save, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    f->name->setFocus();
}

PatientDialog::~PatientDialog() { delete f; }

PatientProfile PatientDialog::profile() const
{
    PatientProfile p;
    p.answered  = true;
    p.name      = f->name->text();
    p.pronouns  = f->pronouns->currentText();
    p.age       = f->age->value();
    p.condition = f->condition->text();
    p.notes     = f->notes->toPlainText();
    p.caregiver = f->caregiver->text();
    return p;
}
