/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* What the patient has said, kept.
 *
 * Until now an answer was spoken and gone. That loses the thing a ward
 * actually needs at three in the afternoon: whether she has said she is in
 * pain today, what he asked for this morning, what the night shift heard. A
 * patient who can produce four words a minute cannot repeat themselves for
 * every new face, so the application remembers on their behalf.
 *
 * This is the most sensitive data the program holds -- not a name and a
 * diagnosis, which a chart has anyway, but the person's own words. Three rules
 * follow from that and are not negotiable:
 *
 *  - It never leaves the machine. No network call, ever. SECURITY.md counts a
 *    leak here as a vulnerability, not a feature request.
 *  - It is deletable, in full and one row at a time, by whoever is sitting
 *    there. A patient who says something they did not mean must not be stuck
 *    with it, and "delete" writes the file down to nothing rather than merely
 *    unlinking rows.
 *  - It never delays the voice. Recording happens after speech has been
 *    started, and a database that cannot be opened at all is not an error the
 *    patient ever sees -- they simply keep talking, unrecorded.
 *
 * sqlite3 directly rather than Qt's SQL module: one public-domain C library
 * present on every Linux, against a Qt plugin that would have to be found,
 * loaded and bundled. Packaging is already hard enough here.
 */
#ifndef OPENNCOMM_HISTORY_H
#define OPENNCOMM_HISTORY_H

#include <QList>
#include <QString>

struct sqlite3;

/* One thing the patient said, and what prompted it. */
struct Exchange {
    qint64 id = 0;
    qint64 asked_at = 0;     /* unix milliseconds */
    QString question;        /* empty when nothing was asked -- morse, mostly */
    QString answer;
    QString mode;            /* blink | head | morse */
    QString source;          /* model | phrasebook | spelled */
};

class History {
public:
    /* An empty path means the XDG default. Tests pass their own. */
    explicit History(const QString &path = QString());
    ~History();

    History(const History &) = delete;
    History &operator=(const History &) = delete;

    /* False when the database could not be opened or prepared. Everything
     * below is then a no-op returning nothing, which is the correct
     * behaviour: history is a convenience and speaking is not. */
    bool available() const { return db_ != nullptr; }
    QString error() const { return error_; }
    QString path() const { return path_; }

    bool record(const Exchange &exchange);

    /* Newest first. `filter` matches the question or the answer, case
     * insensitively; empty matches everything. */
    QList<Exchange> recent(int limit = 500, const QString &filter = QString()) const;

    int count() const;

    bool remove(qint64 id);

    /* Deletes everything and reclaims the space. Returns false if any part of
     * that failed, because a "cleared" history that is still on disk is worse
     * than an error message. */
    bool clear();

    /* Plain text, oldest first, for a handover note. Produced here rather than
     * in the dialog so the format is one thing and can be tested. */
    static QString asText(const QList<Exchange> &exchanges);

    /* Where the default database lives. */
    static QString defaultPath();

private:
    bool exec(const char *sql);

    sqlite3 *db_ = nullptr;
    QString path_;
    QString error_;
};

#endif
