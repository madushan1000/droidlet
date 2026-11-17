/*
 * Host stub for the sensors JNI surface.
 *
 * android.hardware.SystemSensorManager calls nativeClassInit() the first time
 * an app asks for SENSOR_SERVICE.  AOSP registers these methods from
 * frameworks/base/core/jni/android_hardware_SensorManager.cpp, which is not
 * part of the droidlet build because it needs a real sensor HAL and
 * SensorService.
 *
 * droidlet has no sensor HAL, but many apps assume an accelerometer or
 * gyroscope exists.  Advertise a small set of virtual sensors with plausible
 * constant values and deliver synthetic SensorEvents through the Java
 * BaseEventQueue on the listener's looper.  Set DROIDLET_VIRTUAL_SENSORS=0 to
 * go back to an empty sensor list.
 *
 * Only the sensor manager and event-queue methods used by Android apps are
 * implemented; direct channels, data injection and runtime sensors remain
 * unsupported.
 */

#include <jni.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <utility>
#include <vector>

#define ALOGE(...) do { \
        fprintf(stderr, "droidlet-sensors: "); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } while (0)
#define ALOGI(...) do { \
        fprintf(stdout, "droidlet-sensors: "); \
        fprintf(stdout, __VA_ARGS__); \
        fprintf(stdout, "\n"); \
    } while (0)

