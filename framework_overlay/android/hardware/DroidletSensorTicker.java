/*
 * Synthetic sensor ticker used by droidlet's sensors JNI stub.
 *
 * The stub posts this Runnable to a Handler on the listener's looper.  It
 * calls back into native code, which dispatches SensorEvents to the real
 * SystemSensorManager.BaseEventQueue on that same thread.
 */

package android.hardware;

final class DroidletSensorTicker implements Runnable {
    private final long mToken;

    DroidletSensorTicker(long token) {
        mToken = token;
    }

    @Override
    public void run() {
        nativeTick(mToken);
    }

    private static native void nativeTick(long token);
}
