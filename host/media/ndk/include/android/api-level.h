/* Minimal host shim for the Android NDK media headers. */
#pragma once

#ifndef __ANDROID_API__
#define __ANDROID_API__ 26
#endif

#ifndef __INTRODUCED_IN
#define __INTRODUCED_IN(api_level)
#endif

#ifndef __DEPRECATED_IN
#define __DEPRECATED_IN(api_level)
#endif
