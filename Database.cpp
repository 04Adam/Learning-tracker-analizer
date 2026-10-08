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
    const struct { const char* name; const char* color; } subjects[] = {
        {"Algorithms", "#d93f4c"},
        {"Python",     "#c99a16"},
        {"C++/CUDA",   "#2fb37a"},
        {"Go",         "#3b8fe6"},
    };

    QSqlQuery q;
    // Przedmiot z czasów polskiego interfejsu zachowuje swoje sesje pod nową nazwą.
    q.exec("UPDATE OR IGNORE subjects SET name = 'Algorithms' WHERE name = 'Algorytmy'");
    q.prepare("INSERT INTO subjects(name, color) VALUES(?, ?) "
              "ON CONFLICT(name) DO UPDATE SET color = excluded.color");
    for (const auto& s : subjects) {
        q.addBindValue(s.name);
        q.addBindValue(s.color);
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
        "color TEXT NOT NULL DEFAULT '#9da0a8')",
        // Jeden wiersz = jeden nieprzerwany odcinek nauki (pauza kończy odcinek).
        "CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY, "
        "subject_id INTEGER NOT NULL REFERENCES subjects(id), "
        "start_ts INTEGER NOT NULL, "
        "end_ts INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS idx_sessions_subject_start "
        "ON sessions(subject_id, start_ts)",
    };

    QSqlQuery q;
    for (const char* sql : statements) {
        if (!q.exec(sql)) {
            qCritical() << "Database initialization error:" << q.lastError().text();
            return false;
        }
    }

    // Bazy utworzone przed dodaniem kolorów nie mają tej kolumny.
    if (!hasColumn("subjects", "color")
        && !q.exec("ALTER TABLE subjects ADD COLUMN color TEXT NOT NULL DEFAULT '#9da0a8'")) {
        qCritical() << "Database migration error:" << q.lastError().text();
        return false;
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
              "GROUP BY s.id ORDER BY s.id");
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
