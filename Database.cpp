// Database.cpp
#include "Database.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <algorithm>
#include <functional>

static bool hasColumn(const QString& table, const QString& column) {
    QSqlQuery q("PRAGMA table_info(" + table + ")");
    while (q.next()) {
        if (q.value(1).toString() == column)
            return true;
    }
    return false;
}

static bool seedSubjects() {
    // Only missing subjects: existing ones keep the color chosen by the user.
    // Hues chosen to be distinguishable on a dark background,
    // also with color vision deficiency (red vs. green).
    // The order here is the display order of subjects and their tasks.
    const struct { const char* name; const char* color; } subjects[] = {
        {"Algorithms", "#d93f4c"},
        {"C++/CUDA",   "#2fb37a"},
        {"Go",         "#3b8fe6"},
        {"Python",     "#c99a16"},
    };

    QSqlQuery q;
    // A subject from the Polish UI era keeps its sessions under the new name.
    q.exec("UPDATE OR IGNORE subjects SET name = 'Algorithms' WHERE name = 'Algorytmy'");
    q.prepare("INSERT INTO subjects(name, color, position) VALUES(?, ?, ?) "
              "ON CONFLICT(name) DO NOTHING");
    int position = 0;
    for (const auto& s : subjects) {
        q.addBindValue(s.name);
        q.addBindValue(s.color);
        q.addBindValue(position++);
        if (!q.exec()) {
            qCritical() << "Database initialization error:" << q.lastError().text();
            return false;
        }
    }
    return true;
}

QString databasePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/study.db";
}

bool openDatabase() {
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(databasePath());
    if (!db.open()) {
        qCritical() << "Cannot open database:" << db.lastError().text();
        return false;
    }
    qInfo() << "Database:" << db.databaseName();

    const char* statements[] = {
        "PRAGMA foreign_keys = ON",
        "CREATE TABLE IF NOT EXISTS subjects ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL UNIQUE, "
        "color TEXT NOT NULL DEFAULT '#9da0a8', "
        "position INTEGER NOT NULL DEFAULT 0, "
        "color_ts INTEGER NOT NULL DEFAULT 0)",
        // One row = one uninterrupted study session (a pause ends the session).
        "CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY, "
        "subject_id INTEGER NOT NULL REFERENCES subjects(id), "
        "start_ts INTEGER NOT NULL, "
        "end_ts INTEGER NOT NULL, "
        "uid TEXT)",
        "CREATE INDEX IF NOT EXISTS idx_sessions_subject_start "
        "ON sessions(subject_id, start_ts)",
        // A checked-off task stays in the database with its completion time and just disappears from the list.
        "CREATE TABLE IF NOT EXISTS todos ("
        "id INTEGER PRIMARY KEY, "
        "text TEXT NOT NULL, "
        "created_ts INTEGER NOT NULL, "
        "done_ts INTEGER, "
        "subject_id INTEGER REFERENCES subjects(id), "
        "uid TEXT, "
        "subject_ts INTEGER NOT NULL DEFAULT 0, "
        "deadline TEXT, "
        "deadline_ts INTEGER NOT NULL DEFAULT 0)",
    };

    QSqlQuery q;
    for (const char* sql : statements) {
        if (!q.exec(sql)) {
            qCritical() << "Database initialization error:" << q.lastError().text();
            return false;
        }
    }

    // Older databases lack columns added after they were created.
    const struct { const char* table; const char* column; const char* definition; } added[] = {
        {"subjects", "color",      "TEXT NOT NULL DEFAULT '#9da0a8'"},
        {"subjects", "position",   "INTEGER NOT NULL DEFAULT 0"},
        {"todos",    "subject_id", "INTEGER REFERENCES subjects(id)"},
        // Sync columns: id numbers differ on each device, so rows are
        // identified by uid, and the newer change decides a task's color and subject.
        {"subjects", "color_ts",   "INTEGER NOT NULL DEFAULT 0"},
        {"sessions", "uid",        "TEXT"},
        {"todos",    "uid",        "TEXT"},
        {"todos",    "subject_ts", "INTEGER NOT NULL DEFAULT 0"},
        // The due date is a day in yyyy-mm-dd format, independent of the device's time zone.
        {"todos",    "deadline",    "TEXT"},
        {"todos",    "deadline_ts", "INTEGER NOT NULL DEFAULT 0"},
        // Time of the session's manual edit; 0 when nobody edited it. A deleted session
        // stays in the database with end_ts = start_ts so the other device does not restore it.
        {"sessions", "edit_ts",     "INTEGER NOT NULL DEFAULT 0"},
        {"todos",    "deleted_ts",  "INTEGER"},
    };
    for (const auto& c : added) {
        if (!hasColumn(c.table, c.column)
            && !q.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                           .arg(c.table, c.column, c.definition))) {
            qCritical() << "Database migration error:" << q.lastError().text();
            return false;
        }
    }

    // Rows from before sync existed get their uid only here.
    const char* syncStatements[] = {
        "UPDATE sessions SET uid = lower(hex(randomblob(16))) WHERE uid IS NULL",
        "UPDATE todos SET uid = lower(hex(randomblob(16))) WHERE uid IS NULL",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_sessions_uid ON sessions(uid)",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_todos_uid ON todos(uid)",
    };
    for (const char* sql : syncStatements) {
        if (!q.exec(sql)) {
            qCritical() << "Database migration error:" << q.lastError().text();
            return false;
        }
    }

    return seedSubjects();
}

