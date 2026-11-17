/*
 * Host-build replacement for com.android.internal.os.DebugStore.
 *
 * The real implementation calls into the Rust debugstore bridge through JNI,
 * which is not part of the droidlet host build.  ActivityThread records events
 * unconditionally while binding and launching, so the first call aborts the
 * process with UnsatisfiedLinkError.
 *
 * Events are only diagnostic, so this overlay keeps the public API and drops
 * every event.
 */

package com.android.internal.os;

import android.content.Intent;
import android.content.pm.ServiceInfo;

import java.util.List;

public class DebugStore {
    public static void setDebugStoreNative(DebugStoreNative nativeImpl) {}

    public static void recordScheduleServiceStart(int msgId, Intent intent) {}

    public static long recordServiceStart(int msgId) {
        return 0;
    }

    public static void recordScheduleServiceCreate(int msgId, ServiceInfo serviceInfo) {}

    public static long recordServiceCreate(int msgId) {
        return 0;
    }

    public static void recordScheduleServiceBind(int msgId, Intent intent) {}

    public static long recordServiceBind(int msgId) {
        return 0;
    }

    public static void recordScheduleBroadcastReceive(int pendingResultId, Intent intent) {}

    public static long recordBroadcastReceive(int pendingResultId, String receiverClass) {
        return 0;
    }

    public static void recordScheduleBroadcastReceiveReg(int pendingResultId, Intent intent) {}

    public static long recordBroadcastReceiveReg(int pendingResultId, String receiverClass) {
        return 0;
    }

    public static void recordGoAsync(int pendingResultId) {}

    public static void recordFinish(int pendingResultId) {}

    public static void recordSendFinished(int pendingResultId) {}

    public static void recordScheduleBindApplication() {}

    public static long recordBindApplication() {
        return 0;
    }

    public static void recordScheduleStartJob(int msgId, int jobId, String jobNamespace) {}

    public static long recordStartJob(int msgId) {
        return 0;
    }

    public static void recordScheduleStopJob(int msgId, int jobId, String jobNamespace) {}

    public static long recordStopJob(int msgId) {
        return 0;
    }

    public static void recordLongLooperMessage(int messageCode, String targetClass,
            long elapsedTimeMs) {}

    public static void recordEventEnd(long id) {}

    public interface DebugStoreNative {
        long beginEvent(String eventName, List<String> attributes);

        void endEvent(long id, List<String> attributes);

        void recordEvent(String eventName, List<String> attributes);
    }
}
