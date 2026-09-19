/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#ifndef OPENNCOMM_PROFILE_H
#define OPENNCOMM_PROFILE_H

#include <QDialog>
#include <QString>

/* Who the application is speaking for.
 *
 * This is the most sensitive data the program holds -- a name, an age, a
 * diagnosis -- so it never leaves the machine. It lives in the same local
 * QSettings file as the blink timings, it is never sent anywhere, and the only
 * thing that reads it is the prompt handed to the model running in this
 * process. The dialog says so plainly, because a caregiver typing a diagnosis
 * into a computer deserves to be told where it goes.
 *
 * Every field is optional. A profile with nothing but a name is fine, and an
 * empty profile still gives the patient a working application -- the fields
 * only sharpen the answers, they are not required to produce them. */
struct PatientProfile {
    QString name;
    /* Asked, never guessed. A name does not imply pronouns, and getting this
     * wrong in text a patient has to watch every day is its own small harm. */
    QString pronouns;
    int     age = 0;          /* 0 means not given. */
    QString condition;
    QString notes;
    QString caregiver;

    /* True once the caregiver has been through the dialog, even if they
     * skipped every field. Without this an empty-but-answered profile would
     * reopen the dialog on every launch. */
    bool answered = false;

    static PatientProfile load();
    void save() const;

    /* The profile as lines for the model prompt. Empty when there is nothing
     * worth saying, so the prompt stays short when the profile is blank --
     * every extra line is latency on every question. */
    QString brief() const;

    /* "Kumar" if known, otherwise "the patient" -- for interface text. */
    QString displayName() const;
};

/* The first-run form, reachable afterwards from Settings. */
class PatientDialog : public QDialog {
    Q_OBJECT
public:
    explicit PatientDialog(const PatientProfile &current, QWidget *parent = nullptr);
    ~PatientDialog() override;

    PatientProfile profile() const;

private:
    struct Fields;
    Fields *f;
};

#endif
