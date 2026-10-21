// main.cpp
#include "Autostart.h"
#include "Database.h"
#include "Sync.h"
#include "TrackerWindow.h"

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QScreen>
#include <QStyleHints>
#include <QToolTip>

static void applyDarkTheme() {
    QApplication::setStyle("Fusion");
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Android dobiera do tego jasne ikony na pasku stanu.
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif

    const QColor window(0x1e, 0x1f, 0x22);
    const QColor surface(0x2b, 0x2d, 0x30);
    const QColor text(0xdf, 0xe1, 0xe5);
    const QColor disabled(0x6f, 0x73, 0x7a);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, surface);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, surface);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, surface);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Highlight, QColor(0x2f, 0x65, 0xca));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, disabled);

    QApplication::setPalette(palette);
    QToolTip::setPalette(palette);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // Nazwa wyznacza katalog z bazą danych (~/.local/share/StudyTracker).
    QApplication::setApplicationName("StudyTracker");
    // Po tej nazwie pulpit łączy okno ze skrótem studytracker.desktop.
    QApplication::setDesktopFileName("studytracker");
    QApplication::setWindowIcon(QIcon(":/icons/studytracker.png"));
    applyDarkTheme();

    // Synchronizacja w tle mogła właśnie pracować na bazie; czekamy, aż skończy.
    prepareSync();
    if (!openDatabase())
        return 1;

    // Na komputerze --mobile pokazuje układ z telefonu w zwykłym oknie.
#ifdef Q_OS_ANDROID
    const bool mobile = true;
#else
    const bool mobile = app.arguments().contains("--mobile");
#endif

    // Android daje przyciskom i listom mniejszą czcionkę niż reszcie; wyrównujemy do niej.
    if (mobile)
        QApplication::setFont(QApplication::font("QPushButton"));

    TrackerWindow window(mobile);
    if (mobile) {
        // Android i tak rozciąga okno na cały ekran.
        window.resize(400, 800);
    } else {
        window.adjustSize();
        QRect screen = app.primaryScreen()->availableGeometry();
        window.move(screen.right() - window.width() - 10, screen.top() + 10);
    }
    // Autostart uruchamia program schowany, żeby tylko czekał na telefon.
    if (!mobile && app.arguments().contains("--minimized")) {
        window.showMinimized();
    } else {
        window.show();
    }
    if (!mobile)
        enableAutostartOnFirstRun();

    return app.exec();
}
