/*
 * Minimal application launcher modelled after the java command.
 *
 *   droidlet [java-options] [--launcher <class>] [--activity <component>]
 *            [--data <dir>] [--display-id <id>] [--screenshot <png>]
 *            <apk> [args...]
 *
 * The APK is passed to the launcher class, which reads its manifest, installs
 * it under $ANDROID_DATA, and starts the manifest's launcher activity.  The
 * default launcher is android.app.DroidletLauncher.
 *
 * Before the runtime starts, system properties are merged from
 * $PREFIX/etc/droidlet.props and the HOST_PROPERTIES environment variable
 * (HOST_PROPERTIES wins) and written to the host property store.  The merged
 * value is also put back into HOST_PROPERTIES for child processes.
 */

#include <cstdio>
#define LOG_TAG "droidlet"

#include <dirent.h>
#include <limits.h>
#include <unistd.h>

#include <android_runtime/AndroidRuntime.h>

#include <android-base/properties.h>

#include <binder/Binder.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <utils/Log.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include "host_composer.h"

#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace android {

static void usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s [java-options] [--launcher <class>] [--activity <package/class>]\n"
            "       [--data <dir>] [--display-id <id>] [--size <WxH>] [--dpi <n>]\n"
            "       [--screenshot <png>]\n"
            "       [--input-script <file>] [--input-events <spec>]\n"
            "       [--exit-after-frame <n>] [--exit-after-script]\n"
            "       [--dump-frame <path>] [--dump-layers] [--fps] [--trace-frames]\n"
            "       <apk> [args...]\n"
            "\n"
            "Internal service-host mode (used for android:process services):\n"
            "       %s --service-host <apk> <package/class> --rpc-fd <fd> [--data <dir>]\n",
            argv0, argv0);
}

class AppRuntime : public AndroidRuntime {
  public:
    AppRuntime(char* argBlockStart, size_t argBlockLength)
        : AndroidRuntime(argBlockStart, argBlockLength), mClass(nullptr) {}

    void setClassNameAndArgs(const String8& className, int argc, char* const* argv) {
        mClassName = className;
        for (int i = 0; i < argc; ++i) {
            mArgs.add(String8(argv[i]));
        }
    }

    void onVmCreated(JNIEnv* env) override {
        char* slashClassName = toSlashClassName(mClassName.c_str());
        mClass = env->FindClass(slashClassName);
        if (mClass == nullptr) {
            ALOGE("ERROR: could not find class '%s'", mClassName.c_str());
        }
        free(slashClassName);

        if (mClass != nullptr) {
            mClass = reinterpret_cast<jclass>(env->NewGlobalRef(mClass));
        }
    }

    void onStarted() override {
        AndroidRuntime::getRuntime()->callMain(mClassName, mClass, mArgs);

        IPCThreadState::self()->stopProcess();
    }

  private:
    String8 mClassName;
    Vector<String8> mArgs;
    jclass mClass;
};

}  // namespace android

namespace {

using PropList = std::vector<std::pair<std::string, std::string>>;

std::string trim(std::string s) {
    const char* whitespace = " \t\r\n";
    size_t first = s.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return {};
    }
    size_t last = s.find_last_not_of(whitespace);
    return s.substr(first, last - first + 1);
}

void set_prop(PropList& props, std::unordered_map<std::string, size_t>& index, std::string key,
              std::string value) {
    if (key.empty()) {
        return;
    }
    auto it = index.find(key);
    if (it == index.end()) {
        index.emplace(key, props.size());
        props.emplace_back(std::move(key), std::move(value));
        return;
    }
    props[it->second].second = std::move(value);
}

// Reads a simple key=value file.  Blank lines and lines starting with '#'
// are ignored.  Missing files are not an error.
void read_props_file(const std::string& path, PropList& props,
                     std::unordered_map<std::string, size_t>& index) {
    std::ifstream in(path);
    if (!in) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(std::move(line));
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t equal = line.find('=');
        if (equal == std::string::npos) {
            continue;
        }
        set_prop(props, index, trim(line.substr(0, equal)), trim(line.substr(equal + 1)));
    }
}

