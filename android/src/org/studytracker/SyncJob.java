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

// Background sync while the app is closed. Android runs it:
// - right after the phone connects to a Wi-Fi network (SyncReceiver gets a broadcast then),
// - every 15 minutes, which catches changes on the computer and the phone's own hotspot.
public class SyncJob extends JobService {
    private static final int PERIODIC_JOB = 1;
    private static final int SOON_JOB = 2;
    private static final long PERIOD_MS = 15 * 60 * 1000;

    // Called on every app start and after the phone reboots.
    static void schedule(Context context) {
        final JobScheduler jobs = context.getSystemService(JobScheduler.class);
        if (jobs.getPendingJob(PERIODIC_JOB) == null) {
            jobs.schedule(new JobInfo.Builder(PERIODIC_JOB, new ComponentName(context, SyncJob.class))
                    .setPeriodic(PERIOD_MS)
                    .setPersisted(true)
                    .build());
        }

        // This request survives the app closing, but not a phone reboot
        // or an app update; SyncReceiver renews it then.
        final ConnectivityManager connectivity = context.getSystemService(ConnectivityManager.class);
        final PendingIntent intent = SyncReceiver.networkIntent(context);
        try {
            connectivity.unregisterNetworkCallback(intent);
        } catch (IllegalArgumentException ignored) {
            // Not registered yet.
        }
        connectivity.registerNetworkCallback(new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build(), intent);
    }

    // One-off sync as soon as possible; the network gets a moment to settle.
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
        // The next chance will come anyway; no need to retry.
        return false;
    }
}
