// Host stubs for the incremental-install feature probes.
//
// droidlet installs APKs from ordinary local files and has no incremental
// filesystem or data loader, so the IncrementalManager JNI must report that
// the feature is unavailable.  This replaces libincfs for libandroid_runtime.

#include "incfs_ndk.h"

extern "C" {

bool IncFs_IsEnabled() {
    return false;
}

IncFsFeatures IncFs_Features() {
    return INCFS_FEATURE_NONE;
}

bool IncFs_IsIncFsFd(int /* fd */) {
    return false;
}

bool IncFs_IsIncFsPath(const char* /* path */) {
    return false;
}

IncFsErrorCode IncFs_UnsafeGetSignatureByPath(const char* /* path */, char* /* buffer */,
                                              size_t* /* buffer_size */) {
    return -1;
}

}  // extern "C"
