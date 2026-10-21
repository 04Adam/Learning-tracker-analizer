// AndroidBridge.h
#ifndef LEARNINGTIMEANALIZER_ANDROIDBRIDGE_H
#define LEARNINGTIMEANALIZER_ANDROIDBRIDGE_H

#include <QString>

#include <chrono>
#include <functional>

// Funkcje systemu Android; na komputerze nic nie robią.

// Powiadomienie ze stoperem, które trzyma aplikację przy życiu, gdy jest schowana.
void showTimerNotification(const QString& subject, std::chrono::seconds elapsed, bool running);
void hideTimerNotification();
// Przyciski powiadomienia zgłaszają "toggle" (pauza albo wznowienie) i "stop".
void setNotificationActionHandler(std::function<void(const QString&)> handler);
void requestNotificationPermission();
// Chowa aplikację tak jak przycisk ekranu głównego, bez jej zamykania.
void moveAppToBackground();

#endif
