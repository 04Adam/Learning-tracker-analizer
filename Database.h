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

struct Todo {
    int id;
    QString text;
    // Kolor przypisanego przedmiotu; nieprawidłowy, gdy zadanie nie ma przedmiotu.
    QColor color;
};

bool openDatabase();
QList<SubjectTotal> subjectsWithTodayTotals();
// Odcinki nauki przycięte do przedziału [from, to), posortowane po czasie startu.
QList<Session> sessionsBetween(qint64 from, qint64 to);
// Początek najstarszego odcinka nauki; 0, gdy nie ma jeszcze żadnego.
qint64 firstSessionStart();

// Zadania jeszcze nieodhaczone, w kolejności przedmiotów; te bez przedmiotu na końcu.
QList<Todo> openTodos();
// subjectId == -1 oznacza zadanie bez przedmiotu.
void addTodo(const QString& text, int subjectId);
void setTodoSubject(int id, int subjectId);
void completeTodo(int id);

#endif
