package com.example.droidlet.providerapp;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.database.Cursor;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import java.util.List;

public class MainActivity extends Activity {
    private static final String TAG = "DroidletProvider";
    private TextView mText;
    private int mProviderValueSeen;
    private int mPermissionResultSeen;
    private int mPackageManagerSeen;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        mText = (TextView) findViewById(R.id.text);
        Log.i(TAG, "Activity.onCreate");

        Cursor cursor = getContentResolver().query(
                MyProvider.CONTENT_URI, new String[] {"value"}, null, null, null);
        String value = "no cursor";
        if (cursor != null) {
            try {
                if (cursor.moveToFirst()) {
                    value = cursor.getString(0);
                }
            } finally {
                cursor.close();
            }
        }
        mProviderValueSeen = "provider-ready".equals(value) ? 1 : 0;
        Log.i(TAG, "query result=" + value);

        mPackageManagerSeen = checkPackageManager() ? 1 : 0;

        requestPermissions(new String[] {"android.permission.CAMERA"}, 7);
        updateText();
    }

    private boolean checkPackageManager() {
        PackageManager pm = getPackageManager();
        String pkg = getPackageName();
        boolean ok = true;

        try {
            PackageInfo info = pm.getPackageInfo(pkg, 0);
            ok &= check("getPackageInfo", info != null && pkg.equals(info.packageName));
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "getPackageInfo failed", t);
        }

        try {
            PackageInfo withActivities =
                    pm.getPackageInfo(pkg, PackageManager.GET_ACTIVITIES);
            ok &= check("getPackageInfo(GET_ACTIVITIES)",
                    withActivities != null && withActivities.activities != null
                            && withActivities.activities.length > 0);
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "getPackageInfo(GET_ACTIVITIES) failed", t);
        }

        try {
            ApplicationInfo app = pm.getApplicationInfo(pkg, 0);
            ok &= check("getApplicationInfo",
                    app != null && MyApplication.class.getName().equals(app.className));
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "getApplicationInfo failed", t);
        }

        try {
            ActivityInfo activity = pm.getActivityInfo(getComponentName(), 0);
            ok &= check("getActivityInfo",
                    activity != null && MainActivity.class.getName().equals(activity.name));
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "getActivityInfo failed", t);
        }

        try {
            List<PackageInfo> packages = pm.getInstalledPackages(0);
            ok &= check("getInstalledPackages", packages != null && !packages.isEmpty());
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "getInstalledPackages failed", t);
        }

        try {
            List<ApplicationInfo> apps = pm.getInstalledApplications(0);
            ok &= check("getInstalledApplications", apps != null && !apps.isEmpty());
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "getInstalledApplications failed", t);
        }

        try {
            Intent main = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
                    .setPackage(pkg);
            List<ResolveInfo> resolved = pm.queryIntentActivities(main, 0);
            ok &= check("queryIntentActivities", resolved != null && !resolved.isEmpty());
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "queryIntentActivities failed", t);
        }

        try {
            Intent main = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
                    .setPackage(pkg);
            ResolveInfo resolved = pm.resolveActivity(main, 0);
            ok &= check("resolveActivity", resolved != null
                    && resolved.activityInfo != null
                    && MainActivity.class.getName().equals(resolved.activityInfo.name));
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "resolveActivity failed", t);
        }

        try {
            Intent launch = pm.getLaunchIntentForPackage(pkg);
            ok &= check("getLaunchIntentForPackage", launch != null
                    && getComponentName().equals(launch.getComponent()));
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "getLaunchIntentForPackage failed", t);
        }

        try {
            ok &= check("hasSystemFeature",
                    pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN));
        } catch (Throwable t) {
            ok = false;
            Log.e(TAG, "hasSystemFeature failed", t);
        }

        Log.i(TAG, "package manager checks=" + (ok ? "ok" : "failed"));
        return ok;
    }

    private boolean check(String name, boolean result) {
        Log.i(TAG, "pm." + name + "=" + (result ? "ok" : "failed"));
        return result;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
            int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        boolean granted = grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED;
        mPermissionResultSeen = granted ? 1 : 0;
        Log.i(TAG, "permission result request=" + requestCode + " granted=" + granted);
        updateText();
    }

    private void updateText() {
        if (mText != null) {
            mText.setText("Provider: " + (mProviderValueSeen != 0 ? "ready" : "missing")
                    + "\nPackageManager: " + (mPackageManagerSeen != 0 ? "ok" : "failed")
                    + "\nPermission: " + (mPermissionResultSeen != 0 ? "granted" : "denied"));
        }
    }
}
