#!/usr/bin/env python3
"""Build both real consumers against this libkalburator working tree.

The lane deliberately uses source-directory overrides instead of an installed
or fetched libkalburator.  This keeps consumer verification tied to the exact
tree being changed, including uncommitted source changes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


PLANSTAN_TESTS = (
    r"tst_planstan_runtime_contract|"
    r"tst_collection_runtime_(definition_compiler|event_projector|run_adapter|"
    r"conflict_adapter|account_adapter|discovery_adapter|topology_adapter)|"
    r"tst_collectioncontroller_(provider_lifecycle|recordchanged|syncprogress|"
    r"syncverbs|lifecycle)"
)
WILDPALMS_TESTS = (
    r"tst_palm_runtime_(hotsync|cancel_sync|conflict_handler|modes|routes|"
    r"route_first_sync|route_recategorization|run_lifecycle|clobber_sync)|"
    r"tst_(account_controller|mapping_enable_persists)|"
    r"tst_kf6mainwindow_conflict_apply|"
    r"tst_runtime_carddav_e2e|"
    r"tst_(memoblobbackend|memobackendplugin|todoblobbackend|"
    r"todobackendplugin|contactsblobbackend|contactsbackendplugin)"
)


def run(command: list[str], log: Path) -> None:
    print("+", " ".join(command))
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(encoding="utf-8"), file=sys.stderr)
        raise SystemExit(result.returncode)


def tree_identity(root: Path) -> dict[str, str | bool]:
    def git(*args: str) -> str:
        return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()

    diff = subprocess.check_output(
        ["git", "-C", str(root), "diff", "--no-ext-diff"],
    )
    return {
        "path": str(root),
        "head": git("rev-parse", "HEAD"),
        "dirty": bool(git("status", "--porcelain")),
        "working_diff_sha256": hashlib.sha256(diff).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lib", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--planstan", type=Path, default=None)
    parser.add_argument("--wildpalms", type=Path, default=None)
    parser.add_argument("--planstan-build", type=Path, default=None)
    parser.add_argument("--wildpalms-build", type=Path, default=None)
    parser.add_argument("--artifacts", type=Path, default=None)
    parser.add_argument("--skip-configure", action="store_true")
    args = parser.parse_args()

    lib = args.lib.resolve()
    planstan = (args.planstan or lib.parent / "PlanStan").resolve()
    wildpalms = (args.wildpalms or lib.parent / "WildPalms").resolve()
    planstan_build = (args.planstan_build or planstan / "build-dev").resolve()
    wildpalms_build = (args.wildpalms_build or wildpalms / "build").resolve()
    artifacts = (args.artifacts or lib / "build" / "consumer-integration").resolve()
    artifacts.mkdir(parents=True, exist_ok=True)

    manifest = {
        "libkalburator": tree_identity(lib),
        "planstan": str(planstan),
        "wildpalms": str(wildpalms),
        "planstan_build": str(planstan_build),
        "wildpalms_build": str(wildpalms_build),
    }
    (artifacts / "source-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )

    if not args.skip_configure:
        planstan_configure = [
            "cmake", "-S", str(planstan), "-B", str(planstan_build),
            f"-DPLANSTAN_LIBKALBURATOR_SOURCE_DIR={lib}",
            "-DBUILD_TESTING=ON",
        ]
        populated_editor = planstan_build / "_deps" / "qorgmodeeditor-src"
        if (populated_editor / "src" / "CMakeLists.txt").exists():
            planstan_configure.append(
                f"-DPLANSTAN_QORGMODEEDITOR_SOURCE_DIR={populated_editor}"
            )
        run(
            planstan_configure,
            artifacts / "planstan-configure.log",
        )
        run(
            [
                "cmake", "-S", str(wildpalms), "-B", str(wildpalms_build),
                f"-DWILDPALMS_LIBKALBURATOR_SOURCE_DIR={lib}",
                "-DBUILD_TESTS=ON",
            ],
            artifacts / "wildpalms-configure.log",
        )

    run(
        ["cmake", "--build", str(planstan_build), "-j4"],
        artifacts / "planstan-build.log",
    )
    run(
        [
            "ctest", "--test-dir", str(planstan_build), "--output-on-failure",
            "--timeout", "120", "-R", PLANSTAN_TESTS,
        ],
        artifacts / "planstan-test.log",
    )
    run(
        ["cmake", "--build", str(wildpalms_build), "-j4"],
        artifacts / "wildpalms-build.log",
    )
    run(
        [
            "ctest", "--test-dir", str(wildpalms_build), "--output-on-failure",
            "--timeout", "120", "-R", WILDPALMS_TESTS,
        ],
        artifacts / "wildpalms-test.log",
    )
    print(f"consumer integration lane passed; artifacts: {artifacts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
