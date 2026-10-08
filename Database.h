// Database.h
#ifndef LEARNINGTIMEANALIZER_DATABASE_H
#define LEARNINGTIMEANALIZER_DATABASE_H

#include <QList>
#include <QString>

struct SubjectTotal {
    int id;
    QString name;
    qint64 todaySeconds;
};

bool openDatabase();
QList<SubjectTotal> subjectsWithTodayTotals();

#endif
