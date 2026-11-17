package com.example.droidlet.jnifixture;

import android.app.Activity;
import android.opengl.EGL14;
import android.opengl.EGLDisplay;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final String TAG = "JniFixture";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final int sum = Native.add(20, 22);
        final String description = Native.describe();
        final boolean assetManager = Native.hasAssetManager();
        final boolean sensorManager = Native.hasSensorManager();
        final boolean egl = probeEgl();
        final String nativeDir = getApplicationInfo().nativeLibraryDir;

        Log.i(TAG, "JNI ADD=" + sum + " DESC=" + description
                + " ASSET_MANAGER=" + assetManager + " SENSOR_MANAGER=" + sensorManager
                + " EGL=" + egl + " DIR=" + nativeDir);

        final TextView text = new TextView(this);
        text.setTextSize(22);
        text.setGravity(Gravity.CENTER);
        text.setText("JNI ADD=" + sum + "\n" + description
                + "\nASSET_MANAGER=" + assetManager
                + "\nSENSOR_MANAGER=" + sensorManager
                + "\nEGL=" + egl + "\n" + nativeDir);
        setContentView(text);
    }

    private static boolean probeEgl() {
        final EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        if (display == EGL14.EGL_NO_DISPLAY) {
            return false;
        }
        final int[] major = new int[1];
        final int[] minor = new int[1];
        return EGL14.eglInitialize(display, major, 0, minor, 0);
    }
}
