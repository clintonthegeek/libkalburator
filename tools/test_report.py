#!/usr/bin/env python3
"""Run CTest and report executed versus skipped Qt test coverage.

CTest considers a QtTest QSKIP result to be a passing executable.  This
report keeps that exit status, but also parses the QtTest totals so that
credentialed and optional lanes cannot look like executed coverage when they
only skipped at runtime.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path


CONSUMER_CONTRACT_TESTS = {
    "tst_planstan_runtime_contract",
    "tst_wildpalms_runtime_contract",
    "tst_run_request_contract",
    "tst_reference_consumer_smoke",
}

OPTIONAL_DEPENDENCY_RE = re.compile(
    r"^tst_(?:akonadibackend|akonadi_scoped_collection|"
    r"akonadicontactsbackend|akonadiprovider(?:$|_)|orgbackend)"
)

START_RE = re.compile(r"^\s*Start \d+: (?P<name>\S+)\s*$")
END_RE = re.compile(
    r"^\s*\d+/\d+ Test #\d+: (?P<name>\S+) .*?\s+(?P<status>\w+)\s+"
    r"(?P<seconds>[0-9.]+) sec\s*$"
)
TOTALS_RE = re.compile(
    r"Totals:\s+(?P<passed>\d+) passed,\s+"
    r"(?P<failed>\d+) failed,\s+(?P<skipped>\d+) skipped"
)
SKIP_RE = re.compile(r"\bSKIP\s+:\s+(?P<case>\S+)\s+(?P<reason>.+)$")


def category_for(name: str) -> str:
    """Return one mutually exclusive baseline category for a CTest name."""

    if name in CONSUMER_CONTRACT_TESTS:
        return "consumer-contract"
    if name.endswith("_live"):
        return "live-credential"
    if OPTIONAL_DEPENDENCY_RE.match(name):
        return "optional-dependency"
    return "hermetic"


def registered_tests(ctest: str, build_dir: Path) -> list[str]:
    command = [ctest, "--test-dir", str(build_dir), "--show-only=json-v1"]
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    payload = json.loads(result.stdout)
    return [test["name"] for test in payload.get("tests", [])]


def result_from_body(body: str, status: str = "unknown") -> dict[str, object]:
    totals = TOTALS_RE.search(body)
    if totals:
        passed = int(totals["passed"])
        failed = int(totals["failed"])
        skipped = int(totals["skipped"])
    else:
        passed = 1 if status.lower() == "passed" else 0
        failed = 0 if status.lower() == "passed" else 1
        skipped = 0

    reasons = []
    for line in body.splitlines():
        match = SKIP_RE.search(line)
        if match:
            reasons.append(match["reason"].strip())

    return {
        "status": status.lower(),
        "passed": passed,
        "failed": failed,
        "skipped": skipped,
        "reasons": reasons,
    }


def parse_runs(output: str) -> dict[str, dict[str, object]]:
    runs: dict[str, dict[str, object]] = {}
    current_name: str | None = None
    current_lines: list[str] = []

    def finish(name: str, body: str, status: str = "unknown") -> None:
        runs[name] = result_from_body(body, status)

    for line in output.splitlines():
        start = START_RE.match(line)
        if start:
            if current_name is not None:
                finish(current_name, "\n".join(current_lines))
            current_name = start["name"]
            current_lines = []
            continue

        end = END_RE.match(line)
        if end and current_name is not None:
            current_lines.append(line)
            finish(current_name, "\n".join(current_lines), end["status"])
            current_name = None
            current_lines = []
            continue

        if current_name is not None:
            current_lines.append(line)

    if current_name is not None:
        finish(current_name, "\n".join(current_lines))
    return runs


def parse_last_test_log(output: str) -> dict[str, dict[str, object]]:
    """Parse CTest's ordered per-test log, which preserves QtTest output."""

    runs = {}
    section_re = re.compile(
        r"(?ms)^\d+/\d+ Testing: (?P<name>\S+)\n"
        r"(?P<body>.*?)(?=^\d+/\d+ Testing: |\Z)"
    )
    for section in section_re.finditer(output):
        body = section["body"]
        status_match = re.search(r"^Test (?P<status>Passed|Failed)\.$", body, re.MULTILINE)
        status = status_match["status"] if status_match else "unknown"
        runs[section["name"]] = result_from_body(body, status)
    return runs


def make_report(test_names: list[str], runs: dict[str, dict[str, object]], exit_code: int) -> dict:
    categories = defaultdict(Counter)
    details = []
    for name in test_names:
        run = runs.get(name, {
            "status": "not-run",
            "passed": 0,
            "failed": 1,
            "skipped": 0,
            "reasons": [],
        })
        category = category_for(name)
        categories[category]["registered"] += 1
        categories[category]["passed"] += int(run["passed"])
        categories[category]["failed"] += int(run["failed"])
        categories[category]["skipped"] += int(run["skipped"])
        if int(run["skipped"]) > 0:
            categories[category]["registrations_with_skips"] += 1
        details.append({"name": name, "category": category, **run})

    ordered_categories = [
        "hermetic",
        "optional-dependency",
        "live-credential",
        "consumer-contract",
    ]
    summary = {
        category: {
            "registered": categories[category]["registered"],
            "executed": categories[category]["passed"] + categories[category]["failed"],
            "passed": categories[category]["passed"],
            "failed": categories[category]["failed"],
            "skipped": categories[category]["skipped"],
            "registrations_with_skips": categories[category]["registrations_with_skips"],
        }
        for category in ordered_categories
    }
    return {
        "ctest_exit_code": exit_code,
        "registered": len(test_names),
        "categories": summary,
        "tests": details,
    }


def print_report(report: dict) -> None:
    print("Libkalburator test baseline")
    print("category               registered  executed  passed  failed  skipped  partial")
    for category, values in report["categories"].items():
        print(
            f"{category:<23}{values['registered']:>10}  {values['executed']:>8}  "
            f"{values['passed']:>6}  {values['failed']:>6}  {values['skipped']:>7}  "
            f"{values['registrations_with_skips']:>7}"
        )
    print(f"registered CTest tests: {report['registered']}")
    skipped = [test for test in report["tests"] if test["skipped"]]
    if skipped:
        print("\nRuntime-skipped registrations:")
        for test in skipped:
            reason = test["reasons"][0] if test["reasons"] else "(reason not reported)"
            print(f"  {test['name']} [{test['category']}]: {reason}")
    else:
        print("\nRuntime-skipped registrations: none")
    print(f"CTest exit code: {report['ctest_exit_code']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--json", type=Path, help="also write the machine-readable report here")
    args = parser.parse_args()

    try:
        names = registered_tests(args.ctest, args.build_dir)
    except (OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"unable to enumerate CTest tests: {error}", file=sys.stderr)
        return 2

    command = [
        args.ctest,
        "--test-dir",
        str(args.build_dir),
        "-V",
        "--output-on-failure",
        "--no-tests=error",
    ]
    completed = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    log_path = args.build_dir / "Testing" / "Temporary" / "LastTest.log"
    if log_path.is_file():
        log_runs = parse_last_test_log(log_path.read_text(encoding="utf-8", errors="replace"))
    else:
        log_runs = {}
    runs = log_runs if len(log_runs) >= len(names) else parse_runs(completed.stdout)
    report = make_report(names, runs, completed.returncode)
    print_report(report)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