void read_host_properties(const char* value, PropList& props,
                          std::unordered_map<std::string, size_t>& index) {
    if (value == nullptr) {
        return;
    }

    std::string all(value);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(';', pos);
        if (end == std::string::npos) {
            end = all.size();
        }

        size_t equal = all.find('=', pos);
        if (equal != std::string::npos && equal < end) {
            set_prop(props, index, trim(all.substr(pos, equal - pos)),
                     trim(all.substr(equal + 1, end - equal - 1)));
        }
        pos = end + 1;
    }
}

std::string get_prefix(const char* argv0) {
    const char* prefix = std::getenv("PREFIX");
    if (prefix != nullptr && *prefix != '\0') {
        return prefix;
    }

    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string path;
    if (len > 0) {
        buf[len] = '\0';
        path = buf;
    } else if (argv0 != nullptr && *argv0 != '\0') {
        path = argv0;
    } else {
        return {};
    }

    // Strip the executable name and its "bin" directory.
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    path.erase(slash);

    slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    path.erase(slash);
    return path;
}

// Put the droidlet framework overlay ahead of the prebuilt framework.jar.
// Classes in the overlay have the same names as framework classes and must be
// found first.  The overlay is optional so an uninstalled build still works.
void prepend_framework_overlay(const char* argv0) {
    std::string prefix = get_prefix(argv0);
    if (prefix.empty()) {
        return;
    }

    std::string overlay = prefix + "/share/droidlet/framework-overlay.jar";
    if (access(overlay.c_str(), R_OK) != 0) {
        return;
    }

    const char* boot = std::getenv("BOOTCLASSPATH");
    if (boot == nullptr || *boot == '\0') {
        return;
    }

    std::string boot_classpath(boot);
    if (boot_classpath.compare(0, overlay.size(), overlay) == 0 &&
        (boot_classpath.size() == overlay.size() || boot_classpath[overlay.size()] == ':')) {
        return;
    }

    std::string merged = overlay + ':' + boot_classpath;
    setenv("BOOTCLASSPATH", merged.c_str(), 1);
}

// Must run before the runtime starts.  Some library constructors have already
// initialized libbase's host property store from HOST_PROPERTIES by the time
// main() runs, so the merged values are also written into the store directly.
void load_properties(const char* argv0) {
    PropList props;
    std::unordered_map<std::string, size_t> index;

    std::string prefix = get_prefix(argv0);
    if (!prefix.empty()) {
        read_props_file(prefix + "/etc/droidlet.prop", props, index);
    }
    read_host_properties(std::getenv("HOST_PROPERTIES"), props, index);

    std::string merged;
    for (const auto& [key, value] : props) {
        if (!merged.empty()) {
            merged += ';';
        }
        merged += key;
        merged += '=';
        merged += value;
    }

    if (merged.empty()) {
        return;
    }

    for (const auto& [key, value] : props) {
        ALOGV("key %s, value %s", key.c_str(), value.c_str());
        __system_property_set(key.c_str(), value.c_str());
    }
}

size_t compute_arg_block_size(int argc, char* const argv[]) {
    uintptr_t start = reinterpret_cast<uintptr_t>(argv[0]);
    uintptr_t end = reinterpret_cast<uintptr_t>(argv[argc - 1]);
    end += strlen(argv[argc - 1]) + 1;
    return end - start;
}

}  // namespace

namespace {

int envInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 16384) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

int count_open_fds() {
    DIR* dir = opendir("/proc/self/fd");
    if (dir == nullptr) {
        return -1;
    }
    int count = 0;
    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

int g_startup_fds = -1;

void log_exit_stats() {
    const int fds = count_open_fds();
    ALOGI("exit: open fds=%d startup=%d", fds, g_startup_fds);
    const char* fail = std::getenv("DROIDLET_FAIL_ON_FD_LEAK");
    if (fail != nullptr && fail[0] != '\0' && g_startup_fds >= 0) {
        const int allowance = envInt("DROIDLET_FD_LEAK_ALLOWANCE", 64);
        if (fds > g_startup_fds + allowance) {
            ALOGE("fd leak detected: startup=%d exit=%d allowance=%d", g_startup_fds, fds,
                  allowance);
            _exit(1);
        }
    }
}

}  // namespace

