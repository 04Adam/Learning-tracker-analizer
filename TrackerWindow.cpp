// TrackerWindow.cpp
#include "TrackerWindow.h"
#include "Database.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

static QString formatTime(qint64 seconds) {
    return QString("%1:%2:%3")
        .arg(seconds / 3600)
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}

TrackerWindow::TrackerWindow() {
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);

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
    quitBtn->setToolTip("Zakończ");

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
    subjectList->setMinimumSize(360, 220);

    auto* dashboardLayout = new QVBoxLayout(dashboard);
    dashboardLayout->setContentsMargins(0, 0, 0, 0);
    dashboardLayout->addWidget(new QLabel("Czego się teraz uczysz?", dashboard));
    dashboardLayout->addWidget(subjectList);
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
    connect(subjectList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item)
            selectSubject(item->data(Qt::UserRole).toInt());
    });
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
    if (dashboard->isHidden())
        refreshSubjects();
    dashboard->setVisible(dashboard->isHidden());
    updateControls();

    // Trzymamy prawy górny róg w miejscu, żeby okno nie wyjechało poza ekran.
    adjustSize();
    move(topRight.x() - width() + 1, topRight.y());
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
        item->setText(QString("%1 — dziś %2").arg(s.name, formatTime(s.todaySeconds)));
        item->setData(Qt::UserRole, s.id);
        if (s.id == currentSubject) {
            subjectList->setCurrentItem(item);
            subjectLabel->setText(s.name);
        }
    }
}

void TrackerWindow::updateControls() {
    switch (counter.state()) {
        case Counter::State::Stopped: startPauseBtn->setText("Start"); break;
        case Counter::State::Running: startPauseBtn->setText("Pauza"); break;
        case Counter::State::Paused:  startPauseBtn->setText("Wznów"); break;
    }
    startPauseBtn->setEnabled(currentSubject != -1);
    stopBtn->setEnabled(counter.state() != Counter::State::Stopped);
    expandBtn->setText(dashboard->isHidden() ? "▼" : "▲");
    expandBtn->setToolTip(dashboard->isHidden() ? "Rozwiń pulpit" : "Zwiń pulpit");
}

void TrackerWindow::updateTime() {
    timeLabel->setText(formatTime(counter.elapsed().count()));
}
