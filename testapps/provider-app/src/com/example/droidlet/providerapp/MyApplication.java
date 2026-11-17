package com.example.droidlet.providerapp;

import android.app.Application;
import android.util.Log;

public class MyApplication extends Application {
    private static final String TAG = "DroidletProvider";

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "Application.onCreate");
    }
}