QList<SubjectTotal> subjectsWithTodayTotals() {
    const qint64 dayStart = QDate::currentDate().startOfDay().toSecsSinceEpoch();

    QSqlQuery q;
    q.prepare("SELECT s.id, s.name, s.color, "
              "COALESCE(SUM(se.end_ts - MAX(se.start_ts, ?)), 0) "
              "FROM subjects s "
              "LEFT JOIN sessions se ON se.subject_id = s.id AND se.end_ts > ? "
              "GROUP BY s.id ORDER BY s.position, s.id");
    q.addBindValue(dayStart);
    q.addBindValue(dayStart);

    QList<SubjectTotal> result;
    if (!q.exec()) {
        qWarning() << "Database reading error:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        result.append({q.value(0).toInt(), q.value(1).toString(),
                       QColor(q.value(2).toString()), q.value(3).toLongLong()});
    }
    return result;
}

bool addSubject(const QString& name, const QColor& color) {
    QSqlQuery q;
    q.prepare("SELECT 1 FROM subjects WHERE name = ? COLLATE NOCASE");
    q.addBindValue(name);
    if (q.exec() && q.next())
        return false;

    q.prepare("INSERT INTO subjects(name, color, position) "
              "SELECT ?, ?, COALESCE(MAX(position), -1) + 1 FROM subjects");
    q.addBindValue(name);
    q.addBindValue(color.name());
    if (!q.exec()) {
        qWarning() << "Database saving error:" << q.lastError().text();
        return false;
    }
    return true;
}

void setSubjectColor(int id, const QColor& color) {
    QSqlQuery q;
    q.prepare("UPDATE subjects SET color = ?, color_ts = ? WHERE id = ?");
    q.addBindValue(color.name());
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(id);

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

QList<Session> sessionsBetween(qint64 from, qint64 to) {
    QSqlQuery q;
    q.prepare("SELECT subject_id, MAX(start_ts, ?), MIN(end_ts, ?) "
              "FROM sessions WHERE end_ts > ? AND start_ts < ? AND end_ts > start_ts "
              "ORDER BY start_ts");
    q.addBindValue(from);
    q.addBindValue(to);
    q.addBindValue(from);
    q.addBindValue(to);

    QList<Session> result;
    if (!q.exec()) {
        qWarning() << "Database reading error:" << q.lastError().text();
        return result;
    }
    while (q.next())
        result.append({q.value(0).toInt(), q.value(1).toLongLong(), q.value(2).toLongLong()});
    return result;
}

qint64 firstSessionStart() {
    QSqlQuery q;
    if (!q.exec("SELECT MIN(start_ts) FROM sessions WHERE end_ts > start_ts") || !q.next()) {
        qWarning() << "Database reading error:" << q.lastError().text();
        return 0;
    }
    return q.value(0).toLongLong();
}

QList<Todo> openTodos() {
    QSqlQuery q;
    QList<Todo> result;
    if (!q.exec("SELECT t.id, t.text, s.color, t.deadline FROM todos t "
                "LEFT JOIN subjects s ON s.id = t.subject_id "
                "WHERE t.done_ts IS NULL AND t.deleted_ts IS NULL "
                "ORDER BY t.deadline IS NULL, t.deadline, s.id IS NULL, s.position, t.id")) {
        qWarning() << "Database reading error:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        result.append({q.value(0).toInt(), q.value(1).toString(),
                       q.value(2).isNull() ? QColor() : QColor(q.value(2).toString()),
                       QDate::fromString(q.value(3).toString(), Qt::ISODate)});
    }
    return result;
}

