#!/usr/bin/env python3
"""Run bounded sanitizer, thread, and opt-in live test lanes separately.

The live lane is deliberately opt-in because it needs credentials and/or a
running Akonadi session. Every lane keeps its command output and records the
CTest exit status so runtime skips remain visible instead of being mistaken
for executed coverage.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


THREAD_REGEX = r"tst_(backend_executor|backend_concurrency_capability|decsync_active_controller_thread|transformationregistry_threadsafety)"
LIVE_REGEX = r"(_live$|tst_(akonadi|orgbackend|caldav_integration|carddav_capability_discovery))"
THREAD_TARGETS = [
    "tst_backend_executor",
    "tst_backend_concurrency_capability",
    "tst_decsync_active_controller_thread",
    "tst_transformationregistry_threadsafety",
]


def run(command: list[str], log: Path, timeout: int) -> int:
    print("+", " ".join(command), flush=True)
    try:
        completed = subprocess.run(
            command,
            stdout=log.open("w", encoding="utf-8"),
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        log.write_text(log.read_text(encoding="utf-8") + "\nTIMEOUT\n", encoding="utf-8")
        return 124
    return completed.returncode


def lane(
    name: str,
    command: list[str],
    artifacts: Path,
    timeout: int,
) -> dict[str, object]:
    log = artifacts / f"{name}.log"
    code = run(command, log, timeout)
    result = {"name": name, "exit_code": code, "log": str(log)}
    (artifacts / f"{name}.json").write_text(json.dumps(result, indent=2) + "\n")
    if code:
        print(log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--artifacts", type=Path, default=None)
    parser.add_argument("--jobs", default="4")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--live", action="store_true", help="also run credentialed/optional live tests")
    parser.add_argument("--skip-configure", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    root = (args.build_dir or source / "build-quality").resolve()
    sanitizer_build = root / "asan-ubsan"
    artifacts = (args.artifacts or root / "artifacts").resolve()
    artifacts.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, object]] = []
    if not args.skip_configure:
        results.append(lane(
            "asan-ubsan-configure",
            [
                "cmake", "-S", str(source), "-B", str(sanitizer_build),
                "-DKALBURATOR_BUILD_TESTS=ON",
                "-DKALBURATOR_BUILD_EXAMPLES=OFF",
                "-DKALBURATOR_ENABLE_UNSUPPORTED_RELOCATION_TESTS=OFF",
                "-DCMAKE_BUILD_TYPE=Debug",
                "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer",
                "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined",
                "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined",
            ], artifacts, args.timeout))

    results.append(lane(
        "asan-ubsan-build",
        ["cmake", "--build", str(sanitizer_build), f"-j{args.jobs}", "--target", *THREAD_TARGETS],
        artifacts, args.timeout * 2,
    ))
    results.append(lane(
        "asan-ubsan-tests",
        ["ctest", "--test-dir", str(sanitizer_build), "--output-on-failure",
         "--timeout", str(args.timeout), "-R", THREAD_REGEX],
        artifacts, args.timeout * 2,
    ))

    # Keep the thread lane independent from sanitizer runtime behavior: it uses
    # the supported regular build and can therefore be compared with the
    # baseline without sanitizer allocator effects.
    results.append(lane(
        "thread-focused-tests",
        ["ctest", "--test-dir", str(source / "build"), "--output-on-failure",
         "--timeout", str(args.timeout), "-R", THREAD_REGEX],
        artifacts, args.timeout * 2,
    ))

    if args.live:
        results.append(lane(
            "live-optional-tests",
            ["ctest", "--test-dir", str(source / "build"), "-V",
             "--output-on-failure", "--timeout", str(args.timeout), "-R", LIVE_REGEX],
            artifacts, args.timeout * 3,
        ))
    else:
        skipped = {"name": "live-optional-tests", "status": "not-run", "reason": "pass --live"}
        (artifacts / "live-optional-tests.json").write_text(json.dumps(skipped, indent=2) + "\n")
        print("live-optional-tests: NOT RUN (pass --live to opt in)")

    summary = {"results": results, "live_requested": args.live}
    (artifacts / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    failed = [result for result in results if result.get("exit_code")]
    print(f"quality lanes: {'failed' if failed else 'passed'}; artifacts: {artifacts}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