namespace {

// Opaque non-null handles.  The Java layer never dereferences them; it only
// passes them back to these stubs.
constexpr jlong kFakeSensorManager = 1;
constexpr jint kVirtualHandleBase = 0x7d000000;

// android.hardware.Sensor type constants used by the virtual sensors.
enum {
    TYPE_ACCELEROMETER = 1,
    TYPE_MAGNETIC_FIELD = 2,
    TYPE_GYROSCOPE = 4,
    TYPE_LIGHT = 5,
    TYPE_PROXIMITY = 8,
    TYPE_GRAVITY = 9,
    TYPE_LINEAR_ACCELERATION = 10,
    TYPE_ROTATION_VECTOR = 11,
    TYPE_GAME_ROTATION_VECTOR = 15,
    TYPE_STEP_COUNTER = 19,
};

struct VirtualSensor {
    jint id;
    jint type;
    const char* name;
    jfloat maxRange;
    jfloat resolution;
    jint minDelay;
};

const VirtualSensor kSensors[] = {
        {1, TYPE_ACCELEROMETER, "droidlet Accelerometer", 19.6133f, 0.01f, 10000},
        {2, TYPE_MAGNETIC_FIELD, "droidlet Magnetic Field", 2000.0f, 0.1f, 10000},
        {3, TYPE_GYROSCOPE, "droidlet Gyroscope", 34.906585f, 0.001f, 10000},
        {4, TYPE_LIGHT, "droidlet Light", 1000.0f, 0.1f, 0},
        {5, TYPE_PROXIMITY, "droidlet Proximity", 5.0f, 1.0f, 0},
        {6, TYPE_GRAVITY, "droidlet Gravity", 19.6133f, 0.01f, 10000},
        {7, TYPE_LINEAR_ACCELERATION, "droidlet Linear Acceleration", 19.6133f, 0.01f, 10000},
        {8, TYPE_ROTATION_VECTOR, "droidlet Rotation Vector", 1.0f, 0.00001f, 10000},
        {9, TYPE_GAME_ROTATION_VECTOR, "droidlet Game Rotation Vector", 1.0f, 0.00001f, 10000},
        {10, TYPE_STEP_COUNTER, "droidlet Step Counter", 100000.0f, 1.0f, 0},
};

struct SensorIds {
    bool ready = false;
    jfieldID name = nullptr;
    jfieldID vendor = nullptr;
    jfieldID version = nullptr;
    jfieldID handle = nullptr;
    jfieldID maxRange = nullptr;
    jfieldID resolution = nullptr;
    jfieldID power = nullptr;
    jfieldID minDelay = nullptr;
    jfieldID fifoReserved = nullptr;
    jfieldID fifoMax = nullptr;
    jfieldID requiredPermission = nullptr;
    jfieldID maxDelay = nullptr;
    jfieldID flags = nullptr;
    jmethodID setType = nullptr;
    jmethodID setId = nullptr;
    jmethodID setUuid = nullptr;
};

SensorIds gIds;

struct VirtualEventQueue {
    jobject queue = nullptr;
    jobject handler = nullptr;
    jobject ticker = nullptr;
    jmethodID postDelayed = nullptr;
    jmethodID removeCallbacks = nullptr;
    jmethodID dispatchSensorEvent = nullptr;
    jmethodID dispatchFlushComplete = nullptr;
    std::map<jint, jint> sensors;  // handle -> delay in milliseconds
    jint minDelayMs = 200;
    bool scheduled = false;
    bool destroyed = false;
    bool inTick = false;
    bool deletePending = false;
};

bool virtualSensorsEnabled() {
    const char* value = std::getenv("DROIDLET_VIRTUAL_SENSORS");
    return value == nullptr || value[0] != '0';
}

jfieldID getField(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    jfieldID id = env->GetFieldID(clazz, name, signature);
    if (id == nullptr) {
        env->ExceptionClear();
        ALOGE("missing Sensor.%s", name);
    }
    return id;
}

jmethodID getMethod(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    jmethodID id = env->GetMethodID(clazz, name, signature);
    if (id == nullptr) {
        env->ExceptionClear();
        ALOGE("missing Sensor.%s", name);
    }
    return id;
}

bool initSensorIds(JNIEnv* env) {
    if (gIds.ready) {
        return true;
    }
    jclass sensorClass = env->FindClass("android/hardware/Sensor");
    if (sensorClass == nullptr) {
        env->ExceptionClear();
        ALOGE("android.hardware.Sensor not found");
        return false;
    }
    gIds.name = getField(env, sensorClass, "mName", "Ljava/lang/String;");
    gIds.vendor = getField(env, sensorClass, "mVendor", "Ljava/lang/String;");
    gIds.version = getField(env, sensorClass, "mVersion", "I");
    gIds.handle = getField(env, sensorClass, "mHandle", "I");
    gIds.maxRange = getField(env, sensorClass, "mMaxRange", "F");
    gIds.resolution = getField(env, sensorClass, "mResolution", "F");
    gIds.power = getField(env, sensorClass, "mPower", "F");
    gIds.minDelay = getField(env, sensorClass, "mMinDelay", "I");
    gIds.fifoReserved = getField(env, sensorClass, "mFifoReservedEventCount", "I");
    gIds.fifoMax = getField(env, sensorClass, "mFifoMaxEventCount", "I");
    gIds.requiredPermission = getField(env, sensorClass, "mRequiredPermission",
            "Ljava/lang/String;");
    gIds.maxDelay = getField(env, sensorClass, "mMaxDelay", "I");
    gIds.flags = getField(env, sensorClass, "mFlags", "I");
    gIds.setType = getMethod(env, sensorClass, "setType", "(I)Z");
    gIds.setId = getMethod(env, sensorClass, "setId", "(I)V");
    gIds.setUuid = getMethod(env, sensorClass, "setUuid", "(JJ)V");
    gIds.ready = gIds.name != nullptr && gIds.vendor != nullptr && gIds.version != nullptr
            && gIds.handle != nullptr && gIds.maxRange != nullptr
            && gIds.resolution != nullptr && gIds.power != nullptr
            && gIds.minDelay != nullptr && gIds.fifoReserved != nullptr
            && gIds.fifoMax != nullptr && gIds.requiredPermission != nullptr
            && gIds.maxDelay != nullptr && gIds.flags != nullptr
            && gIds.setType != nullptr && gIds.setId != nullptr && gIds.setUuid != nullptr;
    env->DeleteLocalRef(sensorClass);
    return gIds.ready;
}

bool fillSensor(JNIEnv* env, jobject sensor, const VirtualSensor& sensorInfo) {
    if (!gIds.ready) {
        return false;
    }
    jstring name = env->NewStringUTF(sensorInfo.name);
    jstring vendor = env->NewStringUTF("droidlet");
    jstring permission = env->NewStringUTF("");
    env->SetObjectField(sensor, gIds.name, name);
    env->SetObjectField(sensor, gIds.vendor, vendor);
    env->SetIntField(sensor, gIds.version, 1);
    env->SetIntField(sensor, gIds.handle, kVirtualHandleBase + sensorInfo.id);
    env->SetFloatField(sensor, gIds.maxRange, sensorInfo.maxRange);
    env->SetFloatField(sensor, gIds.resolution, sensorInfo.resolution);
    env->SetFloatField(sensor, gIds.power, 0.5f);
    env->SetIntField(sensor, gIds.minDelay, sensorInfo.minDelay);
    env->SetIntField(sensor, gIds.fifoReserved, 0);
    env->SetIntField(sensor, gIds.fifoMax, 0);
    env->SetObjectField(sensor, gIds.requiredPermission, permission);
    env->SetIntField(sensor, gIds.maxDelay, 0);
    env->SetIntField(sensor, gIds.flags, 0);
    env->CallBooleanMethod(sensor, gIds.setType, sensorInfo.type);
    env->CallVoidMethod(sensor, gIds.setId, sensorInfo.id);
    env->CallVoidMethod(sensor, gIds.setUuid, static_cast<jlong>(sensorInfo.id),
            static_cast<jlong>(0));
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(vendor);
    env->DeleteLocalRef(permission);
    return !env->ExceptionCheck();
}

const VirtualSensor* findSensorByHandle(jint handle) {
    for (const VirtualSensor& sensor : kSensors) {
        if (kVirtualHandleBase + sensor.id == handle) {
            return &sensor;
        }
    }
    return nullptr;
}

int valuesFor(jint type, float* values) {
    switch (type) {
        case TYPE_ACCELEROMETER:
        case TYPE_GRAVITY:
            values[0] = 0.0f;
            values[1] = 0.0f;
            values[2] = 9.81f;
            return 3;
        case TYPE_LINEAR_ACCELERATION:
        case TYPE_GYROSCOPE:
            values[0] = 0.0f;
            values[1] = 0.0f;
            values[2] = 0.0f;
            return 3;
        case TYPE_MAGNETIC_FIELD:
            values[0] = 0.0f;
            values[1] = 0.0f;
            values[2] = 45.0f;
            return 3;
        case TYPE_LIGHT:
            values[0] = 100.0f;
            return 1;
        case TYPE_PROXIMITY:
            values[0] = 0.0f;
            return 1;
        case TYPE_ROTATION_VECTOR:
            values[0] = 0.0f;
            values[1] = 0.0f;
            values[2] = 0.0f;
            values[3] = 1.0f;
            values[4] = 0.0f;
            return 5;
        case TYPE_GAME_ROTATION_VECTOR:
            values[0] = 0.0f;
            values[1] = 0.0f;
            values[2] = 0.0f;
            values[3] = 1.0f;
            return 4;
        case TYPE_STEP_COUNTER:
            values[0] = 0.0f;
            return 1;
        default:
            values[0] = 0.0f;
            return 1;
    }
}

jlong nowNanos() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<jlong>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void destroyQueue(JNIEnv* env, VirtualEventQueue* queue) {
    if (queue == nullptr) {
        return;
    }
    if (queue->handler != nullptr && queue->ticker != nullptr
            && queue->removeCallbacks != nullptr) {
        env->CallVoidMethod(queue->handler, queue->removeCallbacks, queue->ticker);
    }
    queue->destroyed = true;
    if (queue->queue != nullptr) {
        env->DeleteGlobalRef(queue->queue);
        queue->queue = nullptr;
    }
    if (queue->handler != nullptr) {
        env->DeleteGlobalRef(queue->handler);
        queue->handler = nullptr;
    }
    if (queue->ticker != nullptr) {
        env->DeleteGlobalRef(queue->ticker);
        queue->ticker = nullptr;
    }
    if (queue->inTick) {
        queue->deletePending = true;
    } else {
        delete queue;
    }
}

/* ------------------------------------------------------------------------- */
/* SystemSensorManager                                                       */
/* ------------------------------------------------------------------------- */

void nativeClassInit(JNIEnv* env, jclass) {
    if (!virtualSensorsEnabled()) {
        return;
    }
    if (!initSensorIds(env)) {
        ALOGE("virtual sensors disabled: Sensor fields unavailable");
    }
}

jlong nativeCreate(JNIEnv*, jclass, jstring) {
    return kFakeSensorManager;
}

jboolean nativeGetSensorAtIndex(JNIEnv* env, jclass, jlong, jobject sensor, jint index) {
    if (sensor == nullptr || index < 0
            || static_cast<size_t>(index) >= sizeof(kSensors) / sizeof(kSensors[0])) {
        return JNI_FALSE;
    }
    return fillSensor(env, sensor, kSensors[index]) ? JNI_TRUE : JNI_FALSE;
}

jboolean nativeGetDefaultDeviceSensorAtIndex(JNIEnv* env, jclass, jlong, jobject sensor,
        jint index) {
    return nativeGetSensorAtIndex(env, nullptr, 0, sensor, index);
}

void nativeGetDynamicSensors(JNIEnv*, jclass, jlong, jobject) {}

void nativeGetRuntimeSensors(JNIEnv*, jclass, jlong, jint, jobject) {}

jboolean nativeIsDataInjectionEnabled(JNIEnv*, jclass, jlong) {
    return JNI_FALSE;
}

jboolean nativeIsReplayDataInjectionEnabled(JNIEnv*, jclass, jlong) {
    return JNI_FALSE;
}

jboolean nativeIsHalBypassReplayDataInjectionEnabled(JNIEnv*, jclass, jlong) {
    return JNI_FALSE;
}

jint nativeCreateDirectChannel(JNIEnv*, jclass, jlong, jint, jlong, jint, jint, jobject) {
    return -1;
}

void nativeDestroyDirectChannel(JNIEnv*, jclass, jlong, jint) {}

jint nativeConfigDirectChannel(JNIEnv*, jclass, jlong, jint, jint, jint) {
    return -1;
}

jint nativeSetOperationParameter(JNIEnv*, jclass, jlong, jint, jint, jfloatArray, jintArray) {
    return -1;
}

void nativeRegisterClientListener(JNIEnv*, jclass, jlong, jobject) {}

/* ------------------------------------------------------------------------- */
/* SystemSensorManager.BaseEventQueue                                         */
/* ------------------------------------------------------------------------- */

jlong nativeInitBaseEventQueue(JNIEnv* env, jclass, jlong, jobject eventQWeak, jobject,
        jstring, jint, jstring, jstring) {
    jclass weakClass = env->GetObjectClass(eventQWeak);
    jmethodID get = env->GetMethodID(weakClass, "get", "()Ljava/lang/Object;");
    jobject queue = env->CallObjectMethod(eventQWeak, get);
    env->DeleteLocalRef(weakClass);
    if (queue == nullptr) {
        env->ExceptionClear();
        return 0;
    }

    jclass looperClass = env->FindClass("android/os/Looper");
    jmethodID myLooper = env->GetStaticMethodID(looperClass, "myLooper",
            "()Landroid/os/Looper;");
    jobject looper = env->CallStaticObjectMethod(looperClass, myLooper);
    if (looper == nullptr) {
        jmethodID getMainLooper = env->GetStaticMethodID(looperClass, "getMainLooper",
                "()Landroid/os/Looper;");
        looper = env->CallStaticObjectMethod(looperClass, getMainLooper);
    }

    jclass handlerClass = env->FindClass("android/os/Handler");
    jmethodID handlerCtor = env->GetMethodID(handlerClass, "<init>",
            "(Landroid/os/Looper;)V");
    jobject handler = env->NewObject(handlerClass, handlerCtor, looper);

    jclass tickerClass = env->FindClass("android/hardware/DroidletSensorTicker");
    jmethodID tickerCtor = env->GetMethodID(tickerClass, "<init>", "(J)V");

    auto* state = new VirtualEventQueue();
    jobject ticker = env->NewObject(tickerClass, tickerCtor,
            reinterpret_cast<jlong>(state));

    jclass baseClass = env->FindClass("android/hardware/SystemSensorManager$BaseEventQueue");
    state->queue = env->NewGlobalRef(queue);
    state->handler = env->NewGlobalRef(handler);
    state->ticker = env->NewGlobalRef(ticker);
    state->postDelayed = env->GetMethodID(handlerClass, "postDelayed",
            "(Ljava/lang/Runnable;J)Z");
    state->removeCallbacks = env->GetMethodID(handlerClass, "removeCallbacks",
            "(Ljava/lang/Runnable;)V");
    state->dispatchSensorEvent = env->GetMethodID(baseClass, "dispatchSensorEvent",
            "(I[FIJ)V");
    state->dispatchFlushComplete = env->GetMethodID(baseClass,
            "dispatchFlushCompleteEvent", "(I)V");

    env->DeleteLocalRef(looperClass);
    env->DeleteLocalRef(handlerClass);
    env->DeleteLocalRef(tickerClass);
    env->DeleteLocalRef(baseClass);
    env->DeleteLocalRef(looper);
    env->DeleteLocalRef(handler);
    env->DeleteLocalRef(ticker);
    env->DeleteLocalRef(queue);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        destroyQueue(env, state);
        return 0;
    }
    return reinterpret_cast<jlong>(state);
}

