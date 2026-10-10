// TrackerWindow.cpp
#include "TrackerWindow.h"
#include "Database.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>

static QString formatTime(qint64 seconds) {
    return QString("%1:%2:%3")
        .arg(seconds / 3600)
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}

// Nieprawidłowy kolor daje pusty kwadrat, żeby teksty w liście były wyrównane.
static QIcon swatchIcon(const QColor& color) {
    QPixmap swatch(12, 12);
    swatch.fill(color.isValid() ? color : Qt::transparent);
    // Bez wariantu Selected Qt przyciemnia kolor zaznaczonego wiersza.
    QIcon icon(swatch);
    icon.addPixmap(swatch, QIcon::Selected);
    return icon;
}

enum TodoRole {
    TodoIdRole = Qt::UserRole,
    TodoColorRole,
    // Zadanie już rozwiązane, czeka tylko na koniec animacji znikania.
    TodoSolvingRole,
};

// Rysuje na rozwiązywanych zadaniach pasek w kolorze przedmiotu, rosnący od lewej.
class TodoDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    double progress = 0;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);
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
};

TrackerWindow::TrackerWindow() {
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setObjectName("tracker");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(
        "#tracker { border: 1px solid #43454a; }"
        "QPushButton[section=true] { padding: 5px 22px; border: 1px solid #43454a;"
        " border-radius: 4px; background: #2b2d30; }"
        "QPushButton[section=true]:hover { background: #35373b; }"
        "QPushButton[section=true]:checked { background: #2f65ca; border-color: #2f65ca;"
        " color: white; }"
        "#todoList::indicator { width: 12px; height: 12px; border: 1px solid white;"
        " border-radius: 2px; }"
        "#todoList::indicator:checked { background: white; }");

    subjectLabel  = new QLabel(this);
    timeLabel     = new QLabel(this);
    startPauseBtn = new QPushButton(this);
    stopBtn       = new QPushButton("Stop", this);
    expandBtn     = new QPushButton(this);
    auto* quitBtn = new QPushButton("✕", this);

    QFont timeFont = timeLabel->font();
    timeFont.setBold(true);
    timeFont.setPointSize(timeFont.pointSize() + 2);
    timeLabel->setFont(timeFont);

    expandBtn->setFixedWidth(30);
    quitBtn->setFixedWidth(30);
    quitBtn->setToolTip("Quit");

    auto* bar = new QHBoxLayout;
    bar->addWidget(subjectLabel);
    bar->addWidget(timeLabel);
    bar->addStretch();
    bar->addWidget(startPauseBtn);
    bar->addWidget(stopBtn);
    bar->addWidget(expandBtn);
    bar->addWidget(quitBtn);

    dashboard = new QWidget(this);
    subjectList = new QListWidget(dashboard);
    subjectList->setFixedWidth(260);
    subjectList->setSpacing(2);

    todoInput = new QLineEdit(dashboard);
    todoInput->setPlaceholderText("New task…");
    todoSubject = new QComboBox(dashboard);
    todoSubject->addItem(swatchIcon({}), "No subject", -1);
    for (const SubjectTotal& s : subjectsWithTodayTotals())
        todoSubject->addItem(swatchIcon(s.color), s.name, s.id);
    auto* addTodoBtn = new QPushButton("Add", dashboard);
    todoList = new QListWidget(dashboard);
    todoList->setObjectName("todoList");
    todoList->setContextMenuPolicy(Qt::CustomContextMenu);
    todoList->setFixedWidth(260);
    todoList->setSpacing(2);
    todoList->setWordWrap(true);
    // Zadań się nie zaznacza, tylko odhacza.
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

    todayBtn = new QPushButton("Today", dashboard);
    weekBtn  = new QPushButton("Week", dashboard);
    todayBtn->setProperty("section", true);
    weekBtn->setProperty("section", true);
    todayBtn->setCheckable(true);
    weekBtn->setCheckable(true);
    weekPicker = new QComboBox(dashboard);
    weekPicker->setPlaceholderText("Previous weeks");
    weekPicker->setMinimumContentsLength(15);
    weekPicker->setMaxVisibleItems(12);
    auto* hint = new QLabel("Click again: bars ↔ over time", dashboard);
    hint->setEnabled(false);
    chart = new ChartWidget(dashboard);

    auto* subjectColumn = new QVBoxLayout;
    subjectColumn->addWidget(new QLabel("What are you studying now?", dashboard));
    subjectColumn->addWidget(subjectList, 1);

    auto* todoEntry = new QHBoxLayout;
    todoEntry->addWidget(todoSubject, 1);
    todoEntry->addWidget(addTodoBtn);

    subjectColumn->addSpacing(8);
    subjectColumn->addWidget(new QLabel("To do", dashboard));
    subjectColumn->addWidget(todoInput);
    subjectColumn->addLayout(todoEntry);
    subjectColumn->addWidget(todoList, 2);
    subjectColumn->addWidget(solveBtn);

    auto* sections = new QHBoxLayout;
    sections->addWidget(todayBtn);
    sections->addWidget(weekBtn);
    sections->addWidget(weekPicker);
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

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(dashboard);
    // Okno samo dopasowuje rozmiar do tego, czy pulpit jest rozwinięty.
    layout->setSizeConstraint(QLayout::SetFixedSize);

    connect(startPauseBtn, &QPushButton::clicked, this, [this] { toggleRunning(); });
    connect(stopBtn, &QPushButton::clicked, this, [this] { stop(); });
    connect(expandBtn, &QPushButton::clicked, this, [this] { toggleDashboard(); });
    connect(quitBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(todayBtn, &QPushButton::clicked, this, [this] { showSection(ChartWidget::Range::Today); });
    connect(weekBtn, &QPushButton::clicked, this, [this] { showSection(ChartWidget::Range::Week); });
    // Sygnał przychodzi też przy ponownym wybraniu tego samego tygodnia.
    connect(weekPicker, &QComboBox::activated, this,
            [this](int index) { showPastWeek(weekPicker->itemData(index).toDate()); });
    connect(subjectList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item)
            selectSubject(item->data(Qt::UserRole).toInt());
    });
    connect(todoInput, &QLineEdit::returnPressed, this, [this] { addTodoFromInput(); });
    connect(addTodoBtn, &QPushButton::clicked, this, [this] { addTodoFromInput(); });
    connect(todoList, &QListWidget::itemChanged, this, [this] { updateSolveButton(); });
    connect(solveBtn, &QPushButton::clicked, this, [this] { solveCheckedTodos(); });
    connect(solveAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        todoDelegate->progress = value.toDouble();
        todoList->viewport()->update();
    });
    connect(solveAnimation, &QVariantAnimation::finished, this, [this] { removeSolvedTodos(); });
    connect(todoList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { showTodoMenu(pos); });
    connect(qApp, &QApplication::aboutToQuit, this, [this] { counter.stop(); });

    auto* tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, [this] { updateTime(); });
    tick->start(1000);

    auto* autosave = new QTimer(this);
    connect(autosave, &QTimer::timeout, this, [this] {
        counter.checkpoint();
        refreshSubjects();
    });
    autosave->start(10000);

    refreshSubjects();
    refreshTodos();
    if (subjectList->count() > 0)
        subjectList->setCurrentRow(0);
    updateControls();
    updateTime();
}

