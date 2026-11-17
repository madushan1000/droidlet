package com.example.droidlet.vulkanfixture;

import android.view.Surface;

public final class Native {
    static {
        System.loadLibrary("vulkanfixture");
    }

    private Native() {}

    public static native int render(Surface surface);
}
