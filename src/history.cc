/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "history.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <sqlite3.h>

namespace {

/* Bind a QString as UTF-8, copied by sqlite so the QByteArray may die with the
 * statement. */
void bindText(sqlite3_stmt *st, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(st, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

QString columnText(sqlite3_stmt *st, int index)
{
    const auto *raw = sqlite3_column_text(st, index);
    return raw ? QString::fromUtf8(reinterpret_cast<const char *>(raw)) : QString();
}

} // namespace

QString History::defaultPath()
{
    /* Beside the models, for the same reason: GenericDataLocation directly
     * rather than AppDataLocation, which doubles "OpennComm" when the
     * organisation and the application share a name. */
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("openncomm/history.db"));
}

History::History(const QString &path)
    : path_(path.isEmpty() ? defaultPath() : path)
{
    QDir().mkpath(QFileInfo(path_).absolutePath());

    if (sqlite3_open(path_.toUtf8().constData(), &db_) != SQLITE_OK) {
        error_ = db_ ? QString::fromUtf8(sqlite3_errmsg(db_))
                     : QStringLiteral("could not open %1").arg(path_);
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return;
    }

    /* A ward machine gets switched off at the wall. WAL survives that better
     * than the default journal, and a five-second busy timeout costs nothing
     * against a database only this process writes. */
    exec("PRAGMA journal_mode=WAL");
    sqlite3_busy_timeout(db_, 5000);

    if (!exec("CREATE TABLE IF NOT EXISTS exchanges ("
              "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
              "  asked_at INTEGER NOT NULL,"
              "  question TEXT NOT NULL DEFAULT '',"
              "  answer   TEXT NOT NULL,"
              "  mode     TEXT NOT NULL DEFAULT '',"
              "  source   TEXT NOT NULL DEFAULT ''"
              ")")
        || !exec("CREATE INDEX IF NOT EXISTS exchanges_time ON exchanges(asked_at DESC)")) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

History::~History()
{
    if (db_) sqlite3_close(db_);
}

bool History::exec(const char *sql)
{
    char *message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) == SQLITE_OK) return true;
    error_ = message ? QString::fromUtf8(message) : QStringLiteral("sqlite error");
    sqlite3_free(message);
    return false;
}

bool History::record(const Exchange &exchange)
{
    if (!db_) return false;
    /* An empty answer is not an exchange. Nothing was said. */
    if (exchange.answer.trimmed().isEmpty()) return false;

    static const char *kSql =
        "INSERT INTO exchanges (asked_at, question, answer, mode, source)"
        " VALUES (?, ?, ?, ?, ?)";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &st, nullptr) != SQLITE_OK) {
        error_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_int64(st, 1, exchange.asked_at > 0
                                  ? exchange.asked_at
                                  : QDateTime::currentMSecsSinceEpoch());
    bindText(st, 2, exchange.question.trimmed());
    bindText(st, 3, exchange.answer.trimmed());
    bindText(st, 4, exchange.mode);
    bindText(st, 5, exchange.source);

    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok) error_ = QString::fromUtf8(sqlite3_errmsg(db_));
    sqlite3_finalize(st);
    return ok;
}

QList<Exchange> History::recent(int limit, const QString &filter) const
{
    QList<Exchange> out;
    if (!db_ || limit <= 0) return out;

    const QString needle = filter.trimmed();
    const bool filtered = !needle.isEmpty();

    /* LIKE is case insensitive for ASCII in sqlite by default, which is what a
     * caregiver typing "pain" expects. */
    const char *kAll =
        "SELECT id, asked_at, question, answer, mode, source FROM exchanges"
        " ORDER BY asked_at DESC, id DESC LIMIT ?";
    const char *kFiltered =
        "SELECT id, asked_at, question, answer, mode, source FROM exchanges"
        " WHERE question LIKE ? OR answer LIKE ?"
        " ORDER BY asked_at DESC, id DESC LIMIT ?";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db_, filtered ? kFiltered : kAll, -1, &st, nullptr) != SQLITE_OK)
        return out;

    if (filtered) {
        const QString like = QStringLiteral("%") + needle + QStringLiteral("%");
        bindText(st, 1, like);
        bindText(st, 2, like);
        sqlite3_bind_int(st, 3, limit);
    } else {
        sqlite3_bind_int(st, 1, limit);
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        Exchange e;
        e.id       = sqlite3_column_int64(st, 0);
        e.asked_at = sqlite3_column_int64(st, 1);
        e.question = columnText(st, 2);
        e.answer   = columnText(st, 3);
        e.mode     = columnText(st, 4);
        e.source   = columnText(st, 5);
        out << e;
    }
    sqlite3_finalize(st);
    return out;
}

int History::count() const
{
    if (!db_) return 0;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM exchanges", -1, &st, nullptr) != SQLITE_OK)
        return 0;
    const int n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

bool History::remove(qint64 id)
{
    if (!db_) return false;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM exchanges WHERE id = ?", -1, &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, id);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    /* One row gone is not worth rewriting the file; clear() does that. */
    return ok && sqlite3_changes(db_) > 0;
}

bool History::clear()
{
    if (!db_) return false;
    /* DELETE alone leaves every word readable in the file's free pages. The
     * point of this button is that the words are gone, so the file is rewritten
     * and the write-ahead log folded back in.
     *
     * Order matters, and the obvious order is wrong: checkpointing first and
     * vacuuming second leaves the vacuum's own output sitting in a fresh WAL,
     * so the main file never shrinks. Vacuum, then checkpoint. */
    if (!exec("DELETE FROM exchanges")) return false;
    if (!exec("VACUUM")) return false;
    return exec("PRAGMA wal_checkpoint(TRUNCATE)");
}

QString History::asText(const QList<Exchange> &exchanges)
{
    /* Oldest first: a handover note is read forwards, unlike the screen, which
     * shows the most recent thing said at the top. */
    QList<Exchange> ordered = exchanges;
    std::reverse(ordered.begin(), ordered.end());

    QString out;
    QString day;
    for (const Exchange &e : ordered) {
        const QDateTime when = QDateTime::fromMSecsSinceEpoch(e.asked_at);
        const QString this_day = when.toString(QStringLiteral("dddd d MMMM yyyy"));
        if (this_day != day) {
            day = this_day;
            if (!out.isEmpty()) out += QLatin1Char('\n');
            out += this_day + QLatin1Char('\n');
            out += QString(this_day.size(), QLatin1Char('-')) + QLatin1Char('\n');
        }
        out += when.toString(QStringLiteral("HH:mm"));
        if (!e.question.isEmpty())
            out += QStringLiteral("  asked: ") + e.question + QLatin1Char('\n')
                   + QStringLiteral("       said : ") + e.answer + QLatin1Char('\n');
        else
            out += QStringLiteral("  said : ") + e.answer + QLatin1Char('\n');
    }
    return out;
}
