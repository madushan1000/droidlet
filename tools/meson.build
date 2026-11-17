python = find_program('python3')

gen_aconfig_cpp_stubs = find_program('gen_aconfig_cpp_stubs.py', dirs: meson.current_source_dir())

gen_aconfig_java_stubs = find_program('gen_aconfig_java_stubs.py', dirs: meson.current_source_dir())


#aconfig = find_program(
#  'aconfig.sh',
#  dirs: [meson.project_source_root() / 'meson_tools'],
#)
#
#aconfig_java = find_program(
#  'aconfig_java.sh',
#  dirs: [meson.project_source_root() / 'meson_tools'],
#)

operator_srcs_gen = generator(
  python,
  capture: true,
  output: '@PLAINNAME@_operator_out.cc',
  arguments: [
    '@SOURCE_DIR@/art/tools/generate_operator_out.py',
    '@EXTRA_ARGS@',
    '@INPUT@',
  ],
)