jint nativeEnableSensor(JNIEnv* env, jclass, jlong token, jint handle, jint rateUs,
        jint) {
    auto* state = reinterpret_cast<VirtualEventQueue*>(token);
    if (state == nullptr || state->destroyed) {
        return -1;
    }
    jint delayMs = std::max<jint>(10, rateUs / 1000);
    state->sensors[handle] = delayMs;
    state->minDelayMs = delayMs;
    for (const auto& entry : state->sensors) {
        state->minDelayMs = std::min(state->minDelayMs, entry.second);
    }
    if (!state->scheduled) {
        state->scheduled = true;
        env->CallBooleanMethod(state->handler, state->postDelayed, state->ticker,
                state->minDelayMs);
    }
    return 0;
}

jint nativeDisableSensor(JNIEnv* env, jclass, jlong token, jint handle) {
    auto* state = reinterpret_cast<VirtualEventQueue*>(token);
    if (state == nullptr || state->destroyed) {
        return -1;
    }
    state->sensors.erase(handle);
    if (state->sensors.empty()) {
        state->scheduled = false;
        env->CallVoidMethod(state->handler, state->removeCallbacks, state->ticker);
    }
    return 0;
}

void nativeDestroySensorEventQueue(JNIEnv* env, jclass, jlong token) {
    destroyQueue(env, reinterpret_cast<VirtualEventQueue*>(token));
}