void TrackerWindow::mousePressEvent(QMouseEvent* event) {
    // Okno nie ma ramki, więc przesuwamy je łapiąc za tło.
    if (event->button() == Qt::LeftButton && windowHandle())
        windowHandle()->startSystemMove();
}

void TrackerWindow::toggleRunning() {
    switch (counter.state()) {
        case Counter::State::Stopped: counter.start(currentSubject); break;
        case Counter::State::Running: counter.pause(); break;
        case Counter::State::Paused:  counter.resume(); break;
    }
    refreshSubjects();
    updateControls();
    updateTime();
}

void TrackerWindow::stop() {
    counter.stop();
    refreshSubjects();
    updateControls();
    updateTime();
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

    // Trzymamy prawy górny róg w miejscu, ale nie pozwalamy wyjechać poza ekran.
    adjustSize();
    move(qMax(available.left(), topRight.x() - width() + 1),
         qMin(topRight.y(), qMax(available.top(), available.bottom() - height())));
}

void TrackerWindow::showSection(ChartWidget::Range section) {
    ChartWidget::Mode& mode = section == ChartWidget::Range::Today ? todayMode : weekMode;
    // Z wcześniejszego tygodnia przycisk sekcji najpierw wraca do bieżącego okresu.
    if (section == currentSection && !pastWeek.isValid()) {
        mode = mode == ChartWidget::Mode::Totals ? ChartWidget::Mode::Cumulative
                                                 : ChartWidget::Mode::Totals;
    }
    currentSection = section;
    pastWeek = {};
    chart->setView(section, mode);
    updateControls();
}

void TrackerWindow::showPastWeek(const QDate& monday) {
    // Wcześniejsze tygodnie dzielą tryb z sekcją tygodnia; ponowny wybór go przełącza.
    if (monday == pastWeek) {
        weekMode = weekMode == ChartWidget::Mode::Totals ? ChartWidget::Mode::Cumulative
                                                         : ChartWidget::Mode::Totals;
    }
    currentSection = ChartWidget::Range::Week;
    pastWeek = monday;
    chart->setView(ChartWidget::Range::Week, weekMode, monday);
    updateControls();
}

