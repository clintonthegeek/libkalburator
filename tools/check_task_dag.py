#!/usr/bin/env python3
"""Validate the active task dependency graph in docs/TASKS.md."""

from __future__ import annotations

import re
import sys
from pathlib import Path


TASK_HEADING = re.compile(r"^### ([A-Z]+-\d+)\b")
STATE = re.compile(r"^- \*\*State:\*\* ([A-Z ]+)")
DEPENDS = re.compile(r"^- \*\*Depends on:\*\* (.+)$")
DEPENDENCY_LIST = re.compile(r"(?:[A-Z]+-\d+)(?:, [A-Z]+-\d+)*")
TASK_ID = re.compile(r"[A-Z]+-\d+")


def fail(message: str) -> None:
    print(f"task DAG error: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    task_file = Path(__file__).resolve().parents[1] / "docs" / "TASKS.md"
    tasks: dict[str, dict[str, object]] = {}
    current: str | None = None

    for line_number, line in enumerate(task_file.read_text().splitlines(), 1):
        if match := TASK_HEADING.match(line):
            current = match.group(1)
            if current in tasks:
                fail(f"duplicate heading {current} at line {line_number}")
            tasks[current] = {"state": None, "dependencies": [], "line": line_number}
            continue
        if current is None:
            continue
        if match := STATE.match(line):
            tasks[current]["state"] = match.group(1).strip()
        elif match := DEPENDS.match(line):
            value = match.group(1)
            if value == "—":
                continue
            if not DEPENDENCY_LIST.fullmatch(value):
                fail(f"{current} has a non-task or malformed dependency list: {value!r}")
            tasks[current]["dependencies"] = TASK_ID.findall(value)

    active = {
        task_id: task
        for task_id, task in tasks.items()
        if task["state"] is not None and task["state"] != "REMOVED"
    }
    for task_id, task in active.items():
        for dependency in task["dependencies"]:
            if dependency not in active:
                fail(f"{task_id} depends on missing or removed task {dependency}")

    visiting: list[str] = []
    visited: set[str] = set()

    def visit(task_id: str) -> None:
        if task_id in visiting:
            start = visiting.index(task_id)
            fail("cycle: " + " -> ".join(visiting[start:] + [task_id]))
        if task_id in visited:
            return
        visiting.append(task_id)
        for dependency in active[task_id]["dependencies"]:
            visit(str(dependency))
        visiting.pop()
        visited.add(task_id)

    for task_id in active:
        visit(task_id)

    ready = sorted(
        task_id for task_id, task in active.items() if task["state"] == "READY"
    )
    edge_count = sum(len(task["dependencies"]) for task in active.values())
    print(
        f"task DAG: {len(active)} active nodes, {edge_count} edges, acyclic; "
        f"READY: {', '.join(ready) or 'none'}"
    )


if __name__ == "__main__":
    main()
