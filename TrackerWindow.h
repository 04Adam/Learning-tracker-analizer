// TrackerWindow.h
#ifndef LEARNINGTIMEANALIZER_TRACKERWINDOW_H
#define LEARNINGTIMEANALIZER_TRACKERWINDOW_H

#include "ChartWidget.h"
#include "Counter.h"

#include <optional>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTabWidget;
class QTimer;
class QVariantAnimation;
class TodoDelegate;

// Small stopwatch window that can be expanded into a dashboard with subject selection.
// In the phone layout the stopwatch is at the top of the screen and the dashboard is tabs below it.
class TrackerWindow : public QWidget {
public:
    explicit TrackerWindow(bool mobile = false);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void toggleRunning();
    void stop();
    void toggleDashboard();
    void selectSubject(int subjectId);
    void showSection(ChartWidget::Range section);
    void showPastWeek(const QDate& monday);
    void refreshSubjects();
    void refreshSubjectPickers();
    void addSubjectFromDialog();
    void changeSubjectColor();
    QColor pickColor(const QColor& initial, const QString& title);
    // std::nullopt when cancelled; an invalid date when "no due date" was chosen.
    std::optional<QDate> pickDate(const QDate& initial, const QString& title);
    void pickFromSheet(QComboBox* picker, const QString& title);
    int runSheet(const QString& text, QWidget* content, const QStringList& buttons);
    QString askText(const QString& title, const QString& label, const QString& initial,
                    bool* accepted);
    void showWarning(const QString& title, const QString& text);
    bool askYesNo(const QString& title, const QString& text);
    void refreshWeeks();
    void addTodoFromInput();
    void pickDeadline();
    void updateDeadlineButton();
    QList<QListWidgetItem*> checkedTodos() const;
    void refreshTodos();
    void solveCheckedTodos();
    void removeSolvedTodos();
    void updateSolveButton();
    void showTodoMenu(const QPoint& pos);
    void updateControls();
    void updateNotification();
    void goBack();
    // Automatic sync asks nothing and shows no errors.
    void sync(bool automatic = false);
    void showSyncInfo();
    bool askSyncTarget();
    void reloadAfterSync();
    void updateTime();

    const bool mobile;
    Counter counter;
    int currentSubject = -1;

    ChartWidget::Range currentSection = ChartWidget::Range::Today;
    // Clicking a section again switches it to the next chart; only the week has a third one.
    ChartWidget::Mode todayMode = ChartWidget::Mode::Totals;
    ChartWidget::Mode weekMode = ChartWidget::Mode::Totals;
    // Monday of the earlier week picked from the list; invalid
    // when the regular day or week section is being viewed.
    QDate pastWeek;
    // Monday of the week before which the list of earlier weeks ends.
    QDate listedBefore;

    QLabel* subjectLabel;
    QLabel* timeLabel;
    QPushButton* startPauseBtn;
    QPushButton* stopBtn;
    QPushButton* expandBtn;
    QWidget* dashboard;
    // Dashboard tabs in the phone layout; absent on desktop.
    QTabWidget* tabs = nullptr;
    // Previously viewed tabs the Back button returns to.
    QList<int> tabHistory;
    int shownTab = 0;
    QListWidget* subjectList;
    QPushButton* subjectColorBtn;
    QPushButton* syncBtn;
    // The open prompt panel in the phone layout and a way to close it.
    QWidget* sheet = nullptr;
    std::function<void(int)> finishSheet;
    // On desktop: whether waiting for the phone started successfully.
    bool syncListening = false;
    // On the phone: whether a sync is in progress and what the button between them shows.
    bool syncing = false;
    QString syncIdleText = "Sync with computer";
    QLineEdit* todoInput;
    QComboBox* todoSubject;
    QPushButton* deadlineBtn;
    // With tasks selected the button changes their due date, otherwise it sets the due date
    // of the task being added; invalid means today.
    QDate newTodoDeadline;
    // The task being added gets no due date and goes to the end of the list.
    bool newTodoWithoutDeadline = false;
    QListWidget* todoList;
    TodoDelegate* todoDelegate;
    QPushButton* solveBtn;
    QVariantAnimation* solveAnimation;
    // Times a long press on a task, which opens its menu on the phone.
    QTimer* holdTimer;
    QPoint holdPos;
    QPushButton* todayBtn;
    QPushButton* weekBtn;
    QComboBox* weekPicker;
    QComboBox* chartSubject;
    ChartWidget* chart;
};

#endif
