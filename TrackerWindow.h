// TrackerWindow.h
#ifndef LEARNINGTIMEANALIZER_TRACKERWINDOW_H
#define LEARNINGTIMEANALIZER_TRACKERWINDOW_H

#include "Counter.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

// Małe okienko ze stoperem, które można rozwinąć do pulpitu z wyborem przedmiotu.
class TrackerWindow : public QWidget {
public:
    TrackerWindow();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void toggleRunning();
    void stop();
    void toggleDashboard();
    void selectSubject(int subjectId);
    void refreshSubjects();
    void updateControls();
    void updateTime();

    Counter counter;
    int currentSubject = -1;

    QLabel* subjectLabel;
    QLabel* timeLabel;
    QPushButton* startPauseBtn;
    QPushButton* stopBtn;
    QPushButton* expandBtn;
    QWidget* dashboard;
    QListWidget* subjectList;
};

#endif
