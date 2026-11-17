/*
 * droidlet sensor fixture: initializes SensorManager, checks the virtual
 * sensor list, and receives a few synthetic accelerometer events.
 */

package com.example.droidlet.sensorapp;

import android.app.Activity;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

public class MainActivity extends Activity implements SensorEventListener {
    private static final String TAG = "SensorApp";

    private SensorManager mManager;
    private Sensor mAccelerometer;
    private int mEvents;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        mManager = (SensorManager) getSystemService(SENSOR_SERVICE);
        Log.i(TAG, "SensorManager created");
        Log.i(TAG, "sensor count=" + mManager.getSensorList(Sensor.TYPE_ALL).size());
        mAccelerometer = mManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        Log.i(TAG, "accelerometer=" + mAccelerometer);
        boolean registered = mManager.registerListener(this, mAccelerometer,
                SensorManager.SENSOR_DELAY_NORMAL);
        Log.i(TAG, "registerListener=" + registered);
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                finishNow();
            }
        }, 5000);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (mEvents == 0 && event.values.length >= 3) {
            Log.i(TAG, "event x=" + event.values[0] + " y=" + event.values[1]
                    + " z=" + event.values[2]);
        }
        mEvents++;
        if (mEvents >= 3) {
            finishNow();
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        Log.i(TAG, "accuracy=" + accuracy);
    }

    private void finishNow() {
        if (isFinishing()) {
            return;
        }
        Log.i(TAG, "events=" + mEvents);
        mManager.unregisterListener(this);
        finish();
    }
}
