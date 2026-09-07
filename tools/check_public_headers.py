#!/usr/bin/env python3
"""Audit that in-tree public headers use domain-qualified internal includes."""

from __future__ import annotations

import re
import sys
from pathlib import Path


LOCAL_INCLUDE = re.compile(r'^\s*#include\s+"([^"/]+\.h)"')
RELATIVE_INCLUDE = re.compile(r'^\s*#include\s+"\.\./')


def main() -> int:
    source = Path(__file__).resolve().parents[1] / "src"
    failures: list[str] = []
    for header in sorted(source.rglob("*.h")):
        for line_number, line in enumerate(header.read_text().splitlines(), 1):
            if LOCAL_INCLUDE.match(line) or RELATIVE_INCLUDE.match(line):
                # OrgFileManager is intentionally supplied by PlanStan's
                # optional org-io peer and is excluded from the default tree.
                if '"orgfilemanager.h"' in line:
                    continue
                failures.append(f"{header}:{line_number}: {line.strip()}")
    if failures:
        print("unqualified public-header includes:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"public-header include audit passed ({len(list(source.rglob('*.h')))} headers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
