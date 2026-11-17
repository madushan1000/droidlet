fs = import('fs')
android_os_binder_flags = custom_target(
  'android_os_binder_flags',
  input: 'flags.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'android.os.binder.flags',
  ],
  capture: true,
  output: 'android_os_binder_flags.h',
)

aidl_android_os = custom_target(
  'aidl_android_os',
  input: [
    'aidl/android/os/ConnectionInfo.aidl',
    'aidl/android/os/IClientCallback.aidl',
    'aidl/android/os/IServiceCallback.aidl',
    'aidl/android/os/IServiceManager.aidl',
    'aidl/android/os/Service.aidl',
    'aidl/android/os/ServiceWithMetadata.aidl',
    'aidl/android/os/ServiceDebugInfo.aidl',
    'aidl/android/os/IAccessor.aidl',
  ],
  output: [
    'android/os/Service.h',
    'android/os/ServiceDebugInfo.cpp',
    'android/os/BpClientCallback.h',
    'android/os/BnConnectionInfo.h',
    'android/os/BnServiceWithMetadata.h',
    'android/os/IClientCallback.h',
    'android/os/IServiceCallback.cpp',
    'android/os/ConnectionInfo.h',
    'android/os/Service.cpp',
    'android/os/BpService.h',
    'android/os/IServiceManager.h',
    'android/os/BnService.h',
    'android/os/BpConnectionInfo.h',
    'android/os/BpServiceCallback.h',
    'android/os/ConnectionInfo.cpp',
    'android/os/BnServiceManager.h',
    'android/os/BnClientCallback.h',
    'android/os/BpServiceDebugInfo.h',
    'android/os/BpServiceWithMetadata.h',
    'android/os/IServiceCallback.h',
    'android/os/ServiceWithMetadata.h',
    'android/os/ServiceDebugInfo.h',
    'android/os/BpServiceManager.h',
    'android/os/BnServiceCallback.h',
    'android/os/BnServiceDebugInfo.h',
    'android/os/IClientCallback.cpp',
    'android/os/IServiceManager.cpp',
    'android/os/ServiceWithMetadata.cpp',
    'android/os/BnAccessor.h',
    'android/os/BpAccessor.h',
    'android/os/IAccessor.cpp',
    'android/os/IAccessor.h',
  ],
  command: [
    aidl,
    '--lang=cpp',
    '-I', fs.relative_to(meson.current_source_dir() / 'aidl', meson.project_build_root()),
    #'--stability=vintf',
    #'--structured',
    '-o', meson.current_build_dir(),
    '-h', meson.current_build_dir(),
    '@INPUT@',
  ],
)

libaidl_android_os = static_library(
  'aidl_android_os',
  aidl_android_os,
  include_directories: [
    'include',
    'ndk/include_cpp',
    'ndk/include_ndk',
    'ndk/include_platform',
  ],
  dependencies: [
    #libbinder_ndk_dep,
    libbase_dep,
    libutils_dep,
    libcutils_dep,
    liblog_dep,
  ],
)

libbinder_includes = include_directories(
  'include',
  '.',
  'aidl',
  'ndk/include_cpp',
  'ndk/include_ndk',
  'ndk/include_platform',
  'include_private',
)
# Extracted from the droidlet tree; see droidlet/host/binder/.
droidlet_host_local_service = static_library(
  'droidlet_host_local_service',
  '../../../../droidlet/host/binder/local_service_manager.cpp',
  include_directories: libbinder_includes,
  cpp_args: ['-std=c++20'],
  dependencies: [libbase_dep, libutils_dep, libcutils_dep, liblog_dep],
)

libbinder = shared_library(
  'binder',
  [
    'Binder.cpp',
    'BpBinder.cpp',
    'Debug.cpp',
    'FdTrigger.cpp',
    'IInterface.cpp',
    'IResultReceiver.cpp',
    'JavaBBinderBase.cpp',
    'Parcel.cpp',
    'ParcelFileDescriptor.cpp',
    'RecordedTransaction.cpp',
    'RpcSession.cpp',
    'RpcServer.cpp',
    'RpcState.cpp',
    'RpcTransportRaw.cpp',
    'Stability.cpp',
    'Status.cpp',
    'TextOutput.cpp',
    'Utils.cpp',
    'file.cpp',

    'IMemory.cpp',
    'IShellCallback.cpp',
    'LazyServiceRegistrar.cpp',
    'MemoryBase.cpp',
    'MemoryDealer.cpp',
    'MemoryHeapBase.cpp',
    'ParcelableHolder.cpp',
    'PersistableBundle.cpp',

    'OS_non_android_linux.cpp',
    'OS_unix_base.cpp',

    'BufferedTextOutput.cpp',
    'BackendUnifiedServiceManager.cpp',
    'IPCThreadState.cpp',
    'IServiceManager.cpp',
    'IServiceManagerFFI.cpp',
    'ProcessState.cpp',
    'Static.cpp',

    'IPermissionController.cpp',
    'PermissionCache.cpp',
    'PermissionController.cpp',

    'ServiceManagerHost.cpp',
    'UtilsHost.cpp',
    android_os_binder_flags,
  ],
  include_directories: [
    system_core_includes,
    libbinder_includes,
    bionic_kernel_includes,
    '../../../../libnativehelper/include_jni',
  ],
  cpp_args: [
    '-std=c++20',
    # droidlet links libandroid_runtime and uses binder RPC threads that need
    # to attach to the JVM (and detach on exit).  JvmUtils otherwise only
    # enables that path for Android targets.
    '-DDROIDLET_HOST_JVM',
    # droidlet has no binder kernel driver, but the kernel-wire Parcel and
    # AIDL plumbing is still needed for the process-local service manager:
    # Java BinderProxy.transact() and AIDL stubs serialize through Parcel even
    # when both ends are in this process.  Handle-based BpBinder paths are
    # never reached because handle 0 resolves to the local service manager and
    # all other services are local objects.
    '-DBINDER_WITH_KERNEL_IPC',
  ],
  link_with: [libaidl_android_os, droidlet_host_local_service],
  dependencies: [
    libbase_dep,
    libutils_dep,
    libcutils_dep,
    liblog_dep,
  ],
  install: true,
)
libbinder_dep = declare_dependency(
  include_directories: libbinder_includes,
  link_with: libbinder,
)
