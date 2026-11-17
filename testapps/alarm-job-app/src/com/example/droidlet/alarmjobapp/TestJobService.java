package com.example.droidlet.alarmjobapp;

import android.app.job.JobParameters;
import android.app.job.JobService;
import android.util.Log;

public class TestJobService extends JobService {
    @Override
    public boolean onStartJob(JobParameters params) {
        Log.i(MainActivity.TAG, "job started id=" + params.getJobId());
        jobFinished(params, false);
        Log.i(MainActivity.TAG, "job finished id=" + params.getJobId());
        return false;
    }

    @Override
    public boolean onStopJob(JobParameters params) {
        Log.i(MainActivity.TAG, "job stopped id=" + params.getJobId());
        return false;
    }
}
