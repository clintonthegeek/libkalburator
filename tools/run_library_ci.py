#!/usr/bin/env python3
"""Run the supported libkalburator build/test lane with retained logs."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run(command: list[str], log: Path) -> None:
    print("+", " ".join(command), flush=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(encoding="utf-8"), file=sys.stderr)
        raise SystemExit(result.returncode)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--artifacts", type=Path, default=None)
    parser.add_argument("--jobs", default="4")
    parser.add_argument("--timeout", default="180")
    parser.add_argument("--skip-configure", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    build = (args.build_dir or source / "build-ci").resolve()
    artifacts = (args.artifacts or build / "ci-artifacts").resolve()
    artifacts.mkdir(parents=True, exist_ok=True)

    if not args.skip_configure:
        run(
            [
                "cmake", "-S", str(source), "-B", str(build),
                "-DKALBURATOR_BUILD_TESTS=ON",
                "-DKALBURATOR_BUILD_EXAMPLES=OFF",
                "-DKALBURATOR_ENABLE_UNSUPPORTED_RELOCATION_TESTS=OFF",
            ],
            artifacts / "configure.log",
        )
    run(["cmake", "--build", str(build), f"-j{args.jobs}"], artifacts / "build.log")
    run(
        [
            "ctest", "--test-dir", str(build), "--output-on-failure",
            "--timeout", args.timeout, "--no-tests=error",
        ],
        artifacts / "ctest.log",
    )
    run(
        [
            sys.executable, str(source / "tools" / "test_report.py"),
            "--build-dir", str(build), "--json", str(artifacts / "test-report.json"),
        ],
        artifacts / "test-report.log",
    )
    print(f"library CI lane passed; artifacts: {artifacts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
