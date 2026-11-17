#include <dlfcn.h>
#include <nativeloader/native_loader.h>

extern "C" {
void* bionic_dlsym(void* handle, const char* symbol);
}

namespace android {

void* FindNativeLibrarySymbol(void* handle, const char* symbol) {
#if defined(ART_TARGET_ANDROID)
  return dlsym(handle, symbol);
#else
  return bionic_dlsym(handle, symbol);
#endif
}

}  // namespace android
