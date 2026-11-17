/*
 * Same-app binder-RPC service host.
 *
 * droidlet has no binder driver and no servicemanager, but libbinder includes
 * binder RPC over a Unix domain socket.  A service declared with
 * android:process (or android:isolatedProcess) runs in a child droidlet
 * process; the two processes talk over a nameless socketpair:
 *
 *   parent: socketpair -> exec child -> RpcSession::getRootObject()
 *   child:  --service-host ... --rpc-fd N -> Service.onBind() -> RpcServer
 *
 * Only the launched app's own components use this path.  There is no service
 * manager registration, no cross-app lookup and no kernel binder.
 */

#include <jni.h>

#include <android_util_Binder.h>
#include <binder/IBinder.h>
#include <binder/Parcel.h>
#include <binder/RpcServer.h>
#include <binder/RpcSession.h>
#include <binder/unique_fd.h>
#include <log/log.h>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern char** environ;

namespace {

using android::IBinder;
using android::RpcServer;
using android::RpcSession;
using android::sp;
using android::status_t;
using android::String8;

struct ServiceHost {
    pid_t pid = -1;
    sp<RpcSession> session;
    sp<IBinder> binder;
};

// Keyed by the native binder pointer behind the Java BinderProxy we hand to
// the app.  ibinderForJavaObject() returns that same pointer.
std::mutex gHostLock;
std::unordered_map<IBinder*, std::shared_ptr<ServiceHost>> gHosts;

// Child side: the RpcServer must stay alive for the process lifetime.
std::mutex gServerLock;
sp<RpcServer> gServer;

// Private transaction code used to ask the child host to stop its service
// gracefully (unbind + stop) before the parent tears down the session.
constexpr uint32_t kServiceHostStopTransaction = 0x7fffff01;

std::string jstringToString(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return {};
    }
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

std::string executablePath() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return {};
    }
    buf[len] = '\0';
    return buf;
}

std::string errnoString() {
    return std::string(strerror(errno));
}

bool childAlive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);
    // 0 means still running; -1 with ECHILD means it was already reaped.
    return result == 0;
}

