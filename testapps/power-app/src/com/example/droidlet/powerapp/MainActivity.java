/*
 * droidlet power fixture: PowerManager's SystemServiceRegistry fetcher
 * resolves both the "power" and "thermalservice" services.  Glmark2Activity
 * takes a screen wake lock in onCreate, so a missing service must fail here
 * with a NullPointerException rather than in glmark2.
 */

package com.example.droidlet.powerapp;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

public class MainActivity extends Activity {
    private static final String TAG = "PowerApp";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        Log.i(TAG, "PowerManager created=" + (pm != null));
        Log.i(TAG, "interactive=" + pm.isInteractive()
                + " wakeLockLevelSupported="
                + pm.isWakeLockLevelSupported(PowerManager.SCREEN_DIM_WAKE_LOCK));

        PowerManager.WakeLock lock =
                pm.newWakeLock(PowerManager.SCREEN_DIM_WAKE_LOCK, TAG);
        lock.acquire();
        Log.i(TAG, "wakelock held=" + lock.isHeld());
        lock.release();
        Log.i(TAG, "wakelock released held=" + lock.isHeld());

        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                finish();
            }
        }, 300);
    }
}
