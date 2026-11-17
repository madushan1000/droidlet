package com.example.droidlet.androidxapp;

import android.content.Context;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

public class EchoWorker extends Worker {
    private static final String TAG = "DroidletAndroidX";

    public EchoWorker(@NonNull Context context, @NonNull WorkerParameters params) {
        super(context, params);
    }

    @NonNull
    @Override
    public Result doWork() {
        Log.i(TAG, "WorkManager worker ran");
        sRan = true;
        return Result.success();
    }

    public static volatile boolean sRan;
}
