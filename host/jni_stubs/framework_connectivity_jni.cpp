/*
 * Host stub for libframework-connectivity-jni.so.
 *
 * android.net.NetworkUtils loads this library in its static initializer.
 * ActivityThread.handleBindApplication() constructs ConnectivityManager and
 * calls getDefaultProxy(), so the class initializes during every app bind even
 * when the app never touches the network.  There is no kernel networking
 * stack in droidlet; the process has no bound network and DNS lookups are not
 * available.
 *
 * Only the native entry points NetworkUtils declares are provided.  Returning
 * "no network" keeps getDefaultProxy() and connectivity manager bookkeeping
 * happy without pulling in the real connectivity module.
 */

#include <jni.h>

extern "C" JNIEXPORT jlong JNICALL
Java_android_net_NetworkUtils_getBoundNetworkHandleForProcess(JNIEnv*, jclass) {
    return 0;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_android_net_NetworkUtils_bindProcessToNetworkHandle(JNIEnv*, jclass, jlong) {
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_android_net_NetworkUtils_bindProcessToNetworkForHostResolution(
        JNIEnv*, jclass, jint) {
    return JNI_TRUE;
}

extern "C" JNIEXPORT jint JNICALL
Java_android_net_NetworkUtils_bindSocketToNetworkHandle(
        JNIEnv*, jclass, jobject, jlong) {
    return 0;
}

extern "C" JNIEXPORT void JNICALL
Java_android_net_NetworkUtils_attachDropAllBPFFilter(JNIEnv*, jclass, jobject) {
}

extern "C" JNIEXPORT void JNICALL
Java_android_net_NetworkUtils_detachBPFFilter(JNIEnv*, jclass, jobject) {
}

extern "C" JNIEXPORT jobject JNICALL
Java_android_net_NetworkUtils_resNetworkSend(
        JNIEnv*, jclass, jlong, jbyteArray, jint, jint) {
    return nullptr;
}

extern "C" JNIEXPORT jobject JNICALL
Java_android_net_NetworkUtils_resNetworkQuery(
        JNIEnv*, jclass, jlong, jstring, jint, jint, jint) {
    return nullptr;
}

extern "C" JNIEXPORT jobject JNICALL
Java_android_net_NetworkUtils_resNetworkResult(JNIEnv*, jclass, jobject) {
    return nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_android_net_NetworkUtils_resNetworkCancel(JNIEnv*, jclass, jobject) {
}

extern "C" JNIEXPORT jobject JNICALL
Java_android_net_NetworkUtils_getDnsNetwork(JNIEnv*, jclass) {
    return nullptr;
}

extern "C" JNIEXPORT jobject JNICALL
Java_android_net_NetworkUtils_getTcpRepairWindow(JNIEnv*, jclass, jobject) {
    return nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_android_net_NetworkUtils_setsockoptBytes(
        JNIEnv*, jclass, jobject, jint, jint, jbyteArray) {
}

extern "C" JNIEXPORT jboolean JNICALL
Java_android_net_NetworkUtils_isKernel64Bit(JNIEnv*, jclass) {
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_android_net_NetworkUtils_isKernelX86(JNIEnv*, jclass) {
    return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_android_net_NetworkUtils_getSocketCookie(JNIEnv*, jclass, jobject) {
    return 0;
}