jint nativeFlushSensor(JNIEnv* env, jclass, jlong token) {
    auto* state = reinterpret_cast<VirtualEventQueue*>(token);
    if (state == nullptr || state->destroyed) {
        return -1;
    }
    for (const auto& entry : state->sensors) {
        env->CallVoidMethod(state->queue, state->dispatchFlushComplete, entry.first);
    }
    return env->ExceptionCheck() ? -1 : 0;
}

jint nativeInjectSensorData(JNIEnv*, jclass, jlong, jint, jfloatArray, jint, jlong) {
    return -1;
}

void nativeTick(JNIEnv* env, jclass, jlong token) {
    auto* state = reinterpret_cast<VirtualEventQueue*>(token);
    if (state == nullptr || state->destroyed) {
        return;
    }
    state->inTick = true;
    state->scheduled = false;
    bool failed = false;

    // The listener may unregister (and delete the queue) from onSensorChanged,
    // so iterate over a snapshot and re-check after every callback.
    std::vector<std::pair<jint, jint>> pending(state->sensors.begin(), state->sensors.end());
    for (const auto& entry : pending) {
        if (state->destroyed) {
            break;
        }
        const VirtualSensor* sensor = findSensorByHandle(entry.first);
        if (sensor == nullptr) {
            continue;
        }
        float values[5] = {};
        int count = valuesFor(sensor->type, values);
        jfloatArray array = env->NewFloatArray(count);
        env->SetFloatArrayRegion(array, 0, count, values);
        env->CallVoidMethod(state->queue, state->dispatchSensorEvent, entry.first, array, 3,
                nowNanos());
        env->DeleteLocalRef(array);
        if (env->ExceptionCheck()) {
            // Leave the exception pending for the Java Handler to report, and
            // do not make any further JNI calls in this tick.
            failed = true;
            break;
        }
    }

    state->inTick = false;
    if (!failed && !state->destroyed && !state->sensors.empty()) {
        state->scheduled = true;
        env->CallBooleanMethod(state->handler, state->postDelayed, state->ticker,
                state->minDelayMs);
    }
    if (state->deletePending) {
        delete state;
    }
}

