// TrackerWindow.cpp
#include "TrackerWindow.h"
#include "AndroidBridge.h"
#include "Autostart.h"
#include "Database.h"
#include "Sync.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScroller>
#include <QSet>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

static QString formatTime(qint64 seconds) {
    return QString("%1:%2:%3")
        .arg(seconds / 3600)
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}

// An invalid color gives an empty square so the list texts stay aligned.
static QIcon swatchIcon(const QColor& color) {
    QPixmap swatch(12, 12);
    swatch.fill(color.isValid() ? color : Qt::transparent);
    // Without the Selected variant Qt darkens the color of the selected row.
    QIcon icon(swatch);
    icon.addPixmap(swatch, QIcon::Selected);
    return icon;
}

static const char* const kSubjectColors[] = {"#d93f4c", "#2fb37a", "#3b8fe6", "#c99a16", "#a06be0",
                                             "#e07a3f", "#3fb8c4", "#d65fa8", "#8fb33a", "#8a8d93"};

// The first hue not yet used by any subject.
static QColor suggestedSubjectColor(const QList<SubjectTotal>& subjects) {
    for (const char* color : kSubjectColors) {
        const bool used = std::any_of(subjects.begin(), subjects.end(),
                                      [&](const SubjectTotal& s) { return s.color == QColor(color); });
        if (!used) return QColor(color);
    }
    return QColor(kSubjectColors[0]);
}

enum TodoRole {
    TodoIdRole = Qt::UserRole,
    TodoColorRole,
    TodoDeadlineRole,
    // Task already solved, just waiting for the fade-out animation to end.
    TodoSolvingRole,
};

struct DaysLeft {
    QString text;
    QColor color;
};

// How many days are left until the due date and how urgent it is; empty when the task has no due date.
static DaysLeft daysLeft(const QDate& deadline) {
    if (!deadline.isValid()) return {};

    const qint64 days = QDate::currentDate().daysTo(deadline);
    const QColor color = days >= 7 ? "#3fb950" : days > 3 ? "#e3c62f" : days > 1 ? "#f0883e"
                                                                                : "#f0524f";
    if (days == 0) return {"today", color};
    if (days < 0) return {QString(days == -1 ? "%1 day late" : "%1 days late").arg(-days), color};
    return {QString(days == 1 ? "%1 day" : "%1 days").arg(days), color};
}

// Adds the days left until the due date on the right side of a task, and on tasks being solved
// draws a bar in the subject's color, growing from the left.
class TodoDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    double progress = 0;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        // The task text wraps before the day counter, not under it.
        const int counter = counterWidth(option, index);
        QStyleOptionViewItem textOption = option;
        if (textOption.rect.width() > counter)
            textOption.rect.setWidth(textOption.rect.width() - counter);
        const QSize size = QStyledItemDelegate::sizeHint(textOption, index);
        return {size.width() + counter, size.height()};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const DaysLeft left = daysLeft(index.data(TodoDeadlineRole).toDate());
        QStyleOptionViewItem textOption = option;
        textOption.rect.setWidth(option.rect.width() - counterWidth(option, index));
        QStyledItemDelegate::paint(painter, textOption, index);

        if (!left.text.isEmpty()) {
            painter->save();
            painter->setFont(counterFont(option));
            painter->setPen(left.color);
            painter->drawText(option.rect.adjusted(0, 0, -kCounterMargin, 0),
                              Qt::AlignRight | Qt::AlignVCenter, left.text);
            painter->restore();
        }
        if (!index.data(TodoSolvingRole).toBool()) return;

        const QColor subjectColor = index.data(TodoColorRole).value<QColor>();
        QRectF bar = option.rect;
        bar.setWidth(bar.width() * progress);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(subjectColor.isValid() ? subjectColor : QColor("#8a8d93"));
        painter->drawRoundedRect(bar, 3, 3);
        painter->restore();
    }

private:
    static constexpr int kCounterMargin = 6;

    static QFont counterFont(const QStyleOptionViewItem& option) {
        QFont font = option.font;
        font.setBold(true);
        return font;
    }

    static int counterWidth(const QStyleOptionViewItem& option, const QModelIndex& index) {
        const QString text = daysLeft(index.data(TodoDeadlineRole).toDate()).text;
        if (text.isEmpty()) return 0;
        return QFontMetrics(counterFont(option)).horizontalAdvance(text) + 2 * kCounterMargin;
    }
};

// On the phone a drop-down list opens as a panel in the main window, not a separate window:
// a separate window can crash Qt on Android and slides under the status bar.
class SheetComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;

    std::function<void()> openSheet;

    void showPopup() override {
        if (openSheet)
            openSheet();
        else
            QComboBox::showPopup();
    }
};

// Day: bars ↔ lines; the week also has a third chart with bars for each day.
static ChartWidget::Mode nextMode(ChartWidget::Mode mode, ChartWidget::Range range) {
    switch (mode) {
        case ChartWidget::Mode::Totals:
            return ChartWidget::Mode::Cumulative;
        case ChartWidget::Mode::Cumulative:
            return range == ChartWidget::Range::Week ? ChartWidget::Mode::Daily
                                                     : ChartWidget::Mode::Totals;
        case ChartWidget::Mode::Daily:
            break;
    }
    return ChartWidget::Mode::Totals;
}

