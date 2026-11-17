/*
 * Host stub for libstats_jni.so.
 *
 * android.util.StatsLog's static initializer calls System.loadLibrary(
 * "stats_jni") unconditionally.  The real library talks to statsd through
 * binder, which does not exist in droidlet.  Without it, class initialization
 * fails and the ART ReferenceQueueDaemon dies the first time the framework
 * logs a post-GC metrics snapshot.
 *
 * The only native entry point StatsLog declares is writeImpl(byte[], int,
 * int).  Dropping the event is the correct behaviour for a host runtime with
 * no statsd.
 */

#include <jni.h>

extern "C" JNIEXPORT void JNICALL
Java_android_util_StatsLog_writeImpl(JNIEnv*, jclass, jbyteArray, jint, jint) {
    // Intentionally empty: no statsd in the droidlet process.
}
