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
    // Color of the assigned subject; invalid when the task has no subject.
    QColor color;
    // Invalid for tasks added before due dates existed.
    QDate deadline;
};

// A task checked off during or right after a study session, assigned to that session.
struct DoneTodo {
    QString text;
    // Subject of the session the task counts towards.
    int subjectId;
    qint64 done;
    // A moment within that session and the queried range; determines the day it counts towards.
    qint64 at;
};

QString databasePath();
bool openDatabase();
QList<SubjectTotal> subjectsWithTodayTotals();
// A new subject goes to the end of the list; false when the name already exists.
bool addSubject(const QString& name, const QColor& color);
void setSubjectColor(int id, const QColor& color);
// Study sessions clipped to [from, to), sorted by start time.
QList<Session> sessionsBetween(qint64 from, qint64 to);
// Start of the oldest study session; 0 when there are none yet.
qint64 firstSessionStart();

// Tasks not yet checked off, nearest due date first; those without a due date last.
// Equal due dates follow subject order, with tasks without a subject last.
QList<Todo> openTodos();
// subjectId == -1 means a task without a subject.
void addTodo(const QString& text, int subjectId, const QDate& deadline);
void setTodoSubject(int id, int subjectId);
void setTodoDeadline(int id, const QDate& deadline);
void completeTodo(int id);
// A deleted task stays in the database with its deletion time so sync does not restore it.
void deleteTodo(int id);
// Tasks checked off during a study session or at most kTodoAfterSession seconds after it,
// assigned to sessions overlapping [from, to). A task with a subject counts only towards
// study of that subject; a task without a subject counts towards the last session before it.
constexpr qint64 kTodoAfterSession = 30 * 60;
// Tasks checked off before this feature was introduced (2026-10-27 13:22) do not count.
constexpr qint64 kTodosCountFrom = 1793103771;
QList<DoneTodo> todosDoneWithSessions(qint64 from, qint64 to);

// The whole database in a form the other side of the sync can merge with its own.
QJsonObject exportSnapshot();
// Merging adds missing rows and never deletes anything (a deleted study session is a session
// shortened to zero). When the same field changed on both devices since the last
// sync, the computer wins.
enum class MergeResult { Failed, Unchanged, Changed };
// On the computer: a change from the phone wins when it is newer and the computer has not changed
// that field since lastSync (the computer's time of the previous sync with this phone).
MergeResult mergeFromPhone(const QJsonObject& snapshot, qint64 lastSync);
// On the phone: the computer sends back an already resolved state, so its values win.
MergeResult mergeFromComputer(const QJsonObject& snapshot);

#endif