TrackerWindow::TrackerWindow(bool mobile) : mobile(mobile) {
    if (!mobile)
        // Without Qt::Tool the window gets a taskbar entry it can be restored from after minimizing.
        setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setObjectName("tracker");
    setAttribute(Qt::WA_StyledBackground);
    QString style =
        "#tracker { border: 1px solid #43454a; }"
        "QPushButton[section=true] { padding: 5px 22px; border: 1px solid #43454a;"
        " border-radius: 4px; background: #2b2d30; }"
        "QPushButton[section=true]:hover { background: #35373b; }"
        "QPushButton[section=true]:checked { background: #2f65ca; border-color: #2f65ca;"
        " color: white; }"
        "#todoList::indicator { width: 12px; height: 12px; border: 1px solid white;"
        " border-radius: 2px; }"
        "#todoList::indicator:checked { background: white; }";
    // On the phone everything you tap with a finger must be noticeably larger.
    if (mobile) {
        style +=
            "#tracker { border: none; }"
            "QPushButton, QLineEdit, QComboBox { min-height: 40px; }"
            "QTabBar::tab { min-height: 44px; }"
            "QListWidget::item { min-height: 44px; }"
            "QComboBox QAbstractItemView::item { min-height: 40px; }"
            "QMenu::item { padding: 12px 28px; }"
            "QMenu::icon { padding-left: 12px; }"
            "QMenu::item:selected { background: #2f65ca; }"
            "#shade { background: rgba(0, 0, 0, 170); }"
            "#sheet { background: #2b2d30; border: 1px solid #43454a; border-radius: 8px; }";
    }
    setStyleSheet(style);

    subjectLabel  = new QLabel(this);
    timeLabel     = new QLabel(this);
    startPauseBtn = new QPushButton(this);
    stopBtn       = new QPushButton("Stop", this);
    expandBtn     = new QPushButton(this);
    auto* minimizeBtn = new QPushButton("–", this);
    auto* quitBtn = new QPushButton("✕", this);

    QFont timeFont = timeLabel->font();
    timeFont.setBold(true);
    // On Android the font size is in pixels and pointSize() alone returns -1.
    const qreal timeSize = QFontInfo(timeFont).pointSizeF();
    timeFont.setPointSizeF(mobile ? timeSize * 3 : timeSize + 2);
    timeLabel->setFont(timeFont);

    expandBtn->setFixedWidth(30);
    minimizeBtn->setFixedWidth(30);
    minimizeBtn->setToolTip("Minimize");
    quitBtn->setFixedWidth(30);
    quitBtn->setToolTip("Quit");

    if (mobile)
        tabs = new QTabWidget(this);
    dashboard = mobile ? tabs : new QWidget(this);
    subjectList = new QListWidget(dashboard);
    subjectList->setSpacing(2);
    auto* addSubjectBtn = new QPushButton("Add subject…", dashboard);
    subjectColorBtn = new QPushButton("Change color…", dashboard);
    syncBtn = new QPushButton(mobile ? "Sync with computer" : "Sync with phone…", dashboard);

    todoInput = new QLineEdit(dashboard);
    todoInput->setPlaceholderText("New task…");
    todoSubject = new SheetComboBox(dashboard);
    deadlineBtn = new QPushButton(dashboard);
    auto* addTodoBtn = new QPushButton("Add", dashboard);
    todoList = new QListWidget(dashboard);
    todoList->setObjectName("todoList");
    todoList->setContextMenuPolicy(Qt::CustomContextMenu);
    todoList->setSpacing(2);
    todoList->setWordWrap(true);
    // Tasks are not selected, only checked off.
    todoList->setSelectionMode(QAbstractItemView::NoSelection);
    todoList->setFocusPolicy(Qt::NoFocus);
    todoDelegate = new TodoDelegate(todoList);
    todoList->setItemDelegate(todoDelegate);
    solveBtn = new QPushButton("Solve", dashboard);

    solveAnimation = new QVariantAnimation(this);
    solveAnimation->setStartValue(0.0);
    solveAnimation->setEndValue(1.0);
    solveAnimation->setDuration(550);
    solveAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    holdTimer = new QTimer(this);
    holdTimer->setSingleShot(true);
    holdTimer->setInterval(600);

    todayBtn = new QPushButton("Today", dashboard);
    weekBtn  = new QPushButton("Week", dashboard);
    todayBtn->setProperty("section", true);
    weekBtn->setProperty("section", true);
    todayBtn->setCheckable(true);
    weekBtn->setCheckable(true);
    weekPicker = new SheetComboBox(dashboard);
    weekPicker->setPlaceholderText("Previous weeks");
    weekPicker->setMinimumContentsLength(15);
    weekPicker->setMaxVisibleItems(12);
    chartSubject = new SheetComboBox(dashboard);
    auto* hint = new QLabel(mobile ? "Tap again: bars → over time → week by day"
                                   : "Click again: bars → over time → week by day", dashboard);
    hint->setEnabled(false);
    chart = new ChartWidget(dashboard);
    chart->setInlineDetails(mobile);

    auto* subjectButtons = new QHBoxLayout;
    subjectButtons->addWidget(addSubjectBtn);
    subjectButtons->addWidget(subjectColorBtn);

    auto* todoEntry = new QHBoxLayout;
    todoEntry->addWidget(todoSubject, 1);
    todoEntry->addWidget(deadlineBtn);
    todoEntry->addWidget(addTodoBtn);

    auto* sections = new QHBoxLayout;
    auto* layout = new QVBoxLayout(this);

    if (mobile) {
        expandBtn->hide();
        minimizeBtn->hide();
        quitBtn->hide();

        auto* subjectPage = new QWidget;
        auto* subjectColumn = new QVBoxLayout(subjectPage);
        subjectColumn->addWidget(subjectList, 1);
        subjectColumn->addLayout(subjectButtons);
        subjectColumn->addWidget(syncBtn);

        auto* todoPage = new QWidget;
        auto* todoColumn = new QVBoxLayout(todoPage);
        todoColumn->addWidget(todoInput);
        todoColumn->addLayout(todoEntry);
        todoColumn->addWidget(todoList, 1);
        todoColumn->addWidget(solveBtn);

        sections->addWidget(todayBtn, 1);
        sections->addWidget(weekBtn, 1);
        auto* filters = new QHBoxLayout;
        filters->addWidget(weekPicker, 1);
        filters->addWidget(chartSubject, 1);
        hint->setAlignment(Qt::AlignHCenter);

        auto* chartPage = new QWidget;
        auto* chartColumn = new QVBoxLayout(chartPage);
        chartColumn->addLayout(sections);
        chartColumn->addLayout(filters);
        chartColumn->addWidget(hint);
        chartColumn->addWidget(chart, 1);

        tabs->setTabPosition(QTabWidget::South);
        tabs->setDocumentMode(true);
        // Document mode stops tabs from stretching to the full width.
        tabs->tabBar()->setExpanding(true);
        tabs->addTab(subjectPage, "Subjects");
        tabs->addTab(todoPage, "To do");
        tabs->addTab(chartPage, "Charts");

        // Without this the list scrolls only with the scrollbar, not by dragging a finger.
        for (QListWidget* list : {subjectList, todoList}) {
            list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
        }
        todoList->viewport()->installEventFilter(this);
        // The Back button reaches the window before any widget sees it.
        qApp->installEventFilter(this);
        connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
            tabHistory.append(shownTab);
            shownTab = index;
        });
        const struct { QComboBox* picker; const char* title; } pickers[] = {
            {todoSubject, "Subject of the new task"},
            {weekPicker, "Previous week"},
            {chartSubject, "Show subject"},
        };
        for (const auto& p : pickers) {
            static_cast<SheetComboBox*>(p.picker)->openSheet =
                [this, picker = p.picker, title = QString(p.title)] { pickFromSheet(picker, title); };
        }

        auto* buttons = new QHBoxLayout;
        buttons->addWidget(startPauseBtn);
        buttons->addWidget(stopBtn);

        layout->addWidget(subjectLabel, 0, Qt::AlignHCenter);
        layout->addWidget(timeLabel, 0, Qt::AlignHCenter);
        layout->addLayout(buttons);
        layout->addWidget(dashboard, 1);
    } else {
        subjectList->setFixedWidth(260);
        todoList->setFixedWidth(260);

        auto* bar = new QHBoxLayout;
        bar->addWidget(subjectLabel);
        bar->addWidget(timeLabel);
        bar->addStretch();
        bar->addWidget(startPauseBtn);
        bar->addWidget(stopBtn);
        bar->addWidget(expandBtn);
        bar->addWidget(minimizeBtn);
        bar->addWidget(quitBtn);

        auto* subjectColumn = new QVBoxLayout;
        subjectColumn->addWidget(new QLabel("What are you studying now?", dashboard));
        subjectColumn->addWidget(subjectList, 1);
        subjectColumn->addLayout(subjectButtons);
        subjectColumn->addWidget(syncBtn);

        subjectColumn->addSpacing(8);
        subjectColumn->addWidget(new QLabel("To do", dashboard));
        subjectColumn->addWidget(todoInput);
        subjectColumn->addLayout(todoEntry);
        subjectColumn->addWidget(todoList, 2);
        subjectColumn->addWidget(solveBtn);

        sections->addWidget(todayBtn);
        sections->addWidget(weekBtn);
        sections->addWidget(weekPicker);
        sections->addWidget(chartSubject);
        sections->addSpacing(12);
        sections->addWidget(hint);
        sections->addStretch();

        auto* chartColumn = new QVBoxLayout;
        chartColumn->addLayout(sections);
        chartColumn->addWidget(chart, 1);

        auto* dashboardLayout = new QHBoxLayout(dashboard);
        dashboardLayout->setContentsMargins(0, 8, 0, 0);
        dashboardLayout->setSpacing(16);
        dashboardLayout->addLayout(subjectColumn);
        dashboardLayout->addLayout(chartColumn, 1);
        dashboard->hide();

        layout->addLayout(bar);
        layout->addWidget(dashboard);
        // The window adjusts its size to whether the dashboard is expanded.
        layout->setSizeConstraint(QLayout::SetFixedSize);
    }

    connect(startPauseBtn, &QPushButton::clicked, this, [this] { toggleRunning(); });
    connect(stopBtn, &QPushButton::clicked, this, [this] { stop(); });
    connect(expandBtn, &QPushButton::clicked, this, [this] { toggleDashboard(); });
    connect(minimizeBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(quitBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(todayBtn, &QPushButton::clicked, this, [this] { showSection(ChartWidget::Range::Today); });
    connect(weekBtn, &QPushButton::clicked, this, [this] { showSection(ChartWidget::Range::Week); });
    // The signal also fires when the same week is picked again.
    connect(weekPicker, &QComboBox::activated, this,
            [this](int index) { showPastWeek(weekPicker->itemData(index).toDate()); });
    connect(chartSubject, &QComboBox::currentIndexChanged, this,
            [this](int index) { chart->setSubject(chartSubject->itemData(index).toInt()); });
    connect(subjectList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item)
            selectSubject(item->data(Qt::UserRole).toInt());
    });
    connect(addSubjectBtn, &QPushButton::clicked, this, [this] { addSubjectFromDialog(); });
    connect(subjectColorBtn, &QPushButton::clicked, this, [this] { changeSubjectColor(); });
    connect(syncBtn, &QPushButton::clicked, this, [this] { sync(); });
    // The computer waits for the phone for as long as the program runs.
    if (!mobile)
        syncListening = startSyncServer(this, [this] { reloadAfterSync(); });
    connect(todoInput, &QLineEdit::returnPressed, this, [this] { addTodoFromInput(); });
    connect(addTodoBtn, &QPushButton::clicked, this, [this] { addTodoFromInput(); });
    connect(deadlineBtn, &QPushButton::clicked, this, [this] { pickDeadline(); });
    connect(todoList, &QListWidget::itemChanged, this, [this] { updateSolveButton(); });
    // On the phone tapping the whole row checks off a task, not just the checkbox.
    if (mobile) {
        connect(todoList, &QListWidget::itemClicked, this, [](QListWidgetItem* item) {
            if (!item->data(TodoSolvingRole).toBool())
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        });
    }
    connect(solveBtn, &QPushButton::clicked, this, [this] { solveCheckedTodos(); });
    connect(solveAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        todoDelegate->progress = value.toDouble();
        todoList->viewport()->update();
    });
    connect(solveAnimation, &QVariantAnimation::finished, this, [this] { removeSolvedTodos(); });
    connect(todoList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { showTodoMenu(pos); });
    connect(holdTimer, &QTimer::timeout, this, [this] { showTodoMenu(holdPos); });
    setNotificationActionHandler([this](const QString& action) {
        if (action == "toggle")
            toggleRunning();
        else if (action == "stop")
            stop();
    });
    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        counter.stop();
        hideTimerNotification();
    });
    // Android can kill a background app without aboutToQuit, so save when leaving the screen.
    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) {
            counter.checkpoint();
            // Changes from the phone reach the computer before the app leaves the screen.
            if (this->mobile)
                sync(true);
        } else if (this->mobile) {
            updateTime();
            refreshSubjects();
            // A notification sent before the user allowed notifications was lost.
            updateNotification();
            sync(true);
        }
    });

    // A hidden app on the phone keeps counting and saving time, but has nothing to draw.
    const auto hidden = [this] {
        return this->mobile && QApplication::applicationState() != Qt::ApplicationActive;
    };

    auto* tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, [this, hidden] {
        if (!hidden())
            updateTime();
    });
    tick->start(1000);

    auto* autosave = new QTimer(this);
    connect(autosave, &QTimer::timeout, this, [this, hidden] {
        counter.checkpoint();
        if (!hidden()) {
            refreshSubjects();
            // After midnight every due date is one day closer.
            updateDeadlineButton();
            todoList->viewport()->update();
        }
    });
    autosave->start(10000);

    // With the app open the phone regularly exchanges data with the computer if it is on the network.
    if (mobile) {
        auto* autosync = new QTimer(this);
        connect(autosync, &QTimer::timeout, this, [this, hidden] {
            if (!hidden())
                sync(true);
        });
        autosync->start(2 * 60 * 1000);
        QTimer::singleShot(0, this, [this] { sync(true); });
        // A closed app syncs in the background; if it is running at the time, it refreshes the view.
        onBackgroundSync([this] {
            syncIdleText = "Synced at " + QTime::currentTime().toString("HH:mm");
            syncBtn->setText(syncIdleText);
            reloadAfterSync();
        });
    }

    refreshSubjectPickers();
    refreshSubjects();
    refreshTodos();
    updateDeadlineButton();
    if (subjectList->count() > 0)
        subjectList->setCurrentRow(0);
    updateControls();
    updateTime();
}

