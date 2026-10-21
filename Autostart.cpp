// Autostart.cpp
#include "Autostart.h"
#include "Sync.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

static QString autostartFile() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + "/autostart/studytracker.desktop";
}

bool autostartEnabled() {
#ifdef Q_OS_ANDROID
    return false;
#else
    return QFile::exists(autostartFile());
#endif
}

void setAutostart(bool enabled) {
#ifndef Q_OS_ANDROID
    if (!enabled) {
        QFile::remove(autostartFile());
        return;
    }
    QDir().mkpath(QFileInfo(autostartFile()).path());
    QFile file(autostartFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QString("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=StudyTracker\n"
                       "Comment=Learning time tracker\n"
                       "Exec=\"%1\" --minimized\n"
                       "Icon=studytracker\n"
                       "Terminal=false\n"
                       "X-GNOME-Autostart-enabled=true\n")
                   .arg(QCoreApplication::applicationFilePath())
                   .toUtf8());
#else
    Q_UNUSED(enabled);
#endif
}

void enableAutostartOnFirstRun() {
#if !defined(Q_OS_ANDROID) && defined(STUDYTRACKER_INSTALLED_BINARY)
    // Programy uruchamiane z katalogów budowania nie trafiają do autostartu.
    if (QCoreApplication::applicationFilePath() != STUDYTRACKER_INSTALLED_BINARY) return;
    if (syncSetting("autostart_offered").toBool()) return;
    setSyncSetting("autostart_offered", true);
    setAutostart(true);
#endif
}
