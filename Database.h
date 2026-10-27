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

// Zadanie odhaczone w trakcie nauki albo tuż po niej, przypisane do tej nauki.
struct DoneTodo {
    QString text;
    // Przedmiot nauki, do której zadanie się liczy.
    int subjectId;
    qint64 done;
    // Chwila w obrębie tej nauki i pytanego przedziału; wyznacza dzień, do którego się liczy.
    qint64 at;
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
// Usunięte zadanie zostaje w bazie z czasem usunięcia, żeby synchronizacja go nie przywróciła.
void deleteTodo(int id);
// Zadania odhaczone w trakcie odcinka nauki albo najwyżej kTodoAfterSession sekund po nim,
// przypisane do odcinków nachodzących na [from, to). Zadanie z przedmiotem liczy się tylko do
// nauki tego przedmiotu; zadanie bez przedmiotu do ostatniej nauki przed nim.
constexpr qint64 kTodoAfterSession = 30 * 60;
// Zadania odhaczone przed wprowadzeniem tej funkcji (27.10.2026 13:22) się nie liczą.
constexpr qint64 kTodosCountFrom = 1793103771;
QList<DoneTodo> todosDoneWithSessions(qint64 from, qint64 to);

// Cała baza w postaci, którą druga strona synchronizacji potrafi scalić ze swoją.
QJsonObject exportSnapshot();
// Scalanie dopisuje brakujące wiersze i niczego nie usuwa (usunięty odcinek nauki to odcinek
// skrócony do zera). Gdy to samo pole zmieniono na obu urządzeniach od ostatniej
// synchronizacji, wygrywa komputer.
enum class MergeResult { Failed, Unchanged, Changed };
// Na komputerze: zmiana z telefonu wygrywa, gdy jest nowsza, a komputer nie zmieniał tego
// pola od lastSync (czas komputera z poprzedniej synchronizacji z tym telefonem).
MergeResult mergeFromPhone(const QJsonObject& snapshot, qint64 lastSync);
// Na telefonie: komputer odsyła stan już rozstrzygnięty, więc jego wartości wygrywają.
MergeResult mergeFromComputer(const QJsonObject& snapshot);

#endif
