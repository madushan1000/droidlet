#include <jni.h>
#include <stdint.h>
#include <stdio.h>

extern "C" int jni_fixture_add(int a, int b);
extern "C" int __android_log_print(int prio, const char* tag, const char* fmt, ...);
extern "C" void* AAssetManager_fromJava(JNIEnv* env, jobject assetManager);
struct ASensorManager;
extern "C" ASensorManager* ASensorManager_getInstance();

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    __android_log_print(4, "JniFixture", "JNI_OnLoad");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_droidlet_jnifixture_Native_add(JNIEnv* env, jclass, jint a, jint b) {
    (void)env;
    return jni_fixture_add(a, b);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_droidlet_jnifixture_Native_describe(JNIEnv* env, jclass) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "JNI_OK add=%d", jni_fixture_add(1, 2));
    __android_log_print(4, "JniFixture", "describe: %s", buffer);
    return env->NewStringUTF(buffer);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_droidlet_jnifixture_Native_hasAssetManager(JNIEnv* env, jclass) {
    (void)env;
    volatile auto fn = &AAssetManager_fromJava;
    return fn != nullptr ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_droidlet_jnifixture_Native_hasSensorManager(JNIEnv* env, jclass) {
    (void)env;
    return ASensorManager_getInstance() != nullptr ? JNI_TRUE : JNI_FALSE;
}