const JNINativeMethod gSystemSensorManagerMethods[] = {
        {"nativeClassInit", "()V", reinterpret_cast<void*>(nativeClassInit)},
        {"nativeCreate", "(Ljava/lang/String;)J", reinterpret_cast<void*>(nativeCreate)},
        {"nativeRegisterClientListener", "(JLandroid/os/IBinder;)V",
                reinterpret_cast<void*>(nativeRegisterClientListener)},
        {"nativeGetSensorAtIndex", "(JLandroid/hardware/Sensor;I)Z",
                reinterpret_cast<void*>(nativeGetSensorAtIndex)},
        {"nativeGetDefaultDeviceSensorAtIndex", "(JLandroid/hardware/Sensor;I)Z",
                reinterpret_cast<void*>(nativeGetDefaultDeviceSensorAtIndex)},
        {"nativeGetDynamicSensors", "(JLjava/util/List;)V",
                reinterpret_cast<void*>(nativeGetDynamicSensors)},
        {"nativeGetRuntimeSensors", "(JILjava/util/List;)V",
                reinterpret_cast<void*>(nativeGetRuntimeSensors)},
        {"nativeIsDataInjectionEnabled", "(J)Z",
                reinterpret_cast<void*>(nativeIsDataInjectionEnabled)},
        {"nativeIsReplayDataInjectionEnabled", "(J)Z",
                reinterpret_cast<void*>(nativeIsReplayDataInjectionEnabled)},
        {"nativeIsHalBypassReplayDataInjectionEnabled", "(J)Z",
                reinterpret_cast<void*>(nativeIsHalBypassReplayDataInjectionEnabled)},
        {"nativeCreateDirectChannel", "(JIJIILandroid/hardware/HardwareBuffer;)I",
                reinterpret_cast<void*>(nativeCreateDirectChannel)},
        {"nativeDestroyDirectChannel", "(JI)V",
                reinterpret_cast<void*>(nativeDestroyDirectChannel)},
        {"nativeConfigDirectChannel", "(JIII)I",
                reinterpret_cast<void*>(nativeConfigDirectChannel)},
        {"nativeSetOperationParameter", "(JII[F[I)I",
                reinterpret_cast<void*>(nativeSetOperationParameter)},
};

