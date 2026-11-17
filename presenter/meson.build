# Host-side presenters for droidlet (Wayland preferred, X11 optional).

presenter_sources = [
  'presenter.cpp',
  'host_composer.cpp',
  'jni.cpp',
  'service_host.cpp',
  'input_injector.cpp',
  'input_keys.cpp',
  'perf_overlay.cpp',
]

presenter_deps = [
  libandroid_runtime_dep,
  libbase_dep,
  libbinder_dep,
  libcutils_dep,
  libgui_dep,
  libinput_dep,
  liblog_dep,
  libnativewindow_dep,
  libui_dep,
  libutils_dep,
  dependency('egl'),
  dependency('glesv2'),
]
presenter_cpp_args = ['-std=c++20']
presenter_generated = []

wayland_client_dep = dependency('wayland-client', required: false)
wayland_scanner = find_program('wayland-scanner', required: false)
wayland_protocols_dep = dependency('wayland-protocols', required: false)
xkbcommon_dep = dependency('xkbcommon', required: false)

if wayland_client_dep.found() and wayland_scanner.found() and wayland_protocols_dep.found()
  wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
  xdg_shell_xml = wayland_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'
  xdg_decoration_xml = wayland_protocols_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'

  xdg_shell_header = custom_target(
    'xdg-shell-client-protocol.h',
    output: 'xdg-shell-client-protocol.h',
    command: [wayland_scanner, 'client-header', xdg_shell_xml, '@OUTPUT@'],
  )
  xdg_shell_code = custom_target(
    'xdg-shell-protocol.c',
    output: 'xdg-shell-protocol.c',
    command: [wayland_scanner, 'private-code', xdg_shell_xml, '@OUTPUT@'],
  )

  xdg_decoration_header = custom_target(
    'xdg-decoration-unstable-v1-client-protocol.h',
    output: 'xdg-decoration-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', xdg_decoration_xml, '@OUTPUT@'],
  )
  xdg_decoration_code = custom_target(
    'xdg-decoration-unstable-v1-protocol.c',
    output: 'xdg-decoration-unstable-v1-protocol.c',
    command: [wayland_scanner, 'private-code', xdg_decoration_xml, '@OUTPUT@'],
  )

  presenter_generated += [
    xdg_shell_header,
    xdg_shell_code,
    xdg_decoration_header,
    xdg_decoration_code,
  ]
  presenter_sources += ['wayland_presenter.cpp']
  presenter_deps += [wayland_client_dep]
  presenter_cpp_args += ['-DDROIDLET_HAVE_WAYLAND']
  if xkbcommon_dep.found()
    presenter_deps += [xkbcommon_dep]
    presenter_cpp_args += ['-DDROIDLET_HAVE_XKBCOMMON']
  else
    message('xkbcommon not found; Wayland keyboard input falls back to evdev key codes')
  endif
else
  message('wayland-client/wayland-scanner/wayland-protocols not found; Wayland presenter disabled')
endif

x11_dep = dependency('x11', required: false)
if x11_dep.found()
  presenter_sources += ['x11_presenter.cpp']
  presenter_deps += [x11_dep]
  presenter_cpp_args += ['-DDROIDLET_HAVE_X11']
else
  message('libX11 not found; X11 presenter disabled')
endif

# The Java launcher loads this with an absolute path under $PREFIX, so it must
# live next to the other host libraries (lib/x86_64-linux-gnu on this build).
droidlet_presenter = shared_library(
  'droidlet_presenter',
  presenter_sources + presenter_generated,
  include_directories: [
    include_directories('.'),
    libsystem_includes,
    frameworks_native_includes,
    frameworks_native_libs_math_includes,
    frameworks_base_core_jni,
    include_directories('../../../frameworks/native/libs/arect/include'),
  ],
  cpp_args: presenter_cpp_args,
  dependencies: presenter_deps + [libnativehelper_dep, droidlet_wayland_dep],
  install: true,
)
