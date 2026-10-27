// AndroidBridge.cpp
#include "AndroidBridge.h"

#ifdef Q_OS_ANDROID

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

static const char* const kService = "org/studytracker/TimerService";

static QJniObject androidContext() {
    return QNativeInterface::QAndroidApplication::context();
}

void showTimerNotification(const QString& subject, std::chrono::seconds elapsed, bool running) {
    QJniObject::callStaticMethod<void>(
        kService, "show", "(Landroid/content/Context;Ljava/lang/String;JZ)V",
        androidContext().object(), QJniObject::fromString(subject).object(),
        jlong(std::chrono::milliseconds(elapsed).count()), jboolean(running));
}

void hideTimerNotification() {
    QJniObject::callStaticMethod<void>(kService, "hide", "(Landroid/content/Context;)V",
                                       androidContext().object());
}

static std::function<void(const QString&)> actionHandler;

static void onNotificationAction(JNIEnv*, jclass, jstring action) {
    // Android calls this from its own thread, while the stopwatch lives in the Qt thread.
    const QString name = QJniObject(action).toString();
    QMetaObject::invokeMethod(qApp, [name] {
        if (actionHandler)
            actionHandler(name);
    }, Qt::QueuedConnection);
}

void setNotificationActionHandler(std::function<void(const QString&)> handler) {
    actionHandler = std::move(handler);
    const JNINativeMethod methods[] = {
        {"onAction", "(Ljava/lang/String;)V", reinterpret_cast<void*>(onNotificationAction)},
    };
    QJniEnvironment().registerNativeMethods(kService, methods, 1);
}

void requestNotificationPermission() {
    QJniObject::callStaticMethod<void>(kService, "requestPermission", "(Landroid/app/Activity;)V",
                                       androidContext().object());
}

void moveAppToBackground() {
    androidContext().callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true));
}

#else

void showTimerNotification(const QString&, std::chrono::seconds, bool) {}
void hideTimerNotification() {}
void setNotificationActionHandler(std::function<void(const QString&)>) {}
void requestNotificationPermission() {}
void moveAppToBackground() {}

#endif
