// Counter.cpp
#include "Counter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

using namespace std::chrono;

static qlonglong toTs(system_clock::time_point t) {
    return duration_cast<seconds>(t.time_since_epoch()).count();
}

void Counter::start(int subjectId) {
    if (currentState != State::Stopped) return;
    subject = subjectId;
    accumulated = {};
    openSegment();
    currentState = State::Running;
}

void Counter::pause() {
    if (currentState != State::Running) return;
    closeSegment();
    currentState = State::Paused;
}

void Counter::resume() {
    if (currentState != State::Paused) return;
    openSegment();
    currentState = State::Running;
}

void Counter::stop() {
    if (currentState == State::Running)
        closeSegment();
    accumulated = {};
    currentState = State::Stopped;
}

void Counter::checkpoint() {
    if (currentState != State::Running) return;

    QSqlQuery q;
    q.prepare("UPDATE sessions SET end_ts = ? WHERE id = ?");
    q.addBindValue(toTs(system_clock::now()));
    q.addBindValue(segmentRowId);

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
}

seconds Counter::elapsed() const {
    auto total = accumulated;
    if (currentState == State::Running)
        total += system_clock::now() - segmentStart;
    return duration_cast<seconds>(total);
}

void Counter::openSegment() {
    segmentStart = system_clock::now();

    QSqlQuery q;
    q.prepare("INSERT INTO sessions(subject_id, start_ts, end_ts) VALUES(?, ?, ?)");
    q.addBindValue(subject);
    q.addBindValue(toTs(segmentStart));
    q.addBindValue(toTs(segmentStart));

    if (!q.exec())
        qWarning() << "Database saving error:" << q.lastError().text();
    segmentRowId = q.lastInsertId().toLongLong();
}

void Counter::closeSegment() {
    checkpoint();
    accumulated += system_clock::now() - segmentStart;
}
