#!/usr/bin/env python3
#
# Build a legacy (framework-only, Java) AOSP sample as an APK without Gradle.
#
# Many samples under droidlet-samples/development predate Gradle and only have
# Android.bp/AndroidManifest.xml.  They are perfect droidlet fixtures precisely
# because they do not depend on AndroidX or Google Play services.  This script
# runs the same steps Gradle would: aapt2 (resources + R.java), javac against
# the platform android.jar, d8, zipalign and apksigner.
#
# Usage:
#   tools/build_legacy_apk.py --sample <dir> --output <dir> [options]
#
# The sample directory must contain AndroidManifest.xml and one or more of
# src/, res/, assets/.  Use --extra-src/--extra-res/--extra-package for library
# projects that were separate modules in the original build.

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile


def find_sdk():
    for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        value = os.environ.get(env)
        if value and os.path.isdir(value):
            return os.path.abspath(value)
    default = os.path.expanduser("~/tools/android-sdk")
    if os.path.isdir(default):
        return default
    sys.exit("error: Android SDK not found (set ANDROID_HOME)")


def version_key(path):
    name = os.path.basename(path)
    parts = []
    for piece in name.split("."):
        try:
            parts.append(int(piece))
        except ValueError:
            parts.append(0)
    return parts


def highest(directory):
    entries = [os.path.join(directory, e) for e in os.listdir(directory)
               if os.path.isdir(os.path.join(directory, e))]
    if not entries:
        sys.exit(f"error: {directory} is empty")
    return sorted(entries, key=version_key)[-1]


def find_platform(sdk, requested):
    platforms = os.path.join(sdk, "platforms")
    if not os.path.isdir(platforms):
        sys.exit(f"error: {platforms} does not exist; install an SDK platform")
    if requested:
        candidates = [p for p in glob.glob(os.path.join(platforms, requested + "*"))]
        if not candidates:
            sys.exit(f"error: no platform matching {requested} in {platforms}")
        return sorted(candidates, key=version_key)[-1]
    return highest(platforms)


def run(cmd, cwd=None, what=None):
    print("+", " ".join(cmd), flush=True)
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(f"error: {what or cmd[0]} failed with {result.returncode}")


def collect(roots, suffix):
    found = []
    for root in roots:
        for base, _dirs, files in os.walk(root):
            for name in files:
                if name.endswith(suffix):
                    found.append(os.path.join(base, name))
    return sorted(found)


