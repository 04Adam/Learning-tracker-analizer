// main.cpp
#include "Database.h"
#include "TrackerWindow.h"

#include <QApplication>
#include <QPalette>
#include <QScreen>
#include <QToolTip>

static void applyDarkTheme() {
    QApplication::setStyle("Fusion");

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
    applyDarkTheme();

    if (!openDatabase())
        return 1;

    TrackerWindow window;
    window.adjustSize();
    QRect screen = app.primaryScreen()->availableGeometry();
    window.move(screen.right() - window.width() - 10, screen.top() + 10);
    window.show();

    return app.exec();
}
