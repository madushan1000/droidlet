prebuilt_android_all_jar = files('android-all-17.jar')

framework_dex_only = custom_target(
  'framework-dec-only.jar',
  build_by_default: true,
  input: [prebuilt_android_all_jar],
  output: 'framework-dec-only.jar',
  command: [
    d8,
    '--min-api', '35',
    '--output', '@OUTPUT@',
    '@INPUT@',
  ],
)

framework_resources = custom_target(
  'framework-resources.jar',
  build_by_default: true,
  input: [prebuilt_android_all_jar],
  output: 'framework-resources.jar',
  command: [
    'sh',
    '-c', 'cp @INPUT@ @OUTPUT@ && zip -q -d @OUTPUT@ *.class res/* AndroidManifest.xml resources.arsc',
  ],
)

framework_hostdex = custom_target(
  'framework-hostdex.jar',
  build_by_default: true,
  input: [framework_dex_only, framework_resources],
  output: 'framework-hostdex.jar',
  command: [zipmerge, '@OUTPUT@', '@INPUT0@', '@INPUT1@'],
  install: true,
  install_dir: 'framework',
)

framework_res_apk = custom_target(
  'framework-res.apk',
  build_by_default: true,
  input: [prebuilt_android_all_jar],
  output: 'framework-res.apk',
  command: [
    'sh',
    '-c', 'unzip -q -o @INPUT@ res/**/** AndroidManifest.xml resources.arsc && zip -q -r @OUTPUT@ res/* AndroidManifest.xml resources.arsc',
  ],
  install: true,
  install_dir: 'framework',
)
