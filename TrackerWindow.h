// TrackerWindow.h
#ifndef LEARNINGTIMEANALIZER_TRACKERWINDOW_H
#define LEARNINGTIMEANALIZER_TRACKERWINDOW_H

#include "ChartWidget.h"
#include "Counter.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QVariantAnimation;
class TodoDelegate;

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
    void addTodoFromInput();
    void refreshTodos();
    void solveCheckedTodos();
    void removeSolvedTodos();
    void updateSolveButton();
    void showTodoMenu(const QPoint& pos);
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
    QLineEdit* todoInput;
    QComboBox* todoSubject;
    QListWidget* todoList;
    TodoDelegate* todoDelegate;
    QPushButton* solveBtn;
    QVariantAnimation* solveAnimation;
    QPushButton* todayBtn;
    QPushButton* weekBtn;
    ChartWidget* chart;
};

#endif
