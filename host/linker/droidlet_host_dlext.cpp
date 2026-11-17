/*
 * Host stubs for the bionic dynamic-linker namespace API.
 *
 * GraphicsEnv only calls these when an updatable graphics driver or ANGLE is
 * explicitly enabled, which droidlet does not do.  The host libc has no
 * android_* namespace symbols, so provide inert definitions at link time.
 */

#include <bionic/dlext_namespaces.h>

extern "C" {

struct android_namespace_t* android_create_namespace(const char*, const char*, const char*,
                                                     uint64_t, const char*, android_namespace_t*) {
    return nullptr;
}

struct android_namespace_t* android_get_exported_namespace(const char*) {
    return nullptr;
}

bool android_link_namespaces(struct android_namespace_t*, struct android_namespace_t*,
                             const char*) {
    return false;
}

bool android_link_namespaces_all_libs(struct android_namespace_t*, struct android_namespace_t*) {
    return false;
}

}  // extern "C"
