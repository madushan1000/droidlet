package com.example.droidlet.androidxapp;

import android.content.Context;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.startup.Initializer;

import java.util.Collections;
import java.util.List;

public class HostInitializer implements Initializer<Boolean> {
    private static final String TAG = "DroidletAndroidX";

    public static volatile boolean sRan;

    @NonNull
    @Override
    public Boolean create(@NonNull Context context) {
        sRan = true;
        Log.i(TAG, "Startup initializer ran");
        return true;
    }

    @NonNull
    @Override
    public List<Class<? extends Initializer<?>>> dependencies() {
        return Collections.emptyList();
    }
}
