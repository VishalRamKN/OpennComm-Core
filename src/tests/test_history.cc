/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* Conversation history.
 *
 * Every fixture here is invented. CONTRIBUTING.md forbids deriving test data
 * from a real conversation, and this is the suite where that would be most
 * tempting.
 *
 * The assertions that matter are the ones about deletion and about failing
 * quietly: history is a convenience, and nothing in it may stop a patient
 * speaking.
 */
#include "oc_test.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "history.h"

static Exchange make(const char *question, const char *answer, qint64 at,
                     const char *mode = "blink", const char *source = "model")
{
    Exchange e;
    e.asked_at = at;
    e.question = QString::fromUtf8(question);
    e.answer   = QString::fromUtf8(answer);
    e.mode     = QString::fromUtf8(mode);
    e.source   = QString::fromUtf8(source);
    return e;
}

int main()
{
    QTemporaryDir tmp;
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("history.db"));
    const qint64 noon = QDateTime(QDate(2026, 9, 18), QTime(12, 0)).toMSecsSinceEpoch();

    group("an empty history");
    {
        History h(path);
        check(h.available(), "opens a database where none existed");
        check_int(h.count(), 0, "nothing recorded yet");
        check_int(h.recent().size(), 0, "nothing to show");
        check(h.path() == path, "uses the path it was given");
    }

    group("recording");
    {
        History h(path);
        check(h.record(make("Are you in pain?", "I am in pain", noon)), "records an exchange");
        check_int(h.count(), 1, "count follows");

        const QList<Exchange> all = h.recent();
        check_int(all.size(), 1, "and it comes back");
        check_str(qPrintable(all[0].question), "Are you in pain?", "the question is kept");
        check_str(qPrintable(all[0].answer), "I am in pain", "the answer is kept");
        check_str(qPrintable(all[0].mode), "blink", "so is how it was chosen");
        check(all[0].id > 0, "and it has an id");
    }

    group("what is not an exchange");
    {
        History h(path);
        const int before = h.count();
        check(!h.record(make("Are you hungry?", "", noon)), "an empty answer is not recorded");
        check(!h.record(make("Are you hungry?", "   ", noon)), "nor is whitespace");
        check_int(h.count(), before, "nothing was stored");

        /* A question is optional: morse mode spells an answer to nothing. */
        check(h.record(make("", "I would like the window open", noon + 1000, "morse", "spelled")),
              "an answer with no question is still an exchange");
    }

    group("newest first, and limited");
    {
        History h(path);
        h.record(make("Do you want water?", "Yes please", noon + 60000));
        h.record(make("Are you comfortable?", "Please move my arm", noon + 120000));

        const QList<Exchange> all = h.recent();
        check(all.size() >= 3, "everything is there");
        check_str(qPrintable(all[0].answer), "Please move my arm", "the most recent is first");
        check(all[0].asked_at >= all[1].asked_at, "and the order holds");

        check_int(h.recent(1).size(), 1, "a limit is respected");
        check_int(h.recent(0).size(), 0, "a limit of zero returns nothing");
    }

    group("searching");
    {
        History h(path);
        check_int(h.recent(500, QStringLiteral("water")).size(), 1, "matches the question");
        check_int(h.recent(500, QStringLiteral("arm")).size(), 1, "matches the answer");
        check_int(h.recent(500, QStringLiteral("WATER")).size(), 1, "ignores case");
        check_int(h.recent(500, QStringLiteral("  water  ")).size(), 1, "ignores surrounding space");
        check_int(h.recent(500, QStringLiteral("helicopter")).size(), 0, "no match means none");
        check(h.recent(500, QString()).size() >= 3, "an empty search is not a filter");
    }

    group("the timestamp defaults to now");
    {
        History h(path);
        const qint64 before = QDateTime::currentMSecsSinceEpoch();
        h.record(make("What day is it?", "I do not know", 0));
        const QList<Exchange> all = h.recent(1);
        check(all[0].asked_at >= before, "an unset time is filled in");
        check(all[0].asked_at <= QDateTime::currentMSecsSinceEpoch() + 1000, "and it is now");
    }

    group("it survives being closed");
    {
        int before = 0;
        { History h(path); before = h.count(); }
        History h(path);
        check_int(h.count(), before, "reopening finds the same rows");
    }

    group("removing one");
    {
        History h(path);
        const QList<Exchange> all = h.recent();
        const int before = h.count();
        const qint64 id = all[0].id;

        check(h.remove(id), "removes the row");
        check_int(h.count(), before - 1, "one fewer");
        check(!h.remove(id), "removing it twice reports nothing removed");
        check(!h.remove(999999), "an unknown id removes nothing");

        for (const Exchange &e : h.recent())
            check(e.id != id, "the removed row is gone");
    }

    group("clearing leaves nothing on disk");
    {
        History h(path);
        /* Enough rows to fill several pages. Forty fitted inside sqlite's
         * minimum file size, so "the file shrank" was true of nothing and the
         * assertion could not have failed for the right reason. */
        for (int i = 0; i < 600; i++)
            h.record(make("Are you in pain?", "My shoulder is unbearable today", noon + i));
        check(h.count() >= 600, "recorded plenty");

        const qint64 filled = QFileInfo(path).size();
        check(h.clear(), "clear succeeds");
        check_int(h.count(), 0, "nothing is left");

        const qint64 emptied = QFileInfo(path).size();
        check(filled > 32768, "the database really had grown");
        check(emptied < filled / 2, "and clearing shrank the file, not just the row count");

        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const QByteArray raw = f.readAll();
        check(!raw.contains("unbearable"), "the words are not still sitting in free pages");

        check(h.record(make("Are you thirsty?", "Yes please", noon)),
              "and it still works afterwards");
    }

    group("a database that cannot be opened is not an error the patient sees");
    {
        /* A directory where the file should be: open must fail. */
        const QString blocked = QDir(tmp.path()).filePath(QStringLiteral("blocked.db"));
        QDir().mkpath(blocked);

        History h(blocked);
        check(!h.available(), "reports itself unavailable");
        check(!h.error().isEmpty(), "and says why");
        check(!h.record(make("Are you in pain?", "I am in pain", noon)), "recording is a no-op");
        check_int(h.count(), 0, "counting is a no-op");
        check_int(h.recent().size(), 0, "reading is a no-op");
        check(!h.clear(), "clearing is a no-op");
        check(!h.remove(1), "removing is a no-op");
    }

    group("the handover note");
    {
        QList<Exchange> day;
        /* recent() order: newest first. asText must turn that around. */
        day << make("Do you want water?", "Yes please", noon + 3600000)
            << make("Are you in pain?", "I am in pain", noon);

        const QString text = History::asText(day);
        check(text.contains(QStringLiteral("Friday 18 September 2026")), "the day is a heading");
        check(text.indexOf(QStringLiteral("I am in pain"))
                  < text.indexOf(QStringLiteral("Yes please")),
              "oldest first, because a note is read forwards");
        check(text.contains(QStringLiteral("12:00")), "each line is timed");
        check(text.contains(QStringLiteral("asked: Are you in pain?")), "the question is shown");

        QList<Exchange> spelled;
        spelled << make("", "please call my daughter", noon, "morse", "spelled");
        const QString note = History::asText(spelled);
        check(!note.contains(QStringLiteral("asked:")), "an answer to nothing has no question line");
        check(note.contains(QStringLiteral("said : please call my daughter")), "it is still shown");

        check(History::asText({}).isEmpty(), "nothing in, nothing out");
    }

    return oc_report("test_history");
}
