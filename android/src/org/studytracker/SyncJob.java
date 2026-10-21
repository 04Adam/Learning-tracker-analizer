package org.studytracker;

import android.app.PendingIntent;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;

// Synchronizacja w tle, gdy aplikacja jest zamknięta. Android uruchamia ją:
// - zaraz po połączeniu telefonu z siecią Wi-Fi (SyncReceiver dostaje wtedy wiadomość),
// - co 15 minut, co wyłapuje zmiany na komputerze i hotspot udostępniany przez telefon.
public class SyncJob extends JobService {
    private static final int PERIODIC_JOB = 1;
    private static final int SOON_JOB = 2;
    private static final long PERIOD_MS = 15 * 60 * 1000;

    // Wołane przy każdym starcie aplikacji i po ponownym uruchomieniu telefonu.
    static void schedule(Context context) {
        final JobScheduler jobs = context.getSystemService(JobScheduler.class);
        if (jobs.getPendingJob(PERIODIC_JOB) == null) {
            jobs.schedule(new JobInfo.Builder(PERIODIC_JOB, new ComponentName(context, SyncJob.class))
                    .setPeriodic(PERIOD_MS)
                    .setPersisted(true)
                    .build());
        }

        // Ta prośba przetrwa zamknięcie aplikacji, ale nie ponowne uruchomienie telefonu
        // ani aktualizację aplikacji; wtedy odnawia ją SyncReceiver.
        final ConnectivityManager connectivity = context.getSystemService(ConnectivityManager.class);
        final PendingIntent intent = SyncReceiver.networkIntent(context);
        try {
            connectivity.unregisterNetworkCallback(intent);
        } catch (IllegalArgumentException ignored) {
            // Nie była jeszcze zarejestrowana.
        }
        connectivity.registerNetworkCallback(new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build(), intent);
    }

    // Jednorazowa synchronizacja możliwie szybko; sieć ma chwilę, żeby się ustabilizować.
    static void runSoon(Context context) {
        context.getSystemService(JobScheduler.class).schedule(
                new JobInfo.Builder(SOON_JOB, new ComponentName(context, SyncJob.class))
                        .setMinimumLatency(2000)
                        .build());
    }

    @Override
    public boolean onStartJob(JobParameters params) {
        final Context context = getApplicationContext();
        new Thread(() -> {
            SyncClient.runSync(context);
            jobFinished(params, false);
        }, "StudyTracker background sync").start();
        return true;
    }

    @Override
    public boolean onStopJob(JobParameters params) {
        // Następna okazja i tak przyjdzie; nie trzeba ponawiać.
        return false;
    }
}
