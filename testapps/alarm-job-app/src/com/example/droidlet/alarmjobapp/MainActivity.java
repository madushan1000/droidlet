package com.example.droidlet.alarmjobapp;

import android.app.Activity;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.app.job.JobInfo;
import android.app.job.JobScheduler;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

public class MainActivity extends Activity {
    public static final String TAG = "AlarmJobApp";
    public static final String ACTION_ALARM = "com.example.droidlet.alarmjob.ALARM";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i(TAG, "onCreate");

        Intent alarm = new Intent(this, AlarmReceiver.class).setAction(ACTION_ALARM);
        PendingIntent pending = PendingIntent.getBroadcast(this, 0, alarm,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        AlarmManager manager = getSystemService(AlarmManager.class);
        manager.setExact(AlarmManager.RTC_WAKEUP, System.currentTimeMillis() + 2000, pending);
        Log.i(TAG, "alarm scheduled");

        JobScheduler scheduler = getSystemService(JobScheduler.class);
        ComponentName jobService = new ComponentName(this, TestJobService.class);
        int result = scheduler.schedule(new JobInfo.Builder(1, jobService).build());
        Log.i(TAG, "job scheduled result=" + result);
        int delayed = scheduler.schedule(new JobInfo.Builder(2, jobService)
                .setMinimumLatency(1500).build());
        Log.i(TAG, "delayed job scheduled result=" + delayed);

        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                Log.i(TAG, "activity finished; alarm remains");
                finish();
            }
        }, 400);
    }
}
