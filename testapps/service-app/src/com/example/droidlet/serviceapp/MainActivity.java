/*
 * droidlet service fixture: starts and binds a service in the app's process,
 * calls a method through the binder, then unbinds and stops it so the host
 * can exit.
 */

package com.example.droidlet.serviceapp;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

public class MainActivity extends Activity {
    private static final String TAG = "ServiceApp";

    private boolean mBound;

    private final ServiceConnection mConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            Log.i(TAG, "onServiceConnected " + name.flattenToShortString());
            LocalService.LocalBinder local = (LocalService.LocalBinder) binder;
            Log.i(TAG, "add=" + local.add(2, 3));
            mBound = true;
            new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
                @Override
                public void run() {
                    finishBinding();
                }
            }, 500);
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.i(TAG, "onServiceDisconnected " + name.flattenToShortString());
            mBound = false;
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i(TAG, "onCreate");
        Intent intent = new Intent(this, LocalService.class);
        startService(intent);
        if (!bindService(intent, mConnection, BIND_AUTO_CREATE)) {
            Log.e(TAG, "bindService returned false");
            stopService(intent);
            finish();
            return;
        }
        // Safety net: if the binder never arrives, do not leave the host
        // alive forever.
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!mBound) {
                    Log.e(TAG, "binder did not arrive; stopping");
                    finishBinding();
                }
            }
        }, 5000);
    }

    private void finishBinding() {
        if (mBound) {
            unbindService(mConnection);
            mBound = false;
        }
        stopService(new Intent(this, LocalService.class));
        Log.i(TAG, "done");
        finish();
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "onDestroy");
        super.onDestroy();
    }
}
