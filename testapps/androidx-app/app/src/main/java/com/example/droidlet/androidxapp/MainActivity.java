package com.example.droidlet.androidxapp;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.util.Log;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.FileProvider;
import androidx.work.OneTimeWorkRequest;
import androidx.work.WorkManager;

import java.io.File;
import java.io.FileOutputStream;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "DroidletAndroidX";

    private TextView mText;
    private int mStartupSeen;
    private int mFileProviderSeen;
    private int mClipboardSeen;
    private int mSettingsSeen;
    private int mConnectivitySeen;
    private int mVibratorSeen;
    private int mAccountsSeen;
    private int mWorkManagerSeen;
    private String mError = "";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_main);
        mText = (TextView) findViewById(R.id.text);
        Log.i(TAG, "AppCompatActivity.onCreate");

        mStartupSeen = HostInitializer.sRan ? 1 : 0;
        Log.i(TAG, "Startup initializer=" + (mStartupSeen != 0 ? "ran" : "missing"));
        checkFileProvider();
        checkClipboard();
        checkSettings();
        checkConnectivity();
        checkVibrator();
        checkAccounts();
        checkWorkManager();
        updateText();

        new Handler(Looper.getMainLooper()).postDelayed(this::updateText, 1500);
    }

    private void checkFileProvider() {
        try {
            File file = new File(getCacheDir(), "hello.txt");
            try (FileOutputStream out = new FileOutputStream(file)) {
                out.write("droidlet".getBytes("UTF-8"));
            }
            Uri uri = FileProvider.getUriForFile(this,
                    getPackageName() + ".files", file);
            mFileProviderSeen = uri != null && "content".equals(uri.getScheme()) ? 1 : 0;
            Log.i(TAG, "FileProvider=" + (mFileProviderSeen != 0 ? "ok" : "failed") + " " + uri);
        } catch (Throwable t) {
            fail("FileProvider", t);
        }
    }

    private void checkClipboard() {
        try {
            ClipboardManager clipboard =
                    (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText("droidlet", "clip-value"));
            ClipData clip = clipboard.getPrimaryClip();
            mClipboardSeen = clip != null && clip.getItemCount() > 0
                    && "clip-value".contentEquals(clip.getItemAt(0).coerceToText(this)) ? 1 : 0;
            Log.i(TAG, "Clipboard=" + (mClipboardSeen != 0 ? "ok" : "failed"));
        } catch (Throwable t) {
            fail("Clipboard", t);
        }
    }

    private void checkSettings() {
        try {
            String androidId = Settings.Secure.getString(
                    getContentResolver(), Settings.Secure.ANDROID_ID);
            int adb = Settings.Global.getInt(getContentResolver(),
                    Settings.Global.ADB_ENABLED, 0);
            mSettingsSeen = androidId != null ? 1 : 0;
            Log.i(TAG, "Settings androidId=" + androidId + " adb=" + adb);
        } catch (Throwable t) {
            fail("Settings", t);
        }
    }

    private void checkConnectivity() {
        try {
            ConnectivityManager cm =
                    (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
            Network network = cm.getActiveNetwork();
            NetworkCapabilities caps = network == null ? null
                    : cm.getNetworkCapabilities(network);
            mConnectivitySeen = 1;
            Log.i(TAG, "Connectivity network=" + network + " caps=" + caps);
        } catch (Throwable t) {
            fail("Connectivity", t);
        }
    }

    private void checkVibrator() {
        try {
            Vibrator vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            boolean has = vibrator != null && vibrator.hasVibrator();
            vibrator.vibrate(VibrationEffect.createOneShot(10, VibrationEffect.DEFAULT_AMPLITUDE));
            mVibratorSeen = 1;
            Log.i(TAG, "Vibrator hasVibrator=" + has);
        } catch (Throwable t) {
            fail("Vibrator", t);
        }
    }

    private void checkAccounts() {
        try {
            AccountManager manager = AccountManager.get(this);
            Account[] accounts = manager.getAccounts();
            mAccountsSeen = 1;
            Log.i(TAG, "Accounts count=" + (accounts == null ? "null" : accounts.length));
        } catch (Throwable t) {
            fail("Accounts", t);
        }
    }

    private void checkWorkManager() {
        try {
            WorkManager workManager = WorkManager.getInstance(this);
            workManager.enqueue(new OneTimeWorkRequest.Builder(EchoWorker.class).build());
            mWorkManagerSeen = 1;
            Log.i(TAG, "WorkManager=enqueued");
        } catch (Throwable t) {
            fail("WorkManager", t);
        }
    }

    private void fail(String what, Throwable t) {
        Log.e(TAG, what + " failed", t);
        if (mError.isEmpty()) {
            mError = what;
        }
    }

    private void updateText() {
        if (mText == null) {
            return;
        }
        boolean worker = EchoWorker.sRan;
        mText.setText("AppCompat: ok"
                + "\nStartup: " + (mStartupSeen != 0 ? "ok" : "missing")
                + "\nFileProvider: " + (mFileProviderSeen != 0 ? "ok" : "missing")
                + "\nClipboard: " + (mClipboardSeen != 0 ? "ok" : "missing")
                + "\nSettings: " + (mSettingsSeen != 0 ? "ok" : "missing")
                + "\nConnectivity: " + (mConnectivitySeen != 0 ? "ok" : "missing")
                + "\nVibrator: " + (mVibratorSeen != 0 ? "ok" : "missing")
                + "\nAccounts: " + (mAccountsSeen != 0 ? "ok" : "missing")
                + "\nWorkManager: " + (mWorkManagerSeen != 0 ? "enqueued" : "missing")
                + (worker ? " (ran)" : "")
                + (mError.isEmpty() ? "" : "\nError: " + mError));
        Log.i(TAG, "status " + mText.getText().toString().replace('\n', ' '));
    }
}
