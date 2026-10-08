// Database.h
#ifndef LEARNINGTIMEANALIZER_DATABASE_H
#define LEARNINGTIMEANALIZER_DATABASE_H

#include <QColor>
#include <QList>
#include <QString>

struct SubjectTotal {
    int id;
    QString name;
    QColor color;
    qint64 todaySeconds;
};

struct Session {
    int subjectId;
    qint64 start;
    qint64 end;
};

bool openDatabase();
QList<SubjectTotal> subjectsWithTodayTotals();
// Odcinki nauki przycięte do przedziału [from, to), posortowane po czasie startu.
QList<Session> sessionsBetween(qint64 from, qint64 to);

#endif
