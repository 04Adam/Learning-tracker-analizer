// TrackerWindow.h
#ifndef LEARNINGTIMEANALIZER_TRACKERWINDOW_H
#define LEARNINGTIMEANALIZER_TRACKERWINDOW_H

#include "ChartWidget.h"
#include "Counter.h"

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

// Małe okienko ze stoperem, które można rozwinąć do pulpitu z wyborem przedmiotu.
// W układzie na telefon stoper jest na górze ekranu, a pulpit to zakładki pod nim.
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
    QDate pickDate(const QDate& initial, const QString& title);
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
    // Samoczynna synchronizacja nie pyta o nic i nie pokazuje błędów.
    void sync(bool automatic = false);
    void showSyncInfo();
    bool askSyncTarget();
    void reloadAfterSync();
    void updateTime();

    const bool mobile;
    Counter counter;
    int currentSubject = -1;

    ChartWidget::Range currentSection = ChartWidget::Range::Today;
    // Ponowne kliknięcie sekcji przełącza ją na kolejny wykres; trzeci ma tylko tydzień.
    ChartWidget::Mode todayMode = ChartWidget::Mode::Totals;
    ChartWidget::Mode weekMode = ChartWidget::Mode::Totals;
    // Poniedziałek wcześniejszego tygodnia wybranego z listy; nieprawidłowy,
    // gdy oglądana jest zwykła sekcja dnia albo tygodnia.
    QDate pastWeek;
    // Poniedziałek tygodnia, przed którym kończy się lista wcześniejszych tygodni.
    QDate listedBefore;

    QLabel* subjectLabel;
    QLabel* timeLabel;
    QPushButton* startPauseBtn;
    QPushButton* stopBtn;
    QPushButton* expandBtn;
    QWidget* dashboard;
    // Zakładki pulpitu w układzie na telefon; na komputerze ich nie ma.
    QTabWidget* tabs = nullptr;
    // Wcześniej oglądane zakładki, do których wraca przycisk Wstecz.
    QList<int> tabHistory;
    int shownTab = 0;
    QListWidget* subjectList;
    QPushButton* subjectColorBtn;
    QPushButton* syncBtn;
    // Otwarty panel z pytaniem w układzie na telefon i sposób na jego zamknięcie.
    QWidget* sheet = nullptr;
    std::function<void(int)> finishSheet;
    // Na komputerze: czy udało się zacząć czekać na telefon.
    bool syncListening = false;
    // Na telefonie: czy synchronizacja właśnie trwa i co pokazuje przycisk między nimi.
    bool syncing = false;
    QString syncIdleText = "Sync with computer";
    QLineEdit* todoInput;
    QComboBox* todoSubject;
    QPushButton* deadlineBtn;
    // Przy zaznaczonych zadaniach przycisk zmienia ich termin, a bez nich ustawia termin
    // dodawanego zadania; nieprawidłowy oznacza dzisiaj.
    QDate newTodoDeadline;
    QListWidget* todoList;
    TodoDelegate* todoDelegate;
    QPushButton* solveBtn;
    QVariantAnimation* solveAnimation;
    // Odmierza długie przytrzymanie zadania, które na telefonie otwiera jego menu.
    QTimer* holdTimer;
    QPoint holdPos;
    QPushButton* todayBtn;
    QPushButton* weekBtn;
    QComboBox* weekPicker;
    QComboBox* chartSubject;
    ChartWidget* chart;
};

#endif