void TrackerWindow::mousePressEvent(QMouseEvent* event) {
    // The window is frameless, so it is moved by dragging the background.
    if (!mobile && event->button() == Qt::LeftButton && windowHandle())
        windowHandle()->startSystemMove();
}

void TrackerWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (sheet)
        sheet->setGeometry(rect());
}

bool TrackerWindow::eventFilter(QObject* watched, QEvent* event) {
    // An unhandled Back button would close the app along with the stopwatch.
    const bool isKey = event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease;
    if (isKey && watched == windowHandle()
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Back) {
        if (event->type() == QEvent::KeyRelease)
            goBack();
        return true;
    }

    // On the phone a long press on a task replaces the right mouse button.
    if (watched == todoList->viewport()) {
        switch (event->type()) {
            case QEvent::MouseButtonPress:
                holdPos = static_cast<QMouseEvent*>(event)->position().toPoint();
                holdTimer->start();
                break;
            case QEvent::MouseMove: {
                const QPoint pos = static_cast<QMouseEvent*>(event)->position().toPoint();
                if ((pos - holdPos).manhattanLength() > QApplication::startDragDistance())
                    holdTimer->stop();
                break;
            }
            case QEvent::MouseButtonRelease:
                holdTimer->stop();
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TrackerWindow::goBack() {
    if (finishSheet) {
        finishSheet(-1);
    } else if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
    } else if (!tabHistory.isEmpty()) {
        // Going back is not another step to remember.
        const QSignalBlocker blocker(tabs);
        shownTab = tabHistory.takeLast();
        tabs->setCurrentIndex(shownTab);
    } else {
        moveAppToBackground();
    }
}

void TrackerWindow::updateNotification() {
    if (counter.state() == Counter::State::Stopped)
        hideTimerNotification();
    else
        showTimerNotification(subjectLabel->text(), counter.elapsed(),
                              counter.state() == Counter::State::Running);
}

void TrackerWindow::sync(bool automatic) {
    if (!mobile) {
        showSyncInfo();
        return;
    }

    if (syncing) return;
    if (normalizePairingCode(syncSetting("code").toString()).isEmpty()
        && (automatic || !askSyncTarget()))
        return;

    // The running session must reach the computer with the current time.
    counter.checkpoint();
    syncing = true;
    if (!automatic) {
        syncBtn->setEnabled(false);
        syncBtn->setText("Syncing…");
    }
    syncNow(this, [this, automatic](const QString& error, bool changed) {
        syncing = false;
        syncBtn->setEnabled(true);
        if (!error.isEmpty()) {
            syncBtn->setText(syncIdleText);
            if (!automatic
                && askYesNo("Sync", error + "\n\nChange the pairing code or the computer's "
                                    "address?")
                && askSyncTarget())
                sync();
            return;
        }
        syncIdleText = "Synced at " + QTime::currentTime().toString("HH:mm");
        syncBtn->setText(syncIdleText);
        if (changed)
            reloadAfterSync();
    });
}

void TrackerWindow::showSyncInfo() {
    QMessageBox box(QMessageBox::Information, "Sync with phone",
        syncListening
            ? QString("Keep this program running. The phone syncs by itself whenever it is on "
                      "the same Wi-Fi, even with its app closed.\n\n"
                      "Pairing code to enter on the phone once:\n%1\n\n"
                      "Address (only if the phone cannot find this computer): %2")
                  .arg(pairingCode(), syncAddresses().join("  or  "))
            : QString("Sync is not available: another program is using its network port."),
        QMessageBox::Ok, this);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* autostart = new QCheckBox("Start this program when I log in", &box);
    autostart->setChecked(autostartEnabled());
    box.setCheckBox(autostart);
    box.exec();
    if (autostart->isChecked() != autostartEnabled())
        setAutostart(autostart->isChecked());
}

bool TrackerWindow::askSyncTarget() {
    bool accepted = false;
    const QString code = askText("Sync", "Pairing code (the computer shows it under "
                                 "\"Sync with phone…\"):",
                                 syncSetting("code").toString(), &accepted).trimmed();
    if (!accepted) return false;
    if (normalizePairingCode(code).isEmpty()) {
        showWarning("Sync", "That is not a pairing code. It has 20 letters and digits, "
                            "like ABCD-1234-EFGH-5678-JKMN.");
        return false;
    }

    const QString host = askText("Sync", "Computer address (leave empty to find it "
                                 "automatically):",
                                 syncSetting("host").toString(), &accepted).trimmed();
    if (!accepted) return false;

    setSyncSetting("host", host);
    setSyncSetting("code", formatPairingCode(code));
    return true;
}

void TrackerWindow::reloadAfterSync() {
    // Weeks older than those on the list may have arrived from the other device.
    listedBefore = {};
    refreshSubjectPickers();
    refreshSubjects();
    refreshTodos();
    updateControls();
}

void TrackerWindow::toggleRunning() {
    switch (counter.state()) {
        case Counter::State::Stopped:
            requestNotificationPermission();
            counter.start(currentSubject);
            break;
        case Counter::State::Running: counter.pause(); break;
        case Counter::State::Paused:  counter.resume(); break;
    }
    refreshSubjects();
    updateControls();
    updateTime();
    updateNotification();
    if (mobile && counter.state() == Counter::State::Paused)
        sync(true);
}

void TrackerWindow::stop() {
    counter.stop();
    refreshSubjects();
    updateControls();
    updateTime();
    updateNotification();
    if (mobile)
        sync(true);
}

void TrackerWindow::toggleDashboard() {
    const QPoint topRight = frameGeometry().topRight();
    const QRect available = screen()->availableGeometry();

    if (dashboard->isHidden()) {
        dashboard->setFixedSize(qMin(1280, available.width() - 80),
                                qMin(760, available.height() - 140));
        refreshSubjects();
    }
    dashboard->setVisible(dashboard->isHidden());
    updateControls();

    // Keep the top-right corner in place, but do not let the window go off screen.
    adjustSize();
    move(qMax(available.left(), topRight.x() - width() + 1),
         qMin(topRight.y(), qMax(available.top(), available.bottom() - height())));
}

void TrackerWindow::showSection(ChartWidget::Range section) {
    ChartWidget::Mode& mode = section == ChartWidget::Range::Today ? todayMode : weekMode;
    // From an earlier week the section button first returns to the current period.
    if (section == currentSection && !pastWeek.isValid())
        mode = nextMode(mode, section);
    currentSection = section;
    pastWeek = {};
    chart->setView(section, mode);
    updateControls();
}

void TrackerWindow::showPastWeek(const QDate& monday) {
    // Earlier weeks share the mode with the week section; picking again switches it.
    if (monday == pastWeek)
        weekMode = nextMode(weekMode, ChartWidget::Range::Week);
    currentSection = ChartWidget::Range::Week;
    pastWeek = monday;
    chart->setView(ChartWidget::Range::Week, weekMode, monday);
    updateControls();
}

void TrackerWindow::selectSubject(int subjectId) {
    if (subjectId == currentSubject) return;

    // Changing the subject while studying closes the current measurement and starts a new one.
    const bool wasRunning = counter.state() == Counter::State::Running;
    counter.stop();
    currentSubject = subjectId;
    if (wasRunning)
        counter.start(currentSubject);

    refreshSubjects();
    updateControls();
    updateTime();
    updateNotification();
}

void TrackerWindow::refreshSubjects() {
    const QSignalBlocker blocker(subjectList);
    const QList<SubjectTotal> subjects = subjectsWithTodayTotals();

    // Update rows in place: the list is sometimes refreshed from its own signal.
    for (int row = 0; row < subjects.size(); ++row) {
        const SubjectTotal& s = subjects[row];
        auto* item = row < subjectList->count() ? subjectList->item(row)
                                                : new QListWidgetItem(subjectList);
        item->setText(QString("%1 — today %2").arg(s.name, formatTime(s.todaySeconds)));
        item->setData(Qt::UserRole, s.id);
        item->setIcon(swatchIcon(s.color));
        if (s.id == currentSubject) {
            subjectList->setCurrentItem(item);
            subjectLabel->setText(s.name);
        }
    }

    refreshWeeks();
    if (!dashboard->isHidden())
        chart->reload();
}

void TrackerWindow::refreshSubjectPickers() {
    const QList<SubjectTotal> subjects = subjectsWithTodayTotals();
    const struct { QComboBox* picker; const char* none; } pickers[] = {
        {todoSubject, "No subject"},
        {chartSubject, "All subjects"},
    };
    for (const auto& p : pickers) {
        // Subjects are only ever added, so the previous selection can always be restored.
        const QSignalBlocker blocker(p.picker);
        const QVariant chosen = p.picker->count() > 0 ? p.picker->currentData() : QVariant(-1);
        p.picker->clear();
        p.picker->addItem(swatchIcon({}), p.none, -1);
        for (const SubjectTotal& s : subjects)
            p.picker->addItem(swatchIcon(s.color), s.name, s.id);
        p.picker->setCurrentIndex(p.picker->findData(chosen));
    }
}

void TrackerWindow::addSubjectFromDialog() {
    bool accepted = false;
    const QString name = askText("Add subject", "Subject name:", {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;

    const QList<SubjectTotal> subjects = subjectsWithTodayTotals();
    for (const SubjectTotal& s : subjects) {
        if (s.name.compare(name, Qt::CaseInsensitive) != 0) continue;
        showWarning("Add subject", QString("Subject \"%1\" already exists.").arg(s.name));
        return;
    }

    const QColor color = pickColor(suggestedSubjectColor(subjects), "Color for " + name);
    if (!color.isValid() || !addSubject(name, color)) return;

    refreshSubjectPickers();
    refreshSubjects();
    updateControls();
}

void TrackerWindow::changeSubjectColor() {
    for (const SubjectTotal& s : subjectsWithTodayTotals()) {
        if (s.id != currentSubject) continue;

        const QColor color = pickColor(s.color, "Color for " + s.name);
        if (!color.isValid()) return;

        setSubjectColor(s.id, color);
        refreshSubjectPickers();
        refreshSubjects();
        refreshTodos();
        return;
    }
}

QColor TrackerWindow::pickColor(const QColor& initial, const QString& title) {
    if (!mobile)
        return QColorDialog::getColor(initial, this, title);

    // QColorDialog does not fit on a phone screen, so the choice is from a preset palette.
    auto* palette = new QWidget;
    auto* grid = new QGridLayout(palette);
    grid->setContentsMargins(0, 0, 0, 0);

    QColor chosen;
    int index = 0;
    for (const char* color : kSubjectColors) {
        auto* swatch = new QPushButton(palette);
        swatch->setFixedSize(52, 52);
        swatch->setStyleSheet(QString("background: %1; border-radius: 6px; border: %2;")
                                  .arg(color, QColor(color) == initial ? "3px solid white" : "none"));
        connect(swatch, &QPushButton::clicked, this, [this, &chosen, color] {
            chosen = QColor(color);
            finishSheet(0);
        });
        grid->addWidget(swatch, index / 5, index % 5);
        ++index;
    }
    runSheet(title, palette, {"Cancel"});
    return chosen;
}

// Returns an invalid date when the selection was cancelled.
std::optional<QDate> TrackerWindow::pickDate(const QDate& initial, const QString& title) {
    auto* calendar = new QCalendarWidget;
    // Month and day names in English, like the rest of the program.
    calendar->setLocale(QLocale::English);
    calendar->setFirstDayOfWeek(Qt::Monday);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setMinimumDate(QDate::currentDate());
    calendar->setSelectedDate(initial);

    if (mobile) {
        // Days must be large enough to hit with a finger.
        calendar->setMinimumHeight(340);
        // The calendar disappears with the panel, so the selection must be stored as it changes.
        QDate chosen = calendar->selectedDate();
        connect(calendar, &QCalendarWidget::selectionChanged, this,
                [&chosen, calendar] { chosen = calendar->selectedDate(); });
        switch (runSheet(title, calendar, {"Cancel", "No deadline", "OK"})) {
            case 1: return QDate();
            case 2: return chosen;
            default: return std::nullopt;
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* column = new QVBoxLayout(&dialog);
    column->addWidget(calendar);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(calendar, &QCalendarWidget::activated, &dialog, &QDialog::accept);
    // A separate dialog result to tell "no due date" apart from cancelling.
    const int noDeadline = QDialog::Accepted + 1;
    connect(buttons->addButton("No deadline", QDialogButtonBox::ResetRole), &QPushButton::clicked,
            &dialog, [&dialog, noDeadline] { dialog.done(noDeadline); });
    switch (dialog.exec()) {
        case QDialog::Accepted: return calendar->selectedDate();
        case noDeadline: return QDate();
        default: return std::nullopt;
    }
}

// On the phone prompts are a panel inside the main window: separate Qt dialogs
// can crash the app there or not fit on the screen.
// Returns the index of the pressed button, or -1 after the Back button.
int TrackerWindow::runSheet(const QString& text, QWidget* content, const QStringList& buttons) {
    QWidget shade(this);
    shade.setObjectName("shade");
    shade.setAttribute(Qt::WA_StyledBackground);
    shade.setGeometry(rect());

    auto* panel = new QFrame(&shade);
    panel->setObjectName("sheet");
    auto* label = new QLabel(text, panel);
    label->setWordWrap(true);

    QEventLoop loop;
    int result = -1;
    auto* row = new QHBoxLayout;
    for (int i = 0; i < buttons.size(); ++i) {
        auto* button = new QPushButton(buttons[i], panel);
        connect(button, &QPushButton::clicked, &shade, [this, i] { finishSheet(i); });
        row->addWidget(button);
    }
    // Enter in the text field acts like the last button.
    if (auto* input = qobject_cast<QLineEdit*>(content)) {
        connect(input, &QLineEdit::returnPressed, &shade,
                [this, last = buttons.size() - 1] { finishSheet(last); });
    }

    auto* column = new QVBoxLayout(panel);
    column->addWidget(label);
    if (content)
        column->addWidget(content);
    column->addLayout(row);

    // The panel is in the upper part of the screen so the keyboard does not cover it.
    auto* outer = new QVBoxLayout(&shade);
    outer->addStretch(1);
    outer->addWidget(panel);
    outer->addStretch(3);

    sheet = &shade;
    finishSheet = [&](int button) {
        result = button;
        loop.quit();
    };
    shade.show();
    shade.raise();
    if (content)
        content->setFocus();
    loop.exec();
    finishSheet = nullptr;
    sheet = nullptr;
    return result;
}

void TrackerWindow::pickFromSheet(QComboBox* picker, const QString& title) {
    auto* list = new QListWidget;
    list->setSpacing(2);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
    for (int i = 0; i < picker->count(); ++i)
        new QListWidgetItem(picker->itemIcon(i), picker->itemText(i), list);
    list->setCurrentRow(picker->currentIndex());
    // The whole list if it fits in the part of the screen above the keyboard; a longer one scrolls.
    const int rows = list->count() * (list->sizeHintForRow(0) + 2 * list->spacing());
    list->setFixedHeight(std::min(rows + 2 * list->frameWidth(), height() * 3 / 5));
    list->scrollToItem(list->currentItem(), QAbstractItemView::PositionAtCenter);

    int chosen = -1;
    connect(list, &QListWidget::itemClicked, this, [this, list, &chosen](QListWidgetItem* item) {
        chosen = list->row(item);
        finishSheet(0);
    });
    runSheet(title, list, {"Cancel"});
    if (chosen < 0) return;

    picker->setCurrentIndex(chosen);
    // Like a regular list: activated also when the same item is picked again.
    emit picker->activated(chosen);
}

QString TrackerWindow::askText(const QString& title, const QString& label,
                               const QString& initial, bool* accepted) {
    if (!mobile)
        return QInputDialog::getText(this, title, label, QLineEdit::Normal, initial, accepted);

    // The field disappears with the panel, so the text must be stored as it changes.
    QString text = initial;
    auto* input = new QLineEdit(initial);
    connect(input, &QLineEdit::textChanged, this, [&text](const QString& now) { text = now; });
    *accepted = runSheet(label, input, {"Cancel", "OK"}) == 1;
    return text;
}

void TrackerWindow::showWarning(const QString& title, const QString& text) {
    if (mobile)
        runSheet(text, nullptr, {"OK"});
    else
        QMessageBox::warning(this, title, text);
}

bool TrackerWindow::askYesNo(const QString& title, const QString& text) {
    if (mobile)
        return runSheet(text, nullptr, {"No", "Yes"}) == 1;
    return QMessageBox::question(this, title, text) == QMessageBox::Yes;
}

void TrackerWindow::refreshWeeks() {
    // The list changes only when a week ends: the past week is added to it.
    const QDate today = QDate::currentDate();
    const QDate thisMonday = today.addDays(1 - today.dayOfWeek());
    if (thisMonday == listedBefore) return;
    listedBefore = thisMonday;

    const QSignalBlocker blocker(weekPicker);
    weekPicker->clear();

    // From last week back to the week with the oldest study session.
    const qint64 firstStart = firstSessionStart();
    if (firstStart > 0) {
        const QDate firstDay = QDateTime::fromSecsSinceEpoch(firstStart).date();
        for (QDate monday = thisMonday.addDays(-7); monday.addDays(7) > firstDay;
             monday = monday.addDays(-7)) {
            const QString format = monday.year() == today.year() ? "dd.MM" : "dd.MM.yyyy";
            weekPicker->addItem(monday.toString(format) + " – "
                                    + monday.addDays(6).toString(format), monday);
        }
    }
    weekPicker->setEnabled(weekPicker->count() > 0);
    weekPicker->setCurrentIndex(weekPicker->findData(pastWeek));
}

void TrackerWindow::addTodoFromInput() {
    const QString text = todoInput->text().trimmed();
    if (text.isEmpty()) return;

    // A due date picked yesterday and left unused must not go to a new task.
    const QDate today = QDate::currentDate();
    addTodo(text, todoSubject->currentData().toInt(),
            newTodoWithoutDeadline ? QDate() : std::max(newTodoDeadline, today));
    todoInput->clear();
    newTodoDeadline = {};
    newTodoWithoutDeadline = false;
    updateDeadlineButton();
    refreshTodos();
}

void TrackerWindow::pickDeadline() {
    const QDate today = QDate::currentDate();
    const QList<QListWidgetItem*> checked = checkedTodos();
    if (checked.isEmpty()) {
        const std::optional<QDate> chosen =
            pickDate(std::max(newTodoDeadline, today), "Deadline of the new task");
        if (!chosen) return;
        newTodoWithoutDeadline = !chosen->isValid();
        newTodoDeadline = *chosen == today ? QDate() : *chosen;
        updateDeadlineButton();
        return;
    }

    // The calendar opens on the due date of the first selected task.
    const QDate current = checked.first()->data(TodoDeadlineRole).toDate();
    const QString title = checked.size() == 1 ? "Deadline: " + checked.first()->text()
                                              : QString("Deadline of %1 tasks").arg(checked.size());
    const std::optional<QDate> chosen = pickDate(std::max(current, today), title);
    if (!chosen) return;

    // The selection is cleared so the moved tasks are not solved by accident.
    {
        const QSignalBlocker blocker(todoList);
        for (QListWidgetItem* item : checked) {
            setTodoDeadline(item->data(TodoIdRole).toInt(), *chosen);
            item->setCheckState(Qt::Unchecked);
        }
    }
    refreshTodos();
}

QList<QListWidgetItem*> TrackerWindow::checkedTodos() const {
    QList<QListWidgetItem*> result;
    for (int row = 0; row < todoList->count(); ++row) {
        QListWidgetItem* item = todoList->item(row);
        if (item->checkState() == Qt::Checked && !item->data(TodoSolvingRole).toBool())
            result.append(item);
    }
    return result;
}

void TrackerWindow::updateDeadlineButton() {
    if (!checkedTodos().isEmpty()) {
        deadlineBtn->setText("Set deadline…");
        deadlineBtn->setToolTip("Change the deadline of the checked tasks");
        return;
    }
    deadlineBtn->setToolTip("Deadline of the new task");

    const QDate today = QDate::currentDate();
    if (newTodoWithoutDeadline)
        deadlineBtn->setText("No deadline");
    else if (newTodoDeadline <= today)
        deadlineBtn->setText("Due today");
    else
        deadlineBtn->setText(newTodoDeadline.toString(
            newTodoDeadline.year() == today.year() ? "'Due' dd.MM" : "'Due' dd.MM.yyyy"));
}

void TrackerWindow::refreshTodos() {
    // Tasks being solved are already checked off in the database, so they vanish with the list.
    solveAnimation->stop();

    QSet<int> checked;
    for (int row = 0; row < todoList->count(); ++row) {
        const QListWidgetItem* item = todoList->item(row);
        if (item->checkState() == Qt::Checked)
            checked.insert(item->data(TodoIdRole).toInt());
    }

    {
        const QSignalBlocker blocker(todoList);
        todoList->clear();

        for (const Todo& t : openTodos()) {
            auto* item = new QListWidgetItem(t.text, todoList);
            item->setData(TodoIdRole, t.id);
            item->setData(TodoColorRole, t.color);
            item->setData(TodoDeadlineRole, t.deadline);
            item->setIcon(swatchIcon(t.color));
            item->setFlags(mobile ? Qt::ItemIsEnabled : Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(checked.contains(t.id) ? Qt::Checked : Qt::Unchecked);
        }
    }
    updateSolveButton();
}

void TrackerWindow::solveCheckedTodos() {
    // The previous animation may still be running.
    solveAnimation->stop();
    removeSolvedTodos();

    {
        const QSignalBlocker blocker(todoList);
        for (int row = 0; row < todoList->count(); ++row) {
            QListWidgetItem* item = todoList->item(row);
            if (item->checkState() != Qt::Checked) continue;
            // Save right away so closing the program during the animation loses nothing.
            completeTodo(item->data(TodoIdRole).toInt());
            item->setData(TodoSolvingRole, true);
            item->setFlags(Qt::ItemIsEnabled);
        }
    }
    updateSolveButton();
    solveAnimation->start();
    // Checked-off tasks appear next to the bars immediately.
    if (!dashboard->isHidden())
        chart->reload();
}

void TrackerWindow::removeSolvedTodos() {
    for (int row = todoList->count() - 1; row >= 0; --row) {
        if (todoList->item(row)->data(TodoSolvingRole).toBool())
            delete todoList->takeItem(row);
    }
    todoDelegate->progress = 0;
}

void TrackerWindow::updateSolveButton() {
    solveBtn->setEnabled(!checkedTodos().isEmpty());
    updateDeadlineButton();
}

void TrackerWindow::showTodoMenu(const QPoint& pos) {
    const QListWidgetItem* item = todoList->itemAt(pos);
    if (!item || item->data(TodoSolvingRole).toBool()) return;
    const int todoId = item->data(TodoIdRole).toInt();
    const QDate deadline = item->data(TodoDeadlineRole).toDate();

    QMenu menu(this);
    const QAction* changeDeadline = menu.addAction("Change deadline…");
    const QAction* remove = menu.addAction("Delete task…");
    menu.addSeparator();
    // The same items as when adding a task.
    for (int i = 0; i < todoSubject->count(); ++i) {
        menu.addAction(todoSubject->itemIcon(i), todoSubject->itemText(i))
            ->setData(todoSubject->itemData(i));
    }

    const QAction* chosen = menu.exec(todoList->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == remove) {
        if (!askYesNo("Delete task", "Delete \"" + item->text() + "\"?")) return;
        deleteTodo(todoId);
    } else if (chosen == changeDeadline) {
        const std::optional<QDate> picked =
            pickDate(std::max(deadline, QDate::currentDate()), "Deadline: " + item->text());
        if (!picked) return;
        setTodoDeadline(todoId, *picked);
    } else {
        setTodoSubject(todoId, chosen->data().toInt());
    }
    refreshTodos();
}

void TrackerWindow::updateControls() {
    switch (counter.state()) {
        case Counter::State::Stopped: startPauseBtn->setText("Start"); break;
        case Counter::State::Running: startPauseBtn->setText("Pause"); break;
        case Counter::State::Paused:  startPauseBtn->setText("Resume"); break;
    }
    startPauseBtn->setEnabled(currentSubject != -1);
    stopBtn->setEnabled(counter.state() != Counter::State::Stopped);
    subjectColorBtn->setEnabled(currentSubject != -1);
    todayBtn->setChecked(currentSection == ChartWidget::Range::Today);
    weekBtn->setChecked(currentSection == ChartWidget::Range::Week && !pastWeek.isValid());
    weekPicker->setCurrentIndex(weekPicker->findData(pastWeek));
    expandBtn->setText(dashboard->isHidden() ? "▼" : "▲");
    expandBtn->setToolTip(dashboard->isHidden() ? "Expand dashboard" : "Collapse dashboard");
}

void TrackerWindow::updateTime() {
    timeLabel->setText(formatTime(counter.elapsed().count()));
}
