// AndroidBridge.h
#ifndef LEARNINGTIMEANALIZER_ANDROIDBRIDGE_H
#define LEARNINGTIMEANALIZER_ANDROIDBRIDGE_H

#include <QString>

#include <chrono>
#include <functional>

// Android system functions; no-ops on desktop.

// Stopwatch notification that keeps the app alive while it is in the background.
void showTimerNotification(const QString& subject, std::chrono::seconds elapsed, bool running);
void hideTimerNotification();
// Notification buttons report "toggle" (pause or resume) and "stop".
void setNotificationActionHandler(std::function<void(const QString&)> handler);
void requestNotificationPermission();
// Hides the app like the home button does, without closing it.
void moveAppToBackground();

#endif
