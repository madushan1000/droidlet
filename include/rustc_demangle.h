#ifndef RUST_DEMANGLE_H_
#define RUST_DEMANGLE_H_
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
char *rustc_demangle(const char *mangled, char *out, size_t *len, int *status);
extern "C" inline char* rustc_demangle(const char* mangled, char* out, size_t* len, int* status) {
  (void)mangled;
  (void)out;
  (void)len;
  if (status != nullptr) {
    *status = 0;
  }
  return nullptr;
}
#ifdef __cplusplus
}
#endif
#endif

