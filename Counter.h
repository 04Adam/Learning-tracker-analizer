// Counter.h
#ifndef LEARNINGTIMEANALIZER_COUNTER_H
#define LEARNINGTIMEANALIZER_COUNTER_H

#include <chrono>

class Counter {
public:
    enum class State { Stopped, Running, Paused };

    void start(int subjectId);
    void pause();
    void resume();
    void stop();

    // Zapisuje bieżący koniec trwającego odcinka, żeby awaria nie zgubiła danych.
    void checkpoint();

    State state() const { return currentState; }
    // Czas nauki od startu stopera, bez pauz.
    std::chrono::seconds elapsed() const;

private:
    void openSegment();
    void closeSegment();

    State currentState = State::Stopped;
    int subject = -1;
    long long segmentRowId = -1;
    std::chrono::system_clock::time_point segmentStart{};
    std::chrono::system_clock::duration accumulated{};
};

#endif
