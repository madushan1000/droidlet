/*
 * Minimal host stubs for android.tracing.perfetto.
 *
 * The real implementation depends on the Perfetto SDK, which is not built for
 * the droidlet host.  ProtoLog (initialized from handleLaunchActivity) creates
 * a DataSource and a Producer unconditionally, so the native methods must
 * resolve.  With a zero native handle nothing is registered and tracing stays
 * permanently inactive, which is exactly what we want.
 */

#define LOG_TAG "PerfettoStub"

#include <nativehelper/JNIHelp.h>

#include "core_jni_helpers.h"

namespace android {

static jlong nativeCreate(JNIEnv*, jclass, jobject, jstring) {
    return 0;
}

static void nativeFlushAll(JNIEnv*, jclass, jlong) {}

static jlong nativeGetFinalizer(JNIEnv*, jclass) {
    return 0;
}

static void nativeRegisterDataSource(JNIEnv*, jclass, jlong, jint, jboolean, jboolean, jboolean) {}

static jobject nativeGetPerfettoInstanceLocked(JNIEnv*, jclass, jlong, jint) {
    return nullptr;
}

static void nativeReleasePerfettoInstanceLocked(JNIEnv*, jclass, jlong, jint) {}
static void nativeStopDonePerfettoInstanceLocked(JNIEnv*, jclass, jlong, jint) {}
static void nativeWritePackets(JNIEnv*, jclass, jlong, jobjectArray) {}

static jboolean nativePerfettoDsTraceIterateBegin(CRITICAL_JNI_PARAMS_COMMA jlong) {
    return JNI_FALSE;
}

static jboolean nativePerfettoDsTraceIterateNext(CRITICAL_JNI_PARAMS_COMMA jlong) {
    return JNI_FALSE;
}

static void nativePerfettoDsTraceIterateBreak(CRITICAL_JNI_PARAMS_COMMA jlong) {}
static jint nativeGetPerfettoDsInstanceIndex(CRITICAL_JNI_PARAMS_COMMA jlong) {
    return 0;
}

static jobject nativeGetCustomTls(JNIEnv*, jclass, jlong) {
    return nullptr;
}

static void nativeSetCustomTls(JNIEnv*, jclass, jlong, jobject) {}
static jobject nativeGetIncrementalState(JNIEnv*, jclass, jlong) {
    return nullptr;
}

static void nativeSetIncrementalState(JNIEnv*, jclass, jlong, jobject) {}
static void nativePerfettoProducerInit(JNIEnv*, jclass, jint, jint) {}

static const JNINativeMethod gDataSourceMethods[] = {
        {"nativeCreate", "(Landroid/tracing/perfetto/DataSource;Ljava/lang/String;)J",
         (void*)nativeCreate},
        {"nativeFlushAll", "(J)V", (void*)nativeFlushAll},
        {"nativeGetFinalizer", "()J", (void*)nativeGetFinalizer},
        {"nativeRegisterDataSource", "(JIZZZ)V", (void*)nativeRegisterDataSource},
        {"nativeGetPerfettoInstanceLocked", "(JI)Landroid/tracing/perfetto/DataSourceInstance;",
         (void*)nativeGetPerfettoInstanceLocked},
        {"nativeReleasePerfettoInstanceLocked", "(JI)V",
         (void*)nativeReleasePerfettoInstanceLocked},
        {"nativeStopDonePerfettoInstanceLocked", "(JI)V",
         (void*)nativeStopDonePerfettoInstanceLocked},
        {"nativeWritePackets", "(J[[B)V", (void*)nativeWritePackets},
        {"nativePerfettoDsTraceIterateBegin", "(J)Z", (void*)nativePerfettoDsTraceIterateBegin},
        {"nativePerfettoDsTraceIterateNext", "(J)Z", (void*)nativePerfettoDsTraceIterateNext},
        {"nativePerfettoDsTraceIterateBreak", "(J)V", (void*)nativePerfettoDsTraceIterateBreak},
        {"nativeGetPerfettoDsInstanceIndex", "(J)I", (void*)nativeGetPerfettoDsInstanceIndex},
};

static const JNINativeMethod gTracingContextMethods[] = {
        {"nativeGetCustomTls", "(J)Ljava/lang/Object;", (void*)nativeGetCustomTls},
        {"nativeGetIncrementalState", "(J)Ljava/lang/Object;", (void*)nativeGetIncrementalState},
        {"nativeSetCustomTls", "(JLjava/lang/Object;)V", (void*)nativeSetCustomTls},
        {"nativeSetIncrementalState", "(JLjava/lang/Object;)V", (void*)nativeSetIncrementalState},
};

static const JNINativeMethod gProducerMethods[] = {
        {"nativePerfettoProducerInit", "(II)V", (void*)nativePerfettoProducerInit},
};

int register_android_tracing_PerfettoDataSource(JNIEnv* env) {
    int res = jniRegisterNativeMethods(env, "android/tracing/perfetto/DataSource",
                                       gDataSourceMethods, NELEM(gDataSourceMethods));
    LOG_ALWAYS_FATAL_IF(res < 0, "Unable to register Perfetto DataSource natives.");
    res = jniRegisterNativeMethods(env, "android/tracing/perfetto/TracingContext",
                                   gTracingContextMethods, NELEM(gTracingContextMethods));
    LOG_ALWAYS_FATAL_IF(res < 0, "Unable to register Perfetto TracingContext natives.");
    return 0;
}

int register_android_tracing_PerfettoProducer(JNIEnv* env) {
    int res = jniRegisterNativeMethods(env, "android/tracing/perfetto/Producer",
                                       gProducerMethods, NELEM(gProducerMethods));
    LOG_ALWAYS_FATAL_IF(res < 0, "Unable to register Perfetto Producer natives.");
    return 0;
}

}  // namespace android
