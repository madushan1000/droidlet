package com.example.droidlet.servicestartedapp;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.util.Log;

public class StartedService extends Service {
    private static final String TAG = "ServiceStartedApp";

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "started service onCreate pid=" + android.os.Process.myPid());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.i(TAG, "started service onStartCommand id=" + startId);
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "started service onDestroy");
        super.onDestroy();
    }
}
