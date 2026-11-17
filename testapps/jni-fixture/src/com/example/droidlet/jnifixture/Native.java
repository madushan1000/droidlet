package com.example.droidlet.jnifixture;

public final class Native {
    static {
        System.loadLibrary("jnifixture");
    }

    private Native() {}

    public static native int add(int a, int b);

    public static native String describe();

    public static native boolean hasAssetManager();

    public static native boolean hasSensorManager();
}
