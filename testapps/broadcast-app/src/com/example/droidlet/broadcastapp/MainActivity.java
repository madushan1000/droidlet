package com.example.droidlet.broadcastapp;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

public class MainActivity extends android.app.Activity {
    private static final String TAG = "BroadcastApp";
    public static final String ACTION_PING = "com.example.droidlet.broadcast.PING";
    public static final String ACTION_ORDERED = "com.example.droidlet.broadcast.ORDERED";

    private final BroadcastReceiver mPing = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            Log.i(TAG, "dynamic ping=" + intent.getStringExtra("value"));
        }
    };
    private final BroadcastReceiver mOrdered = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            Log.i(TAG, "ordered receiver");
            setResultCode(7);
            setResultData("seven");
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i(TAG, "onCreate");
        registerReceiver(mPing, new IntentFilter(ACTION_PING));
        registerReceiver(mOrdered, new IntentFilter(ACTION_ORDERED));

        Intent ping = new Intent(ACTION_PING).setPackage(getPackageName());
        ping.putExtra("value", "hello");
        sendBroadcast(ping);

        sendOrderedBroadcast(new Intent(ACTION_ORDERED).setPackage(getPackageName()), null,
                new BroadcastReceiver() {
                    @Override
                    public void onReceive(Context context, Intent intent) {
                        Log.i(TAG, "ordered result=" + getResultCode() + " "
                                + getResultData());
                        finishSoon();
                    }
                }, null, 0, "initial", null);
    }

    private void finishSoon() {
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                unregisterReceiver(mPing);
                unregisterReceiver(mOrdered);
                Log.i(TAG, "done");
                finish();
            }
        }, 100);
    }
}
