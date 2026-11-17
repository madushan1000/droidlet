/*
 * Host NDK sensor stubs.
 *
 * droidlet has no sensor HAL or SensorManager service yet.  The NDK sensor
 * API is exposed as an empty manager/queue so apps can load and initialize
 * their sensor code instead of failing at dynamic-link time.
 *
 * This is intentionally a stub: no sensors are reported and no events are
 * produced.  The future Linux implementation should replace this file with a
 * real backend that reports host sensors through the same ABI.
 */

#include <android/sensor.h>

#include <cstddef>

struct ASensorManager {
    int unused;
};

struct ASensorEventQueue {
    int unused;
};

struct ASensor {
    int unused;
};

namespace {

ASensorManager gSensorManager = {};
ASensorEventQueue gEventQueue = {};

}  // namespace

ASensorManager* ASensorManager_getInstance() {
    return &gSensorManager;
}

ASensorManager* ASensorManager_getInstanceForPackage(const char* /* packageName */) {
    return &gSensorManager;
}

int ASensorManager_getSensorList(ASensorManager* manager, ASensorList* list) {
    if (manager == nullptr) {
        return -22;  // BAD_VALUE
    }
    if (list != nullptr) {
        *list = nullptr;
    }
    return 0;
}

ssize_t ASensorManager_getDynamicSensorList(ASensorManager* manager, ASensorList* list) {
    if (manager == nullptr) {
        return -22;  // BAD_VALUE
    }
    if (list != nullptr) {
        *list = nullptr;
    }
    return 0;
}

ASensor const* ASensorManager_getDefaultSensor(ASensorManager* /* manager */, int /* type */) {
    return nullptr;
}

ASensor const* ASensorManager_getDefaultSensorEx(ASensorManager* /* manager */, int /* type */,
                                                 bool /* wakeUp */) {
    return nullptr;
}

ASensorEventQueue* ASensorManager_createEventQueue(ASensorManager* manager, ALooper* /* looper */,
                                                   int /* ident */,
                                                   ALooper_callbackFunc /* callback */,
                                                   void* /* data */) {
    if (manager == nullptr) {
        return nullptr;
    }
    return &gEventQueue;
}

int ASensorManager_destroyEventQueue(ASensorManager* /* manager */, ASensorEventQueue* /* queue */) {
    return 0;
}

int ASensorManager_createSharedMemoryDirectChannel(ASensorManager* /* manager */, int /* fd */,
                                                   size_t /* size */) {
    return -1;
}

int ASensorManager_createHardwareBufferDirectChannel(ASensorManager* /* manager */,
                                                     AHardwareBuffer const* /* buffer */,
                                                     size_t /* size */) {
    return -1;
}

void ASensorManager_destroyDirectChannel(ASensorManager* /* manager */, int /* channelId */) {
}

int ASensorManager_configureDirectReport(ASensorManager* /* manager */, ASensor const* /* sensor */,
                                         int /* channelId */, int /* rate */) {
    return -1;
}

int ASensorEventQueue_registerSensor(ASensorEventQueue* /* queue */, ASensor const* /* sensor */,
                                     int32_t /* samplingPeriodUs */,
                                     int64_t /* maxBatchReportLatencyUs */) {
    return 0;
}

int ASensorEventQueue_enableSensor(ASensorEventQueue* /* queue */, ASensor const* /* sensor */) {
    return 0;
}

int ASensorEventQueue_disableSensor(ASensorEventQueue* /* queue */, ASensor const* /* sensor */) {
    return 0;
}

int ASensorEventQueue_setEventRate(ASensorEventQueue* /* queue */, ASensor const* /* sensor */,
                                   int32_t /* usec */) {
    return 0;
}

int ASensorEventQueue_hasEvents(ASensorEventQueue* /* queue */) {
    return 0;
}

ssize_t ASensorEventQueue_getEvents(ASensorEventQueue* /* queue */, ASensorEvent* /* events */,
                                    size_t /* count */) {
    return 0;
}

int ASensorEventQueue_requestAdditionalInfoEvents(ASensorEventQueue* /* queue */,
                                                  bool /* enable */) {
    return 0;
}

const char* ASensor_getName(ASensor const* /* sensor */) {
    return "";
}

const char* ASensor_getVendor(ASensor const* /* sensor */) {
    return "";
}

int ASensor_getType(ASensor const* /* sensor */) {
    return 0;
}

float ASensor_getResolution(ASensor const* /* sensor */) {
    return 0.0f;
}

int ASensor_getMinDelay(ASensor const* /* sensor */) {
    return 0;
}

int ASensor_getFifoMaxEventCount(ASensor const* /* sensor */) {
    return 0;
}

int ASensor_getFifoReservedEventCount(ASensor const* /* sensor */) {
    return 0;
}

const char* ASensor_getStringType(ASensor const* /* sensor */) {
    return "";
}

int ASensor_getReportingMode(ASensor const* /* sensor */) {
    return 0;
}

bool ASensor_isWakeUpSensor(ASensor const* /* sensor */) {
    return false;
}

bool ASensor_isDirectChannelTypeSupported(ASensor const* /* sensor */, int /* channelType */) {
    return false;
}

int ASensor_getHighestDirectReportRateLevel(ASensor const* /* sensor */) {
    return ASENSOR_DIRECT_RATE_STOP;
}

int ASensor_getHandle(ASensor const* /* sensor */) {
    return -1;
}
