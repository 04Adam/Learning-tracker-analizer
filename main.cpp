// main.cpp
#include "Database.h"
#include "TrackerWindow.h"

#include <QApplication>
#include <QScreen>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("StudyTracker");

    if (!openDatabase())
        return 1;

    TrackerWindow window;
    window.adjustSize();
    QRect screen = app.primaryScreen()->availableGeometry();
    window.move(screen.right() - window.width() - 10, screen.top() + 10);
    window.show();

    return app.exec();
}