def resource_dirs(roots):
    result = []
    for root in roots:
        path = os.path.join(root, "res")
        if os.path.isdir(path):
            result.append(path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample", required=True, help="sample project directory")
    parser.add_argument("--output", required=True, help="directory for the signed APK")
    parser.add_argument("--name", help="APK base name (default: sample directory)")
    parser.add_argument("--sdk", help="Android SDK root (default: ANDROID_HOME)")
    parser.add_argument("--platform", help="platform name, e.g. android-37.0")
    parser.add_argument("--min-api", type=int, default=21, help="d8 min-api (default 21)")
    parser.add_argument("--source", type=int, default=8, help="javac -source level")
    parser.add_argument("--target", type=int, default=8, help="javac -target level")
    parser.add_argument("--extra-src", action="append", default=[],
                        help="additional source root (library module)")
    parser.add_argument("--extra-res", action="append", default=[],
                        help="additional res directory (library module)")
    parser.add_argument("--assets", action="append", default=[],
                        help="additional assets directory (default: <sample>/assets)")
    parser.add_argument("--native-lib", action="append", default=[],
                        help="native library source[:destination]; default destination is "
                             "lib/x86_64/<basename>")
    parser.add_argument("--extra-package", action="append", default=[],
                        help="extra R package for a library module")
    parser.add_argument("--classpath", action="append", default=[],
                        help="additional jar for javac/d8")
    parser.add_argument("--dex-classpath", action="append", default=[],
                        help="additional jar to compile against and dex into the APK")
    parser.add_argument("--no-crunch", action="store_true",
                        help="pass --no-crunch to aapt2 compile")
    parser.add_argument("--keystore", default=os.path.expanduser("~/.android/debug.keystore"))
    parser.add_argument("--keystore-pass", default="android")
    parser.add_argument("--key-alias", default="androiddebugkey")
    parser.add_argument("--key-pass", default="android")
    args = parser.parse_args()

    sample = os.path.abspath(args.sample)
    manifest = os.path.join(sample, "AndroidManifest.xml")
    if not os.path.isfile(manifest):
        sys.exit(f"error: {manifest} not found")

    sdk = os.path.abspath(args.sdk) if args.sdk else find_sdk()
    platform = find_platform(sdk, args.platform)
    android_jar = os.path.join(platform, "android.jar")
    if not os.path.isfile(android_jar):
        sys.exit(f"error: {android_jar} not found")
    build_tools = highest(os.path.join(sdk, "build-tools"))
    aapt2 = os.path.join(build_tools, "aapt2")
    d8 = os.path.join(build_tools, "d8")
    zipalign = os.path.join(build_tools, "zipalign")
    apksigner = os.path.join(build_tools, "apksigner")
    for tool in (aapt2, d8, zipalign, apksigner):
        if not os.path.isfile(tool):
            sys.exit(f"error: {tool} not found")

    if not os.path.isfile(args.keystore):
        sys.exit(f"error: debug keystore {args.keystore} not found")

    src_roots = [os.path.join(sample, "src")] + [os.path.abspath(p) for p in args.extra_src]
    src_files = collect([r for r in src_roots if os.path.isdir(r)], ".java")
    if not src_files:
        sys.exit("error: no .java sources found")

    res_dirs = resource_dirs([sample]) + [os.path.abspath(p) for p in args.extra_res]
    for res in res_dirs:
        if not os.path.isdir(res):
            sys.exit(f"error: resource dir {res} not found")

    assets_dirs = []
    sample_assets = os.path.join(sample, "assets")
    if os.path.isdir(sample_assets):
        assets_dirs.append(sample_assets)
    assets_dirs.extend(os.path.abspath(p) for p in args.assets)
    for assets in assets_dirs:
        if not os.path.isdir(assets):
            sys.exit(f"error: assets dir {assets} not found")

    native_libs = []
    for spec in args.native_lib:
        if ":" in spec:
            src, dest = spec.rsplit(":", 1)
        else:
            src, dest = spec, "lib/x86_64/" + os.path.basename(spec)
        src = os.path.abspath(src)
        if not os.path.isfile(src):
            sys.exit(f"error: native library {src} not found")
        native_libs.append((src, dest))

    classpath = [android_jar] + [os.path.abspath(p) for p in args.classpath]
    dex_jars = [os.path.abspath(p) for p in args.dex_classpath]
    for jar in dex_jars:
        if not os.path.isfile(jar):
            sys.exit(f"error: dex classpath jar {jar} not found")
    classpath.extend(dex_jars)
    libs = glob.glob(os.path.join(sample, "libs", "*.jar"))
    classpath.extend(sorted(libs))

    output = os.path.abspath(args.output)
    os.makedirs(output, exist_ok=True)
    name = args.name or os.path.basename(sample)
    apk_name = name if name.endswith(".apk") else name + ".apk"

    with tempfile.TemporaryDirectory(prefix="droidlet-apk-") as work:
        compiled = []
        for index, res in enumerate(res_dirs):
            out = os.path.join(work, f"res-{index}.zip")
            compile_cmd = [aapt2, "compile", "--dir", res, "-o", out]
            if args.no_crunch:
                compile_cmd.insert(2, "--no-crunch")
            run(compile_cmd, what=f"aapt2 compile {res}")
            compiled.append(out)

        gen = os.path.join(work, "gen")
        os.makedirs(gen)
        linked = os.path.join(work, "linked.apk")
        link = [aapt2, "link", "-o", linked, "-I", android_jar,
                "--manifest", manifest, "--java", gen,
                # Without an explicit min SDK, aapt2 prunes framework XML
                # attributes from the compiled layout for apps whose manifest
                # omits uses-sdk or declares a very old one.  That produces
                # "You must supply a layout_width attribute" at runtime.
                "--min-sdk-version", str(args.min_api),
                "--auto-add-overlay"]
        for package in args.extra_package:
            link.extend(["--extra-packages", package])
        for assets in assets_dirs:
            link.extend(["-A", assets])
        link.extend(compiled)
        run(link, what="aapt2 link")

        classes = os.path.join(work, "classes")
        os.makedirs(classes)
        java_files = collect([gen], ".java") + src_files
        javac = ["javac", "-source", str(args.source), "-target", str(args.target),
                 "-Xlint:-options", "-encoding", "UTF-8",
                 "-bootclasspath", android_jar,
                 "-classpath", os.pathsep.join(classpath),
                 "-d", classes] + java_files
        run(javac, what="javac")

        class_files = collect([classes], ".class")
        if not class_files:
            sys.exit("error: javac produced no classes")
        dex_dir = os.path.join(work, "dex")
        os.makedirs(dex_dir)
        d8_cmd = [d8, "--release", "--min-api", str(args.min_api),
                  "--lib", android_jar, "--output", dex_dir]
        for extra in classpath[1:]:
            if extra in dex_jars:
                continue
            d8_cmd.extend(["--lib", extra])
        d8_cmd.extend(class_files)
        d8_cmd.extend(dex_jars)
        run(d8_cmd, what="d8")

        unsigned = os.path.join(work, "unsigned.apk")
        shutil.copyfile(linked, unsigned)
        with zipfile.ZipFile(unsigned, "a") as archive:
            archive.write(os.path.join(dex_dir, "classes.dex"), "classes.dex")
            for src, dest in native_libs:
                archive.write(src, dest)

        aligned = os.path.join(work, "aligned.apk")
        run([zipalign, "-f", "4", unsigned, aligned], what="zipalign")

        final = os.path.join(output, apk_name)
        run([apksigner, "sign",
             "--ks", args.keystore,
             "--ks-pass", f"pass:{args.keystore_pass}",
             "--key-pass", f"pass:{args.key_pass}",
             "--ks-key-alias", args.key_alias,
             "--v4-signing-enabled", "false",
             "--out", final, aligned], what="apksigner")

        # Sanity check: a launchable activity must be visible to droidlet.
        badging = subprocess.run([aapt2, "dump", "badging", final],
                                 capture_output=True, text=True)
        launchable = [line for line in badging.stdout.splitlines()
                      if line.startswith("launchable-activity")]
        if not launchable:
            print(f"warning: {apk_name} has no MAIN/LAUNCHER activity", file=sys.stderr)
        else:
            print(f"built {final}")
            for line in launchable:
                print("  " + line)
        print(f"  platform: {os.path.basename(platform)}")
        print(f"  size: {os.path.getsize(final)} bytes")


if __name__ == "__main__":
    main()