static QVariant subjectOrNull(int subjectId) {
    return subjectId == -1 ? QVariant() : QVariant(subjectId);
}

static QVariant deadlineOrNull(const QDate& deadline) {
    return deadline.isValid() ? QVariant(deadline.toString(Qt::ISODate)) : QVariant();
}

void addTodo(const QString& text, int subjectId, const QDate& deadline) {
    QSqlQuery q;
    q.prepare("INSERT INTO todos(text, created_ts, subject_id, deadline, uid) "
              "VALUES(?, ?, ?, ?, lower(hex(randomblob(16))))");
    q.addBindValue(text);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(subjectOrNull(subjectId));
    q.addBindValue(deadlineOrNull(deadline));

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

void setTodoSubject(int id, int subjectId) {
    QSqlQuery q;
    q.prepare("UPDATE todos SET subject_id = ?, subject_ts = ? WHERE id = ?");
    q.addBindValue(subjectOrNull(subjectId));
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(id);

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

void setTodoDeadline(int id, const QDate& deadline) {
    QSqlQuery q;
    q.prepare("UPDATE todos SET deadline = ?, deadline_ts = ? WHERE id = ?");
    q.addBindValue(deadlineOrNull(deadline));
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(id);

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

void completeTodo(int id) {
    QSqlQuery q;
    q.prepare("UPDATE todos SET done_ts = ? WHERE id = ?");
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(id);

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

void deleteTodo(int id) {
    QSqlQuery q;
    q.prepare("UPDATE todos SET deleted_ts = ? WHERE id = ?");
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(id);

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

QList<DoneTodo> todosDoneBetween(qint64 from, qint64 to) {
    QList<DoneTodo> result;
    QSqlQuery q;
    q.prepare("SELECT t.text, COALESCE(t.subject_id, "
              "  (SELECT se.subject_id FROM sessions se "
              "   WHERE se.start_ts <= t.done_ts AND se.end_ts > se.start_ts "
              "   ORDER BY se.start_ts DESC LIMIT 1), -1), t.done_ts "
              "FROM todos t "
              "WHERE t.done_ts IS NOT NULL AND t.deleted_ts IS NULL AND t.done_ts >= ? AND t.done_ts < ? "
              "ORDER BY t.done_ts");
    q.addBindValue(std::max(from, kTodosCountFrom));
    q.addBindValue(to);
    if (!q.exec()) {
        qWarning() << "Database reading error:" << q.lastError().text();
        return result;
    }
    while (q.next())
        result.append({q.value(0).toString(), q.value(1).toInt(), q.value(2).toLongLong()});
    return result;
}

QJsonObject exportSnapshot() {
    QJsonArray subjects, sessions, todos;
    QSqlQuery q;

    // Subjects are sent by name, because only names are shared by both devices.
    if (!q.exec("SELECT name, color, color_ts FROM subjects ORDER BY position, id"))
        qWarning() << "Database reading error:" << q.lastError().text();
    while (q.next()) {
        subjects.append(QJsonObject{{"name", q.value(0).toString()},
                                    {"color", q.value(1).toString()},
                                    {"color_ts", q.value(2).toLongLong()}});
    }

    if (!q.exec("SELECT se.uid, s.name, se.start_ts, se.end_ts, se.edit_ts FROM sessions se "
                "JOIN subjects s ON s.id = se.subject_id"))
        qWarning() << "Database reading error:" << q.lastError().text();
    while (q.next()) {
        sessions.append(QJsonObject{{"uid", q.value(0).toString()},
                                    {"subject", q.value(1).toString()},
                                    {"start", q.value(2).toLongLong()},
                                    {"end", q.value(3).toLongLong()},
                                    {"edit_ts", q.value(4).toLongLong()}});
    }

    if (!q.exec("SELECT t.uid, t.text, t.created_ts, t.done_ts, s.name, t.subject_ts, "
                "t.deadline, t.deadline_ts, t.deleted_ts "
                "FROM todos t LEFT JOIN subjects s ON s.id = t.subject_id"))
        qWarning() << "Database reading error:" << q.lastError().text();
    while (q.next()) {
        todos.append(QJsonObject{
            {"uid", q.value(0).toString()},
            {"text", q.value(1).toString()},
            {"created", q.value(2).toLongLong()},
            {"done", q.value(3).isNull() ? QJsonValue() : QJsonValue(q.value(3).toLongLong())},
            {"subject", q.value(4).isNull() ? QJsonValue() : QJsonValue(q.value(4).toString())},
            {"subject_ts", q.value(5).toLongLong()},
            {"deadline", q.value(6).isNull() ? QJsonValue() : QJsonValue(q.value(6).toString())},
            {"deadline_ts", q.value(7).toLongLong()},
            {"deleted", q.value(8).isNull() ? QJsonValue() : QJsonValue(q.value(8).toLongLong())}});
    }

    return {{"subjects", subjects}, {"sessions", sessions}, {"todos", todos}};
}

static bool run(QSqlQuery& q, const QVariantList& values) {
    for (const QVariant& value : values)
        q.addBindValue(value);
    if (q.exec()) return true;
    qWarning() << "Database merging error:" << q.lastError().text();
    return false;
}

// takeIncoming(localTs, incomingTs) tells whether a field changed on both devices should take
// the value from the other device.
static bool mergeRows(const QJsonObject& snapshot,
                      const std::function<bool(qint64, qint64)>& takeIncoming) {
    QSqlQuery q;

    for (const QJsonValue& value : snapshot["subjects"].toArray()) {
        const QJsonObject s = value.toObject();
        const QString name = s["name"].toString().trimmed();
        const QColor color(s["color"].toString());
        const qint64 colorTs = s["color_ts"].toInteger();
        if (name.isEmpty() || !color.isValid()) continue;

        q.prepare("SELECT id, color_ts FROM subjects WHERE name = ? COLLATE NOCASE");
        if (!run(q, {name})) return false;
        if (!q.next()) {
            q.prepare("INSERT INTO subjects(name, color, color_ts, position) "
                      "SELECT ?, ?, ?, COALESCE(MAX(position), -1) + 1 FROM subjects");
            if (!run(q, {name, color.name(), colorTs})) return false;
        } else if (takeIncoming(q.value(1).toLongLong(), colorTs)) {
            const int id = q.value(0).toInt();
            q.prepare("UPDATE subjects SET color = ?, color_ts = ? WHERE id = ?");
            if (!run(q, {color.name(), colorTs, id})) return false;
        }
    }

    QHash<QString, int> subjectIds;
    if (!q.exec("SELECT name, id FROM subjects")) return false;
    while (q.next())
        subjectIds.insert(q.value(0).toString().toLower(), q.value(1).toInt());
    const auto subjectId = [&](const QJsonValue& name) {
        return subjectIds.value(name.toString().trimmed().toLower(), -1);
    };

    // A running session arrives many times, each time with a later end.
    // A manual edit (edit_ts) replaces the whole session, even with a shorter or empty one.
    for (const QJsonValue& value : snapshot["sessions"].toArray()) {
        const QJsonObject s = value.toObject();
        const QString uid = s["uid"].toString();
        const int subject = subjectId(s["subject"]);
        if (uid.isEmpty() || subject == -1) continue;
        const qint64 start = s["start"].toInteger();
        const qint64 end = s["end"].toInteger();
        const qint64 editTs = s["edit_ts"].toInteger();

        q.prepare("SELECT id, edit_ts FROM sessions WHERE uid = ?");
        if (!run(q, {uid})) return false;
        if (!q.next()) {
            q.prepare("INSERT INTO sessions(uid, subject_id, start_ts, end_ts, edit_ts) "
                      "VALUES(?, ?, ?, ?, ?)");
            if (!run(q, {uid, subject, start, end, editTs})) return false;
            continue;
        }
        const int id = q.value(0).toInt();
        const qint64 localEditTs = q.value(1).toLongLong();
        if (localEditTs == editTs) {
            q.prepare("UPDATE sessions SET end_ts = MAX(end_ts, ?) WHERE id = ?");
            if (!run(q, {end, id})) return false;
        } else if (takeIncoming(localEditTs, editTs)) {
            q.prepare("UPDATE sessions SET subject_id = ?, start_ts = ?, end_ts = ?, edit_ts = ? "
                      "WHERE id = ?");
            if (!run(q, {subject, start, end, editTs, id})) return false;
        }
    }

    for (const QJsonValue& value : snapshot["todos"].toArray()) {
        const QJsonObject t = value.toObject();
        const QString uid = t["uid"].toString();
        if (uid.isEmpty()) continue;
        const QVariant subject = subjectOrNull(subjectId(t["subject"]));
        const qint64 subjectTs = t["subject_ts"].toInteger();
        const QVariant deadline =
            deadlineOrNull(QDate::fromString(t["deadline"].toString(), Qt::ISODate));
        const qint64 deadlineTs = t["deadline_ts"].toInteger();
        const auto timeOrNull = [](const QJsonValue& v) {
            return v.isNull() || v.isUndefined() ? QVariant() : QVariant(v.toInteger());
        };
        QVariant done = timeOrNull(t["done"]);
        QVariant deleted = timeOrNull(t["deleted"]);

        q.prepare("SELECT id, done_ts, subject_ts, deadline_ts, deleted_ts FROM todos WHERE uid = ?");
        if (!run(q, {uid})) return false;
        if (!q.next()) {
            q.prepare("INSERT INTO todos(uid, text, created_ts, done_ts, subject_id, subject_ts, "
                      "deadline, deadline_ts, deleted_ts) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)");
            if (!run(q, {uid, t["text"].toString(), t["created"].toInteger(), done, subject,
                         subjectTs, deadline, deadlineTs, deleted}))
                return false;
            continue;
        }

        const int id = q.value(0).toInt();
        const QVariant localDone = q.value(1);
        const QVariant localDeleted = q.value(4);
        const bool newerSubject = takeIncoming(q.value(2).toLongLong(), subjectTs);
        const bool newerDeadline = takeIncoming(q.value(3).toLongLong(), deadlineTs);
        // A task checked off on either device is checked off on both.
        if (done.isNull() || (!localDone.isNull() && localDone.toLongLong() < done.toLongLong()))
            done = localDone;
        // Same for deletion.
        if (deleted.isNull()
            || (!localDeleted.isNull() && localDeleted.toLongLong() < deleted.toLongLong()))
            deleted = localDeleted;

        q.prepare("UPDATE todos SET done_ts = ?, deleted_ts = ? WHERE id = ?");
        if (!run(q, {done, deleted, id})) return false;
        if (newerSubject) {
            q.prepare("UPDATE todos SET subject_id = ?, subject_ts = ? WHERE id = ?");
            if (!run(q, {subject, subjectTs, id})) return false;
        }
        if (newerDeadline) {
            q.prepare("UPDATE todos SET deadline = ?, deadline_ts = ? WHERE id = ?");
            if (!run(q, {deadline, deadlineTs, id})) return false;
        }
    }
    return true;
}

static MergeResult merge(const QJsonObject& snapshot,
                         const std::function<bool(qint64, qint64)>& takeIncoming) {
    // Comparing the whole database before and after is simple, and it is a few megabytes at most.
    const QJsonObject before = exportSnapshot();
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.transaction()) {
        qWarning() << "Database merging error:" << db.lastError().text();
        return MergeResult::Failed;
    }
    if (!mergeRows(snapshot, takeIncoming)) {
        db.rollback();
        return MergeResult::Failed;
    }
    if (!db.commit()) return MergeResult::Failed;
    return exportSnapshot() == before ? MergeResult::Unchanged : MergeResult::Changed;
}

MergeResult mergeFromPhone(const QJsonObject& snapshot, qint64 lastSync) {
    return merge(snapshot, [lastSync](qint64 localTs, qint64 incomingTs) {
        return incomingTs > localTs && localTs <= lastSync;
    });
}

MergeResult mergeFromComputer(const QJsonObject& snapshot) {
    return merge(snapshot, [](qint64, qint64) { return true; });
}
