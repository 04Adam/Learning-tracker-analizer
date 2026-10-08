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

bool openDatabase() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dir + "/study.db");
    if (!db.open()) {
        qCritical() << "Nie można otworzyć bazy:" << db.lastError().text();
        return false;
    }
    qInfo() << "Baza danych:" << db.databaseName();

    const char* statements[] = {
        "PRAGMA foreign_keys = ON",
        "CREATE TABLE IF NOT EXISTS subjects ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL UNIQUE)",
        // Jeden wiersz = jeden nieprzerwany odcinek nauki (pauza kończy odcinek).
        "CREATE TABLE IF NOT EXISTS sessions ("
        "id INTEGER PRIMARY KEY, "
        "subject_id INTEGER NOT NULL REFERENCES subjects(id), "
        "start_ts INTEGER NOT NULL, "
        "end_ts INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS idx_sessions_subject_start "
        "ON sessions(subject_id, start_ts)",
        "INSERT OR IGNORE INTO subjects(name) VALUES "
        "('Algorytmy'), ('Python'), ('C++/CUDA')",
    };

    QSqlQuery q;
    for (const char* sql : statements) {
        if (!q.exec(sql)) {
            qCritical() << "Błąd inicjalizacji bazy:" << q.lastError().text();
            return false;
        }
    }
    return true;
}

QList<SubjectTotal> subjectsWithTodayTotals() {
    QSqlQuery q;
    q.prepare("SELECT s.id, s.name, COALESCE(SUM(se.end_ts - se.start_ts), 0) "
              "FROM subjects s "
              "LEFT JOIN sessions se ON se.subject_id = s.id AND se.start_ts >= ? "
              "GROUP BY s.id ORDER BY s.id");
    q.addBindValue(QDate::currentDate().startOfDay().toSecsSinceEpoch());

    QList<SubjectTotal> result;
    if (!q.exec()) {
        qWarning() << "Database reading error:" << q.lastError().text();
        return result;
    }
    while (q.next())
        result.append({q.value(0).toInt(), q.value(1).toString(), q.value(2).toLongLong()});
    return result;
}
