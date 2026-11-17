package com.example.droidlet.servicestartedapp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

public class MainActivity extends Activity {
    private static final String TAG = "ServiceStartedApp";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i(TAG, "onCreate");
        final Intent intent = new Intent(this, StartedService.class);
        startService(intent);
        Log.i(TAG, "startService returned");
        startService(new Intent(this, SelfStopService.class));
        Log.i(TAG, "self-stop service started");
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                Log.i(TAG, "activity finished; service remains");
                finish();
            }
        }, 500);
        // Leave enough time for a fresh child process to start ART, load the
        // app and reach onStartCommand.  This takes several seconds with an
        // unoptimized (debug) build, so don't race the host startup.
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                Log.i(TAG, "stopping service");
                stopService(intent);
            }
        }, 10000);
    }
}
