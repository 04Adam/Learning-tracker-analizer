package org.studytracker;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.text.format.DateUtils;

// Usługa pierwszoplanowa: jej powiadomienie pokazuje stoper, a sama jej obecność
// sprawia, że Android nie ubija aplikacji schowanej w tle.
public class TimerService extends Service {
    private static final String CHANNEL = "stopwatch";
    private static final int NOTIFICATION_ID = 1;
    // Akcje przycisków powiadomienia; te same nazwy zna strona C++.
    private static final String ACTION_TOGGLE = "toggle";
    private static final String ACTION_STOP = "stop";

    private static volatile TimerService instance;

    // Przekazuje naciśnięcie przycisku powiadomienia do stopera w C++.
    private static native void onAction(String action);

    public static void show(Context context, String subject, long elapsedMs, boolean running) {
        // Działającą usługę wystarczy poprosić o podmianę powiadomienia; ponowny start
        // usługi pierwszoplanowej bywa zabroniony, gdy aplikacja jest schowana.
        final TimerService service = instance;
        if (service != null) {
            service.getSystemService(NotificationManager.class)
                    .notify(NOTIFICATION_ID, service.buildNotification(subject, elapsedMs, running));
            return;
        }

        Intent intent = new Intent(context, TimerService.class)
                .putExtra("subject", subject)
                .putExtra("elapsed", elapsedMs)
                .putExtra("running", running);
        context.startForegroundService(intent);
    }

    public static void hide(Context context) {
        context.stopService(new Intent(context, TimerService.class));
    }

    // Od Androida 13 powiadomienia wymagają zgody użytkownika.
    public static void requestPermission(Activity activity) {
        final String permission = "android.permission.POST_NOTIFICATIONS";
        if (Build.VERSION.SDK_INT >= 33
                && activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
            activity.requestPermissions(new String[] {permission}, 0);
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        getSystemService(NotificationManager.class).createNotificationChannel(
                new NotificationChannel(CHANNEL, "Stopwatch", NotificationManager.IMPORTANCE_LOW));
        instance = this;
    }

    @Override
    public void onDestroy() {
        instance = null;
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (intent.getAction() != null) {
            onAction(intent.getAction());
            return START_NOT_STICKY;
        }

        final Notification notification = buildNotification(intent.getStringExtra("subject"),
                intent.getLongExtra("elapsed", 0), intent.getBooleanExtra("running", false));
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
        return START_NOT_STICKY;
    }

    private Notification.Action action(String name, String label) {
        final Intent intent = new Intent(this, TimerService.class).setAction(name);
        return new Notification.Action.Builder(null, label,
                PendingIntent.getService(this, 0, intent, PendingIntent.FLAG_IMMUTABLE)).build();
    }

    private Notification buildNotification(String subject, long elapsed, boolean running) {
        final Intent open = getPackageManager().getLaunchIntentForPackage(getPackageName());

        Notification.Builder builder = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
                .setContentTitle(subject)
                .setContentIntent(PendingIntent.getActivity(this, 0, open,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT))
                .setCategory(Notification.CATEGORY_STOPWATCH)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .addAction(action(ACTION_TOGGLE, running ? "Pause" : "Resume"))
                .addAction(action(ACTION_STOP, "Stop"));
        if (running) {
            // Czas odlicza samo powiadomienie, więc nie trzeba go co sekundę odświeżać.
            builder.setUsesChronometer(true)
                    .setShowWhen(true)
                    .setWhen(System.currentTimeMillis() - elapsed)
                    .setContentText("Studying");
        } else {
            builder.setShowWhen(false)
                    .setContentText("Paused · " + DateUtils.formatElapsedTime(elapsed / 1000));
        }
        // Bez tego Android 12+ pokazuje powiadomienie dopiero po kilku sekundach.
        if (Build.VERSION.SDK_INT >= 31)
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        return builder.build();
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        // Aplikację wyrzucono z listy ostatnich: stoper już nie działa.
        stopSelf();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