void terminateHost(const std::shared_ptr<ServiceHost>& host) {
    if (host == nullptr) {
        return;
    }
    if (host->binder != nullptr) {
        // Ask the child to run Service.onUnbind/onDestroy before we drop the
        // session.  A dead child just returns DEAD_OBJECT here.
        android::Parcel data;
        data.markForBinder(host->binder);
        (void)host->binder->transact(kServiceHostStopTransaction, data, nullptr,
                                     android::IBinder::FLAG_ONEWAY);
        if (host->pid > 0) {
            int status = 0;
            for (int i = 0; i < 40; ++i) {
                pid_t result = waitpid(host->pid, &status, WNOHANG);
                if (result == host->pid || (result < 0 && errno == ECHILD)) {
                    host->pid = -1;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
    }
    if (host->session != nullptr) {
        (void)host->session->shutdownAndWait(false);
        host->session.clear();
    }
    if (host->pid > 0) {
        kill(host->pid, SIGTERM);
        int status = 0;
        for (int i = 0; i < 40; ++i) {
            pid_t result = waitpid(host->pid, &status, WNOHANG);
            if (result == host->pid || (result < 0 && errno == ECHILD)) {
                host->pid = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (host->pid > 0) {
            kill(host->pid, SIGKILL);
            waitpid(host->pid, &status, 0);
            host->pid = -1;
        }
    }
    host->binder.clear();
}

}  // namespace

extern "C" {

/*
 * Starts a child droidlet process for a same-app service and returns the
 * service's binder through binder RPC.  The caller runs this off the app main
 * thread; startup pays full runtime initialization.
 */
JNIEXPORT jobject JNICALL
Java_android_app_DroidletLauncher_nativeStartServiceHost(JNIEnv* env, jclass, jstring apk,
                                                         jstring data_dir, jstring component) {
    const std::string apk_path = jstringToString(env, apk);
    const std::string data = jstringToString(env, data_dir);
    const std::string component_name = jstringToString(env, component);
    if (apk_path.empty() || component_name.empty()) {
        ALOGE("service host: missing APK or component");
        return nullptr;
    }

    const std::string exe = executablePath();
    if (exe.empty()) {
        ALOGE("service host: cannot resolve /proc/self/exe");
        return nullptr;
    }

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
        ALOGE("service host: socketpair failed: %s", errnoString().c_str());
        return nullptr;
    }

    // The child gets the RPC end on a stable descriptor.  dup2 clears
    // FD_CLOEXEC, so the descriptor survives exec.
    const int child_fd = 3;
    std::vector<std::string> argv_strings = {
            exe,
            "--service-host",
            apk_path,
            component_name,
            "--rpc-fd",
            std::to_string(child_fd),
    };
    if (!data.empty()) {
        argv_strings.emplace_back("--data");
        argv_strings.emplace_back(data);
    }
    std::vector<char*> argv;
    argv.reserve(argv_strings.size() + 1);
    for (std::string& value : argv_strings) {
        argv.push_back(const_cast<char*>(value.c_str()));
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // Keep the action order safe when socketpair() happened to allocate the
    // target descriptor: dup2 replaces it first, then unrelated ends close.
    if (sockets[0] != child_fd) {
        posix_spawn_file_actions_addclose(&actions, sockets[0]);
    }
    posix_spawn_file_actions_adddup2(&actions, sockets[1], child_fd);
    if (sockets[1] != child_fd) {
        posix_spawn_file_actions_addclose(&actions, sockets[1]);
    } else {
        // dup2(fd, fd) does not clear FD_CLOEXEC.
        fcntl(sockets[1], F_SETFD, 0);
    }

    pid_t pid = -1;
    int spawn_status = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(sockets[1]);
    if (spawn_status != 0) {
        ALOGE("service host: posix_spawn failed: %s", strerror(spawn_status));
        close(sockets[0]);
        return nullptr;
    }

    auto host = std::make_shared<ServiceHost>();
    host->pid = pid;
    host->session = RpcSession::make();
    host->session->setFileDescriptorTransportMode(
            RpcSession::FileDescriptorTransportMode::UNIX);
    // ServiceConnection.linkToDeath() requires at least one incoming thread
    // on the client session, even when the interface itself never calls back.
    host->session->setMaxIncomingThreads(1);

    ::android::binder::unique_fd client_fd(sockets[0]);
    status_t status = host->session->setupUnixDomainSocketBootstrapClient(
            std::move(client_fd));
    if (status != android::OK) {
        ALOGE("service host: bootstrap client failed: %s", strerror(-status));
        terminateHost(host);
        return nullptr;
    }

    // The child creates and binds the Service before it starts the RpcServer,
    // so the root object is normally available on the first query.  Retry to
    // cover the exec/startup window and stop early if the child dies.
    sp<IBinder> root;
    for (int attempt = 0; attempt < 200; ++attempt) {
        root = host->session->getRootObject();
        if (root != nullptr) {
            break;
        }
        if (!childAlive(pid)) {
            ALOGE("service host: child exited before publishing its binder");
            terminateHost(host);
            return nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (root == nullptr) {
        ALOGE("service host: timed out waiting for %s", component_name.c_str());
        terminateHost(host);
        return nullptr;
    }

    host->binder = root;
    {
        std::lock_guard<std::mutex> lock(gHostLock);
        gHosts[root.get()] = host;
    }
    ALOGW("service host: %s ready pid=%d", component_name.c_str(), pid);
    return javaObjectForIBinder(env, root);
}

/*
 * Child side: publish the Service.onBind() binder as the RPC root and start
 * the server on the socketpair end supplied by the parent.
 */
JNIEXPORT jboolean JNICALL
Java_android_app_DroidletLauncher_nativeServiceHostSetRoot(JNIEnv* env, jclass, jobject binder,
                                                           jint fd) {
    sp<IBinder> root = android::ibinderForJavaObject(env, binder);
    if (root == nullptr) {
        ALOGE("service host: onBind returned no binder");
        return JNI_FALSE;
    }
    if (fd < 0) {
        ALOGE("service host: missing RPC file descriptor");
        return JNI_FALSE;
    }

    sp<RpcServer> server = RpcServer::make();
    server->setSupportedFileDescriptorTransportModes(
            {RpcSession::FileDescriptorTransportMode::UNIX});
    server->setMaxThreads(1);

    status_t status = server->setupUnixDomainSocketBootstrapServer(
            ::android::binder::unique_fd(static_cast<int>(fd)));
    if (status != android::OK) {
        ALOGE("service host: bootstrap server failed: %s", strerror(-status));
        return JNI_FALSE;
    }
    server->setRootObject(root);
    server->start();

    {
        std::lock_guard<std::mutex> lock(gServerLock);
        gServer = server;
    }
    ALOGW("service host: RPC server started");
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_android_app_DroidletLauncher_nativeIsServiceHostAlive(JNIEnv* env, jclass, jobject binder) {
    sp<IBinder> native = android::ibinderForJavaObject(env, binder);
    if (native == nullptr) {
        return JNI_FALSE;
    }
    std::lock_guard<std::mutex> lock(gHostLock);
    auto it = gHosts.find(native.get());
    if (it == gHosts.end() || it->second == nullptr) {
        return JNI_FALSE;
    }
    return childAlive(it->second->pid) ? JNI_TRUE : JNI_FALSE;
}

/*
 * Parent side: stop the child process and tear down the RPC session.
 */
JNIEXPORT void JNICALL
Java_android_app_DroidletLauncher_nativeStopServiceHost(JNIEnv* env, jclass, jobject binder) {
    sp<IBinder> native = android::ibinderForJavaObject(env, binder);
    if (native == nullptr) {
        return;
    }

    std::shared_ptr<ServiceHost> host;
    {
        std::lock_guard<std::mutex> lock(gHostLock);
        auto it = gHosts.find(native.get());
        if (it != gHosts.end()) {
            host = it->second;
            gHosts.erase(it);
        }
    }
    if (host == nullptr) {
        return;
    }
    ALOGW("service host: stopping pid=%d", host->pid);
    terminateHost(host);
}

}  // extern "C"
