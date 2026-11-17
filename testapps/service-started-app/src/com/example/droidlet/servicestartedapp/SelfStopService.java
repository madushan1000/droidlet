package com.example.droidlet.servicestartedapp;

import android.app.Service;
import android.content.Intent;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

public class SelfStopService extends Service {
    private static final String TAG = "ServiceStartedApp";

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "self service onCreate pid=" + android.os.Process.myPid());
    }

    @Override
    public int onStartCommand(final Intent intent, int flags, final int startId) {
        Log.i(TAG, "self service onStartCommand id=" + startId);
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                Log.i(TAG, "self service stopSelf id=" + startId);
                stopSelf(startId);
            }
        }, 1000);
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "self service onDestroy");
        super.onDestroy();
    }
}
