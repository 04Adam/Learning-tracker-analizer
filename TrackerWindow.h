// TrackerWindow.h
#ifndef LEARNINGTIMEANALIZER_TRACKERWINDOW_H
#define LEARNINGTIMEANALIZER_TRACKERWINDOW_H

#include "ChartWidget.h"
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
    void showSection(ChartWidget::Range section);
    void refreshSubjects();
    void updateControls();
    void updateTime();

    Counter counter;
    int currentSubject = -1;

    ChartWidget::Range currentSection = ChartWidget::Range::Today;
    // Ponowne kliknięcie sekcji przełącza ją między słupkami a liniami.
    ChartWidget::Mode todayMode = ChartWidget::Mode::Totals;
    ChartWidget::Mode weekMode = ChartWidget::Mode::Totals;

    QLabel* subjectLabel;
    QLabel* timeLabel;
    QPushButton* startPauseBtn;
    QPushButton* stopBtn;
    QPushButton* expandBtn;
    QWidget* dashboard;
    QListWidget* subjectList;
    QPushButton* todayBtn;
    QPushButton* weekBtn;
    ChartWidget* chart;
};

#endif
