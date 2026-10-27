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
    // Android picks light status bar icons to match this.
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
    // The name determines the database directory (~/.local/share/StudyTracker).
    QApplication::setApplicationName("StudyTracker");
    // The desktop uses this name to match the window with the studytracker.desktop shortcut.
    QApplication::setDesktopFileName("studytracker");
    QApplication::setWindowIcon(QIcon(":/icons/studytracker.png"));
    applyDarkTheme();

    // Background sync may be working on the database right now; wait for it to finish.
    prepareSync();
    if (!openDatabase())
        return 1;

    // On desktop --mobile shows the phone layout in a regular window.
#ifdef Q_OS_ANDROID
    const bool mobile = true;
#else
    const bool mobile = app.arguments().contains("--mobile");
#endif

    // Android gives buttons and lists a smaller font than the rest; make them match.
    if (mobile)
        QApplication::setFont(QApplication::font("QPushButton"));

    TrackerWindow window(mobile);
    if (mobile) {
        // Android stretches the window to full screen anyway.
        window.resize(400, 800);
    } else {
        window.adjustSize();
        QRect screen = app.primaryScreen()->availableGeometry();
        window.move(screen.right() - window.width() - 10, screen.top() + 10);
    }
    // Autostart launches the program hidden, just to wait for the phone.
    if (!mobile && app.arguments().contains("--minimized")) {
        window.showMinimized();
    } else {
        window.show();
    }
    if (!mobile)
        enableAutostartOnFirstRun();

    return app.exec();
}