const JNINativeMethod gBaseEventQueueMethods[] = {
        {"nativeInitBaseEventQueue",
                "(JLjava/lang/ref/WeakReference;Landroid/os/MessageQueue;Ljava/lang/String;"
                "ILjava/lang/String;Ljava/lang/String;)J",
                reinterpret_cast<void*>(nativeInitBaseEventQueue)},
        {"nativeEnableSensor", "(JIII)I", reinterpret_cast<void*>(nativeEnableSensor)},
        {"nativeDisableSensor", "(JI)I", reinterpret_cast<void*>(nativeDisableSensor)},
        {"nativeDestroySensorEventQueue", "(J)V",
                reinterpret_cast<void*>(nativeDestroySensorEventQueue)},
        {"nativeFlushSensor", "(J)I", reinterpret_cast<void*>(nativeFlushSensor)},
        {"nativeInjectSensorData", "(JI[FIJ)I", reinterpret_cast<void*>(nativeInjectSensorData)},
};

const JNINativeMethod gTickerMethods[] = {
        {"nativeTick", "(J)V", reinterpret_cast<void*>(nativeTick)},
};

template <size_t N>
bool registerMethods(JNIEnv* env, const char* className, const JNINativeMethod (&methods)[N]) {
    jclass clazz = env->FindClass(className);
    if (clazz == nullptr) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }
    jint result = env->RegisterNatives(clazz, methods, static_cast<jint>(N));
    env->DeleteLocalRef(clazz);
    return result == JNI_OK;
}

}  // namespace

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (!registerMethods(env, "android/hardware/SystemSensorManager",
                         gSystemSensorManagerMethods)) {
        return JNI_ERR;
    }
    if (!registerMethods(env, "android/hardware/SystemSensorManager$BaseEventQueue",
                         gBaseEventQueueMethods)) {
        return JNI_ERR;
    }
    if (!registerMethods(env, "android/hardware/DroidletSensorTicker", gTickerMethods)) {
        return JNI_ERR;
    }
    if (virtualSensorsEnabled()) {
        ALOGI("virtual sensors enabled");
    }
    return JNI_VERSION_1_6;
}
