#!/usr/bin/env python3
"""Run everything CI gates a merge on.

The one command to run before pushing:

    cmake --build build/desktop --target check    (or pick `check` in the IDE)

Works from any preset: the checks span several build trees, so this drives
each one itself. A failing check does not stop the rest, so one run reports
everything that needs fixing. Coverage is left out; it never fails CI.
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Set by the make that launched us; a nested build must not inherit them.
MAKE_VARS = ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")


def run(cmd):
    """Run one command from the repo root and return True if it succeeded."""
    env = {key: value for key, value in os.environ.items() if key not in MAKE_VARS}
    return subprocess.run([str(part) for part in cmd], cwd=ROOT, env=env).returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cmake", default="cmake", help="cmake executable to use")
    args = parser.parse_args()

    cmake = args.cmake
    ctest = str(Path(cmake).with_name("ctest")) if os.sep in cmake else "ctest"
    jobs = str(os.cpu_count() or 1)

    def build(preset, *targets):
        """Build a preset's targets, configuring it first if it never was."""
        tree = ROOT / "build" / preset
        if not (tree / "CMakeCache.txt").exists() and not run([cmake, "--preset", preset]):
            return False
        extra = ["--target", *targets] if targets else []
        return run([cmake, "--build", tree, "--parallel", jobs, *extra])

    def docs():
        """Doxygen has no failing exit code for warnings; CI reads this file."""
        warnings = ROOT / "docs" / "doxygen_warnings.txt"
        if not run(["doxygen", "Doxyfile"]):
            return False
        if warnings.exists() and warnings.stat().st_size > 0:
            print(warnings.read_text(encoding="utf-8"))
            return False
        return True

    checks = [
        ("format", lambda: build("desktop", "format-check")),
        ("desktop build", lambda: build("desktop")),
        ("unit tests", lambda: run([ctest, "--preset", "desktop"])),
        ("cppcheck (desktop)", lambda: build("desktop", "cppcheck")),
        ("clang-tidy", lambda: build("desktop", "tidy")),
        ("ioc-check", lambda: build("mcu-debug", "ioc-check")),
        ("mcu-debug build", lambda: build("mcu-debug")),
        ("cppcheck (mcu)", lambda: build("mcu-debug", "cppcheck")),
        ("mcu-release build", lambda: build("mcu-release")),
        ("docs", docs),
    ]

    results = []
    for name, check in checks:
        print(f"\n===== {name} =====", flush=True)
        started = time.monotonic()
        passed = check()
        results.append((name, passed, time.monotonic() - started))

    print("\n===== summary =====")
    for name, passed, seconds in results:
        print(f"{'PASS' if passed else 'FAIL'}  {name} ({seconds:.0f}s)")

    failed = sum(1 for _, passed, _ in results if not passed)
    if failed:
        print(f"\n{failed} of {len(results)} checks failed: fix these before pushing.")
        return 1
    print(f"\nAll {len(results)} checks passed: safe to push.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
