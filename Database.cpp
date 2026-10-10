// Database.cpp
#include "Database.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

static bool hasColumn(const QString& table, const QString& column) {
    QSqlQuery q("PRAGMA table_info(" + table + ")");
    while (q.next()) {
        if (q.value(1).toString() == column)
            return true;
    }
    return false;
}

static bool seedSubjects() {
    // Odcienie dobrane tak, żeby dało się je rozróżnić na ciemnym tle,
    // także przy zaburzeniach widzenia barw (czerwony kontra zielony).
    // Kolejność tutaj to kolejność wyświetlania przedmiotów i ich zadań.
    const struct { const char* name; const char* color; } subjects[] = {
        {"Algorithms", "#d93f4c"},
        {"C++/CUDA",   "#2fb37a"},
        {"Go",         "#3b8fe6"},
        {"Python",     "#c99a16"},
    };

    QSqlQuery q;
    // Przedmiot z czasów polskiego interfejsu zachowuje swoje sesje pod nową nazwą.
    q.exec("UPDATE OR IGNORE subjects SET name = 'Algorithms' WHERE name = 'Algorytmy'");
    q.prepare("INSERT INTO subjects(name, color, position) VALUES(?, ?, ?) "
              "ON CONFLICT(name) DO UPDATE SET color = excluded.color, "
              "position = excluded.position");
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

bool openDatabase() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dir + "/study.db");
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
        "position INTEGER NOT NULL DEFAULT 0)",
        // Jeden wiersz = jeden nieprzerwany odcinek nauki (pauza kończy odcinek).
        "CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY, "
        "subject_id INTEGER NOT NULL REFERENCES subjects(id), "
        "start_ts INTEGER NOT NULL, "
        "end_ts INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS idx_sessions_subject_start "
        "ON sessions(subject_id, start_ts)",
        // Odhaczone zadanie zostaje w bazie z czasem wykonania, tylko znika z listy.
        "CREATE TABLE IF NOT EXISTS todos ("
        "id INTEGER PRIMARY KEY, "
        "text TEXT NOT NULL, "
        "created_ts INTEGER NOT NULL, "
        "done_ts INTEGER, "
        "subject_id INTEGER REFERENCES subjects(id))",
    };

    QSqlQuery q;
    for (const char* sql : statements) {
        if (!q.exec(sql)) {
            qCritical() << "Database initialization error:" << q.lastError().text();
            return false;
        }
    }

    // Starsze bazy nie mają kolumn dodanych po ich utworzeniu.
    const struct { const char* table; const char* column; const char* definition; } added[] = {
        {"subjects", "color",      "TEXT NOT NULL DEFAULT '#9da0a8'"},
        {"subjects", "position",   "INTEGER NOT NULL DEFAULT 0"},
        {"todos",    "subject_id", "INTEGER REFERENCES subjects(id)"},
    };
    for (const auto& c : added) {
        if (!hasColumn(c.table, c.column)
            && !q.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                           .arg(c.table, c.column, c.definition))) {
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

QList<Session> sessionsBetween(qint64 from, qint64 to) {
    QSqlQuery q;
    q.prepare("SELECT subject_id, MAX(start_ts, ?), MIN(end_ts, ?) "
              "FROM sessions WHERE end_ts > ? AND start_ts < ? "
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

QList<Todo> openTodos() {
    QSqlQuery q;
    QList<Todo> result;
    if (!q.exec("SELECT t.id, t.text, s.color FROM todos t "
                "LEFT JOIN subjects s ON s.id = t.subject_id "
                "WHERE t.done_ts IS NULL "
                "ORDER BY s.id IS NULL, s.position, t.id")) {
        qWarning() << "Database reading error:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        result.append({q.value(0).toInt(), q.value(1).toString(),
                       q.value(2).isNull() ? QColor() : QColor(q.value(2).toString())});
    }
    return result;
}

static QVariant subjectOrNull(int subjectId) {
    return subjectId == -1 ? QVariant() : QVariant(subjectId);
}

void addTodo(const QString& text, int subjectId) {
    QSqlQuery q;
    q.prepare("INSERT INTO todos(text, created_ts, subject_id) VALUES(?, ?, ?)");
    q.addBindValue(text);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(subjectOrNull(subjectId));

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

void setTodoSubject(int id, int subjectId) {
    QSqlQuery q;
    q.prepare("UPDATE todos SET subject_id = ? WHERE id = ?");
    q.addBindValue(subjectOrNull(subjectId));
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
