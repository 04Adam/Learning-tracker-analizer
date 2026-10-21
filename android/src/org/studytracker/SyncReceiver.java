package org.studytracker;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

// Budzi synchronizację po połączeniu z Wi-Fi i odnawia jej harmonogram po ponownym
// uruchomieniu telefonu albo aktualizacji aplikacji.
public class SyncReceiver extends BroadcastReceiver {
    private static final String ACTION_NETWORK = "org.studytracker.NETWORK_AVAILABLE";

    static PendingIntent networkIntent(Context context) {
        final Intent intent = new Intent(context, SyncReceiver.class).setAction(ACTION_NETWORK);
        // Android dopisuje do tej wiadomości dane sieci, więc musi dać się zmienić.
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
        return PendingIntent.getBroadcast(context, 0, intent, flags);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (ACTION_NETWORK.equals(intent.getAction())) {
            SyncJob.runSoon(context);
        } else {
            SyncJob.schedule(context);
            SyncJob.runSoon(context);
        }
    }
}