void TrackerWindow::selectSubject(int subjectId) {
    if (subjectId == currentSubject) return;

    // Zmiana przedmiotu w trakcie nauki zamyka bieżący pomiar i zaczyna nowy.
    const bool wasRunning = counter.state() == Counter::State::Running;
    counter.stop();
    currentSubject = subjectId;
    if (wasRunning)
        counter.start(currentSubject);

    refreshSubjects();
    updateControls();
    updateTime();
}

void TrackerWindow::refreshSubjects() {
    const QSignalBlocker blocker(subjectList);
    const QList<SubjectTotal> subjects = subjectsWithTodayTotals();

    // Aktualizujemy wiersze w miejscu: lista bywa odświeżana z jej własnego sygnału.
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

void TrackerWindow::refreshWeeks() {
    // Lista zmienia się tylko wtedy, gdy kończy się tydzień: trafia na nią ten miniony.
    const QDate today = QDate::currentDate();
    const QDate thisMonday = today.addDays(1 - today.dayOfWeek());
    if (thisMonday == listedBefore) return;
    listedBefore = thisMonday;

    const QSignalBlocker blocker(weekPicker);
    weekPicker->clear();

    // Od zeszłego tygodnia wstecz do tygodnia z najstarszym odcinkiem nauki.
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

    addTodo(text, todoSubject->currentData().toInt());
    todoInput->clear();
    refreshTodos();
}

void TrackerWindow::refreshTodos() {
    // Rozwiązywane zadania są już odhaczone w bazie, więc znikną razem z listą.
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
            item->setIcon(swatchIcon(t.color));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(checked.contains(t.id) ? Qt::Checked : Qt::Unchecked);
        }
    }
    updateSolveButton();
}

void TrackerWindow::solveCheckedTodos() {
    // Poprzednia animacja mogła jeszcze trwać.
    solveAnimation->stop();
    removeSolvedTodos();

    {
        const QSignalBlocker blocker(todoList);
        for (int row = 0; row < todoList->count(); ++row) {
            QListWidgetItem* item = todoList->item(row);
            if (item->checkState() != Qt::Checked) continue;
            // Zapis od razu, żeby zamknięcie programu w trakcie animacji nic nie zgubiło.
            completeTodo(item->data(TodoIdRole).toInt());
            item->setData(TodoSolvingRole, true);
            item->setFlags(Qt::ItemIsEnabled);
        }
    }
    updateSolveButton();
    solveAnimation->start();
}

void TrackerWindow::removeSolvedTodos() {
    for (int row = todoList->count() - 1; row >= 0; --row) {
        if (todoList->item(row)->data(TodoSolvingRole).toBool())
            delete todoList->takeItem(row);
    }
    todoDelegate->progress = 0;
}

void TrackerWindow::updateSolveButton() {
    bool anyChecked = false;
    for (int row = 0; row < todoList->count(); ++row) {
        const QListWidgetItem* item = todoList->item(row);
        if (item->checkState() == Qt::Checked && !item->data(TodoSolvingRole).toBool())
            anyChecked = true;
    }
    solveBtn->setEnabled(anyChecked);
}

void TrackerWindow::showTodoMenu(const QPoint& pos) {
    const QListWidgetItem* item = todoList->itemAt(pos);
    if (!item || item->data(TodoSolvingRole).toBool()) return;
    const int todoId = item->data(TodoIdRole).toInt();

    // Te same pozycje co przy dodawaniu zadania.
    QMenu menu(this);
    for (int i = 0; i < todoSubject->count(); ++i) {
        menu.addAction(todoSubject->itemIcon(i), todoSubject->itemText(i))
            ->setData(todoSubject->itemData(i));
    }

    if (const QAction* chosen = menu.exec(todoList->viewport()->mapToGlobal(pos))) {
        setTodoSubject(todoId, chosen->data().toInt());
        refreshTodos();
    }
}

void TrackerWindow::updateControls() {
    switch (counter.state()) {
        case Counter::State::Stopped: startPauseBtn->setText("Start"); break;
        case Counter::State::Running: startPauseBtn->setText("Pause"); break;
        case Counter::State::Paused:  startPauseBtn->setText("Resume"); break;
    }
    startPauseBtn->setEnabled(currentSubject != -1);
    stopBtn->setEnabled(counter.state() != Counter::State::Stopped);
    todayBtn->setChecked(currentSection == ChartWidget::Range::Today);
    weekBtn->setChecked(currentSection == ChartWidget::Range::Week && !pastWeek.isValid());
    weekPicker->setCurrentIndex(weekPicker->findData(pastWeek));
    expandBtn->setText(dashboard->isHidden() ? "▼" : "▲");
    expandBtn->setToolTip(dashboard->isHidden() ? "Expand dashboard" : "Collapse dashboard");
}

void TrackerWindow::updateTime() {
    timeLabel->setText(formatTime(counter.elapsed().count()));
}
