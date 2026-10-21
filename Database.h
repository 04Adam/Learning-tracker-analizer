// Database.h
#ifndef LEARNINGTIMEANALIZER_DATABASE_H
#define LEARNINGTIMEANALIZER_DATABASE_H

#include <QColor>
#include <QDate>
#include <QJsonObject>
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
    // Nieprawidłowy u zadań dodanych, zanim pojawiły się terminy.
    QDate deadline;
};

QString databasePath();
bool openDatabase();
QList<SubjectTotal> subjectsWithTodayTotals();
// Nowy przedmiot trafia na koniec listy; false, gdy taka nazwa już istnieje.
bool addSubject(const QString& name, const QColor& color);
void setSubjectColor(int id, const QColor& color);
// Odcinki nauki przycięte do przedziału [from, to), posortowane po czasie startu.
QList<Session> sessionsBetween(qint64 from, qint64 to);
// Początek najstarszego odcinka nauki; 0, gdy nie ma jeszcze żadnego.
qint64 firstSessionStart();

// Zadania jeszcze nieodhaczone, od najbliższego terminu; te bez terminu na końcu.
// Przy równych terminach w kolejności przedmiotów, z zadaniami bez przedmiotu na końcu.
QList<Todo> openTodos();
// subjectId == -1 oznacza zadanie bez przedmiotu.
void addTodo(const QString& text, int subjectId, const QDate& deadline);
void setTodoSubject(int id, int subjectId);
void setTodoDeadline(int id, const QDate& deadline);
void completeTodo(int id);

// Cała baza w postaci, którą druga strona synchronizacji potrafi scalić ze swoją.
QJsonObject exportSnapshot();
// Scalanie dopisuje brakujące wiersze i niczego nie usuwa. Gdy to samo pole zmieniono na
// obu urządzeniach od ostatniej synchronizacji, wygrywa komputer.
enum class MergeResult { Failed, Unchanged, Changed };
// Na komputerze: zmiana z telefonu wygrywa, gdy jest nowsza, a komputer nie zmieniał tego
// pola od lastSync (czas komputera z poprzedniej synchronizacji z tym telefonem).
MergeResult mergeFromPhone(const QJsonObject& snapshot, qint64 lastSync);
// Na telefonie: komputer odsyła stan już rozstrzygnięty, więc jego wartości wygrywają.
MergeResult mergeFromComputer(const QJsonObject& snapshot);

#endif
