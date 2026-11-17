libnativehelper_includes = include_directories('include_jni')
libnativehelper = shared_library(
    'nativehelper',
    [
        'DlHelp.c',
        'ExpandableString.c',
        'JNIHelp.c',
        'JNIPlatformHelp.c',
        'JniConstants.c',
        'JniInvocation.c',
        'file_descriptor_jni.c',
    ],
    include_directories: [
        'include',
        'include_jni',
        'include_platform',
        'include_platform_header_only',
    ],
    dependencies: [liblog_dep],
    install: true,
)

libnativehelper_dep = declare_dependency(
    link_with: libnativehelper,
    include_directories: [
        'include',
        'include_jni',
        'include_platform',
        'include_platform_header_only',
        'header_only_include',
    ],
)