int main(int argc, char* const argv[]) {
    g_startup_fds = count_open_fds();
    std::atexit(log_exit_stats);
    load_properties(argv[0]);
    prepend_framework_overlay(argv[0]);

    const char* launcher = "android.app.DroidletLauncher";
    std::vector<std::string> launcher_args;

    bool service_host = false;
    std::string service_apk;
    std::string service_component;
    int service_rpc_fd = -1;

    android::AppRuntime runtime(argv[0], compute_arg_block_size(argc, argv));

    // Everything up to the APK is either a VM option or a droidlet option.
    const char* spaced_commands[] = {"-cp", "-classpath"};
    bool known_command = false;
    bool has_stack_size = false;
    bool screenshot_requested = false;
    int i = 1;
    for (; i < argc; ++i) {
        if (known_command) {
            runtime.addOption(strdup(argv[i]));
            known_command = false;
            continue;
        }

        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-help") == 0) {
            android::usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--launcher") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --launcher requires a class name.\n");
                android::usage(argv[0]);
                return 2;
            }
            launcher = argv[i];
            continue;
        }
        if (strcmp(argv[i], "--service-host") == 0) {
            if (i + 2 >= argc) {
                fprintf(stderr, "Error: --service-host requires an APK and a component.\n");
                android::usage(argv[0]);
                return 2;
            }
            service_apk = argv[++i];
            service_component = argv[++i];
            service_host = true;
            continue;
        }
        if (strcmp(argv[i], "--rpc-fd") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --rpc-fd requires a file descriptor.\n");
                android::usage(argv[0]);
                return 2;
            }
            char* end = nullptr;
            long parsed = std::strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || parsed < 0 || parsed > 4096) {
                fprintf(stderr, "Error: invalid --rpc-fd '%s'.\n", argv[i]);
                return 2;
            }
            service_rpc_fd = static_cast<int>(parsed);
            continue;
        }
        if (strcmp(argv[i], "--activity") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --activity requires a component name.\n");
                android::usage(argv[0]);
                return 2;
            }
            launcher_args.emplace_back("--activity");
            launcher_args.emplace_back(argv[i]);
            continue;
        }
        if (strcmp(argv[i], "--data") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --data requires a directory.\n");
                android::usage(argv[0]);
                return 2;
            }
            launcher_args.emplace_back("--data");
            launcher_args.emplace_back(argv[i]);
            continue;
        }
        if (strcmp(argv[i], "--display-id") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --display-id requires an id.\n");
                android::usage(argv[0]);
                return 2;
            }
            launcher_args.emplace_back("--display-id");
            launcher_args.emplace_back(argv[i]);
            continue;
        }
        if (strcmp(argv[i], "--size") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --size requires <width>x<height>.\n");
                android::usage(argv[0]);
                return 2;
            }
            int width = 0;
            int height = 0;
            char extra = 0;
            if (sscanf(argv[i], "%dx%d%c", &width, &height, &extra) != 2 || width <= 0 ||
                height <= 0 || width > 16384 || height > 16384) {
                fprintf(stderr, "Error: invalid --size '%s' (use <width>x<height>).\n",
                        argv[i]);
                return 2;
            }
            char value[64];
            snprintf(value, sizeof(value), "%d", width);
            setenv("DROIDLET_DISPLAY_WIDTH", value, 1);
            snprintf(value, sizeof(value), "%d", height);
            setenv("DROIDLET_DISPLAY_HEIGHT", value, 1);
            continue;
        }
        if (strcmp(argv[i], "--dpi") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --dpi requires a value.\n");
                android::usage(argv[0]);
                return 2;
            }
            char* end = nullptr;
            const long dpi = std::strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || dpi < 72 || dpi > 640) {
                fprintf(stderr, "Error: invalid --dpi '%s' (72..640).\n", argv[i]);
                return 2;
            }
            setenv("DROIDLET_DISPLAY_DPI", argv[i], 1);
            continue;
        }
        if (strcmp(argv[i], "--screenshot") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --screenshot requires a path.\n");
                android::usage(argv[0]);
                return 2;
            }
            launcher_args.emplace_back("--screenshot");
            launcher_args.emplace_back(argv[i]);
            screenshot_requested = true;
            continue;
        }
        if (strcmp(argv[i], "--no-screenshot") == 0) {
            launcher_args.emplace_back("--no-screenshot");
            continue;
        }
        if (strcmp(argv[i], "--input-script") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --input-script requires a file.\n");
                return 2;
            }
            setenv("DROIDLET_INPUT_SCRIPT", argv[i], 1);
            continue;
        }
        if (strcmp(argv[i], "--input-events") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --input-events requires a specification.\n");
                return 2;
            }
            setenv("DROIDLET_INPUT_EVENTS", argv[i], 1);
            continue;
        }
        if (strcmp(argv[i], "--exit-after-frame") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --exit-after-frame requires a frame count.\n");
                return 2;
            }
            setenv("DROIDLET_EXIT_AFTER_FRAME", argv[i], 1);
            continue;
        }
        if (strcmp(argv[i], "--exit-after-script") == 0) {
            setenv("DROIDLET_EXIT_AFTER_SCRIPT", "1", 1);
            continue;
        }
        if (strcmp(argv[i], "--dump-frame") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --dump-frame requires a path.\n");
                return 2;
            }
            setenv("DROIDLET_COMPOSER_DUMP", argv[i], 1);
            continue;
        }
        if (strcmp(argv[i], "--dump-layers") == 0) {
            setenv("DROIDLET_DUMP_LAYERS", "1", 1);
            continue;
        }
        if (strcmp(argv[i], "--fps") == 0) {
            setenv("DROIDLET_FPS", "1", 1);
            continue;
        }
        if (strcmp(argv[i], "--trace-frames") == 0) {
            setenv("DROIDLET_TRACE_FRAMES", "1", 1);
            continue;
        }
        if (strcmp(argv[i], "--") == 0) {
            ++i;
            break;
        }
        if (argv[i][0] != '-') {
            break;
        }
        if (strncmp(argv[i], "-Xss", 4) == 0) {
            has_stack_size = true;
        }

        for (const char* command : spaced_commands) {
            if (strcmp(argv[i], command) == 0) {
                known_command = true;
                break;
            }
        }
        runtime.addOption(strdup(argv[i]));
    }

    std::string apk;
    if (service_host) {
        if (service_apk.empty() || service_component.empty()) {
            fprintf(stderr, "Error: --service-host requires an APK and a component.\n");
            android::usage(argv[0]);
            return 2;
        }
        apk = service_apk;
        launcher_args.emplace_back("--service");
        launcher_args.emplace_back(service_component);
        if (service_rpc_fd >= 0) {
            launcher_args.emplace_back("--rpc-fd");
            launcher_args.emplace_back(std::to_string(service_rpc_fd));
        }
    } else {
        if (i >= argc) {
            fprintf(stderr, "Error: no APK supplied.\n");
            android::usage(argv[0]);
            return 2;
        }
        apk = argv[i++];
    }

    std::vector<std::string> args;
    args.emplace_back("--apk");
    args.emplace_back(apk);
    args.insert(args.end(), launcher_args.begin(), launcher_args.end());

    // Everything after the APK belongs to the application, like the arguments
    // after the main class of a java command.
    if (i < argc) {
        args.emplace_back("--");
        for (; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
    }

    // Screenshots and exit-after-resume captures read the CPU-composed frame.
    // Zero-copy surfaces are composited by the host compositor, so force the
    // readback path for those runs.  For a normal Wayland session, use the
    // shared subsurface path by default.
    const bool capture_requested = screenshot_requested ||
            std::getenv("DROIDLET_EXIT_AFTER_RESUME") != nullptr;
    const char* present_backend = std::getenv("DROIDLET_PRESENT");
    const bool wayland_presenter = present_backend == nullptr ||
            std::strcmp(present_backend, "auto") == 0 ||
            std::strcmp(present_backend, "wayland") == 0;
    if (capture_requested) {
        unsetenv("DROIDLET_EGL_SUBSURFACE");
        setenv("DROIDLET_SCREENSHOT_MODE", "1", 1);
    } else if (std::getenv("DROIDLET_EGL_SUBSURFACE") == nullptr &&
               std::getenv("DROIDLET_DISABLE_SUBSURFACE") == nullptr &&
               std::getenv("WAYLAND_DISPLAY") != nullptr && wayland_presenter) {
        setenv("DROIDLET_EGL_SUBSURFACE", "1", 1);
    }

    std::vector<char*> launcher_argv;
    launcher_argv.reserve(args.size());
    for (std::string& arg : args) {
        launcher_argv.push_back(const_cast<char*>(arg.c_str()));
    }

    runtime.setClassNameAndArgs(android::String8(launcher),
                                static_cast<int>(launcher_argv.size()), launcher_argv.data());

    // The interpreter and JNI bridges use noticeably more native stack per
    // Java frame than a compiled frame.  The 1 MB default overflows in
    // coroutine workers that call Room's runBlocking bridge (WorkManager 2.12
    // hits this during ForceStopRunnable), so raise the default unless the
    // caller supplied an explicit -Xss.
    if (!has_stack_size) {
        runtime.addOption(strdup("-Xss2m"));
    }

    android::Vector<android::String8> runtime_args;
    runtime_args.add(android::String8("application"));

    // droidlet has no servicemanager process or binder driver.  The
    // process-local BackendUnifiedServiceManager is both the C++
    // IServiceManager and the Java IServiceManager (the Java FFI bridge gets
    // it directly).  Publish it as binder handle 0 too, so
    // BinderInternal.getContextObject() and native defaultServiceManager()
    // resolve to the same local object.  Do not start the binder thread pool:
    // there is no driver for it to read from, and local transactions are
    // direct virtual calls on the caller thread.
    android::sp<android::IBinder> serviceManager =
            android::IInterface::asBinder(android::defaultServiceManager());
    if (android::sp<android::BBinder> local =
                android::sp<android::BBinder>::cast(serviceManager);
        local != nullptr) {
        android::IPCThreadState::self()->setTheContextObject(local);
        ALOGI("installed process-local service manager as binder context object");
    } else {
        ALOGE("process-local service manager is not a local BBinder");
    }

    // Bring up the in-process composer before the runtime starts, so that the
    // first SurfaceComposerClient finds SurfaceFlinger/SurfaceFlingerAIDL.
    // A service host has no activity, window or Surface, so it must not start
    // the composer (or claim a display).  It only needs the JNI bridge in
    // libdroidlet_presenter.
    bool composer_started = false;
    if (!service_host) {
        const int displayWidth = envInt("DROIDLET_DISPLAY_WIDTH", 1280);
        const int displayHeight = envInt("DROIDLET_DISPLAY_HEIGHT", 800);
        const char* presenterBackend = std::getenv("DROIDLET_PRESENT");
        if (!droidlet::startHostComposer(displayWidth, displayHeight, presenterBackend)) {
            // There is no rendering fallback: without the composer ViewRootImpl
            // cannot draw into a Surface and --screenshot has nothing to capture.
            ALOGE("unable to start the host composer (presenter=%s)",
                  presenterBackend ? presenterBackend : "auto");
            return 1;
        }
        ALOGI("host composer presenting %dx%d", displayWidth, displayHeight);
        composer_started = true;
    } else {
        // PR_SET_PDEATHSIG is tied to the parent *thread*, and the Java
        // service-host worker that spawned us exits as soon as it has the
        // binder, so use a process-parent watchdog instead.  The parent also
        // stops the child explicitly on unbind/stop.
        const pid_t parent_pid = getppid();
        std::thread([parent_pid] {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (getppid() != parent_pid) {
                    _exit(0);
                }
            }
        }).detach();
        ALOGI("service host: rpc fd=%d component=%s", service_rpc_fd,
              service_component.c_str());
    }

    runtime.start("com.android.internal.os.RuntimeInit", runtime_args, false);

    if (composer_started) {
        droidlet::stopHostComposer();
    }
    return 0;
}
