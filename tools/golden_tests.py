#!/usr/bin/env python3
#
# Headless golden-image smoke tests for droidlet fixtures.
#
# Runs a small set of stable APKs with the null presenter, captures the
# composed frame through --screenshot, and compares it with the checked-in
# image under droidlet/golden/.  Use --update to refresh the goldens.
#
# Requires ImageMagick's compare/identify (or ffmpeg for dimensions).

import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKSPACE = os.path.dirname(ROOT)
RUN_APK = os.path.join(WORKSPACE, "run_apk.sh")
APK_DIR = os.environ.get(
    "DROIDLET_TEST_APKS", os.path.join(WORKSPACE, "droidlet_test_apks"))
GOLDEN_DIR = os.path.join(ROOT, "golden")

CASES = [
    {"name": "HelloActivity", "apk": "HelloActivity.apk", "delay": 1000},
    {"name": "SkeletonApp", "apk": "SkeletonApp.apk", "delay": 1000},
    {"name": "FixedGridLayout", "apk": "FixedGridLayout.apk", "delay": 1000},
    {"name": "ClickCounter", "apk": "ClickCounter.apk", "delay": 1000},
    {"name": "ClickCounterTap", "apk": "ClickCounter.apk", "delay": 1500,
     "input": "300 tap 640 60"},
    {"name": "NotePad", "apk": "NotePad.apk", "delay": 1200},
    {"name": "DialogApp", "apk": "DialogApp.apk", "delay": 1500},
    {"name": "DialogChecked", "apk": "DialogApp.apk", "delay": 2500,
     "input": "1500 tap 640 438"},
    {"name": "PopupApp", "apk": "PopupApp.apk", "delay": 1500},
    {"name": "PopupChecked", "apk": "PopupApp.apk", "delay": 2500,
     "input": "1500 tap 640 405"},
    {"name": "MenuApp", "apk": "MenuApp.apk", "delay": 1500},
    {"name": "MenuChecked", "apk": "MenuApp.apk", "delay": 6000,
     "input": "1000 tap 1252 26;2200 tap 1180 63;3000 tap 1252 26;"
              "4200 tap 1200 110;5000 tap 1252 26"},
]


def find_tool(name):
    path = shutil.which(name)
    if path is None:
        sys.exit(f"error: required tool {name} not found")
    return path


def run_fixture(case, screenshot):
    env = dict(os.environ)
    env.update(
        {
            "DROIDLET_PRESENT": "null",
            "DROIDLET_EXIT_AFTER_RESUME": "1",
            "DROIDLET_SCREENSHOT_DELAY_MS": str(case.get("delay", 1000)),
            "DROIDLET_APK": os.path.join(APK_DIR, case["apk"]),
            "DROIDLET_LOG_TAGS": "droidlet:W *:S",
        }
    )
    if case.get("input"):
        env["DROIDLET_INPUT_EVENTS"] = case["input"]
    cmd = [RUN_APK, "--screenshot", screenshot]
    process = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               start_new_session=True)
    try:
        output, _ = process.communicate(timeout=45)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        output, _ = process.communicate()
        raise
    return subprocess.CompletedProcess(cmd, process.returncode, output)


def compare_images(expected, actual):
    compare = find_tool("compare")
    result = subprocess.run(
        [compare, "-metric", "RMSE", expected, actual, "null:"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    text = (result.stderr or result.stdout).strip()
    # ImageMagick prints e.g. "123.4 (0.00188)".
    normalized = None
    if "(" in text and ")" in text:
        try:
            normalized = float(text[text.rfind("(") + 1:text.rfind(")")])
        except ValueError:
            normalized = None
    if normalized is None:
        # Older ImageMagick prints just the absolute RMSE.
        try:
            normalized = float(text.split()[0])
        except (ValueError, IndexError):
            normalized = 1.0
    return normalized, text


def image_size(path):
    identify = shutil.which("identify")
    if identify:
        result = subprocess.run([identify, "-format", "%wx%h", path],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    return "?"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true",
                        help="write/replace golden images instead of comparing")
    parser.add_argument("--threshold", type=float, default=0.01,
                        help="maximum normalized RMSE (default 0.01)")
    parser.add_argument("--case", action="append", default=[],
                        help="run only the named case (repeatable)")
    parser.add_argument("--list", action="store_true", help="list cases and exit")
    args = parser.parse_args()

    if args.list:
        for case in CASES:
            print(case["name"])
        return 0

    os.makedirs(GOLDEN_DIR, exist_ok=True)
    selected = [case for case in CASES
                if not args.case or case["name"] in args.case]
    if not selected:
        sys.exit("error: no matching cases")

    failures = []
    with tempfile.TemporaryDirectory(prefix="droidlet-golden-") as work:
        for case in selected:
            apk = os.path.join(APK_DIR, case["apk"])
            golden = os.path.join(GOLDEN_DIR, case["name"] + ".png")
            actual = os.path.join(work, case["name"] + ".png")
            print(f"== {case['name']}")
            if not os.path.isfile(apk):
                failures.append((case["name"], f"missing APK {apk}"))
                print(f"  FAIL missing APK {apk}")
                continue
            result = run_fixture(case, actual)
            if result.returncode != 0 or not os.path.isfile(actual):
                tail = "\n".join(result.stdout.splitlines()[-12:])
                failures.append((case["name"], "fixture run failed"))
                print(f"  FAIL fixture run (exit {result.returncode})")
                if tail:
                    print(tail)
                continue
            if args.update:
                shutil.copyfile(actual, golden)
                print(f"  updated {golden} ({image_size(actual)})")
                continue
            if not os.path.isfile(golden):
                failures.append((case["name"], "missing golden"))
                print(f"  FAIL missing golden {golden} (run --update)")
                continue
            normalized, detail = compare_images(golden, actual)
            status = "ok" if normalized <= args.threshold else "FAIL"
            print(f"  {status} RMSE={normalized:.6f} threshold={args.threshold:.6f} "
                  f"size={image_size(actual)}")
            if normalized > args.threshold:
                failures.append((case["name"], f"RMSE {normalized:.6f}"))
                if detail:
                    print(f"  compare: {detail}")

    if failures:
        print("\nGolden failures:")
        for name, reason in failures:
            print(f"  {name}: {reason}")
        return 1
    print(f"\nAll {len(selected)} golden cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
