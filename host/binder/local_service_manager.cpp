#include "BackendUnifiedServiceManager.h"

#include <binder/RpcSession.h>
#include <binder/Status.h>
#include <android/os/IServiceCallback.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace android {

using binder::Status;

bool BackendUnifiedServiceManager::findLocalService(const std::string& name,
                                                   sp<IBinder>* binder) const {
    std::lock_guard<std::mutex> lock(mLocalMutex);
    auto it = mLocalServices.find(name);
    if (it == mLocalServices.end()) {
        return false;
    }
    *binder = it->second.binder;
    return true;
}

Status BackendUnifiedServiceManager::addLocalService(const std::string& name,
                                                     const sp<IBinder>& service,
                                                     bool allowIsolated, int32_t dumpPriority) {
    if (name.empty() || service == nullptr) {
        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
    }

    std::vector<sp<os::IServiceCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(mLocalMutex);
        mLocalServices[name] = LocalService{service, allowIsolated, dumpPriority};
        auto it = mLocalCallbacks.find(name);
        if (it != mLocalCallbacks.end()) {
            callbacks = it->second;
        }
    }

    // Notify outside the lock: callbacks may call back into the service
    // manager (and eventuate in addService/getService).
    for (const sp<os::IServiceCallback>& callback : callbacks) {
        if (callback != nullptr) {
            callback->onRegistration(name, service);
        }
    }
    return Status::ok();
}
}  // namespace android
