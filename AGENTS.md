# Working rules

This file is the operational entry point for humans and coding agents. Keep it short. Product facts belong in `docs/`; active work belongs in `docs/TASKS.md`.

## Start here

1. Read `README.md`.
2. Read `docs/ARCHITECTURE.md`, `docs/INVARIANTS.md`, and the top of `docs/ROADMAP.md`.
3. Read `docs/TASKS.md` sections **Now** and the selected task.
4. Read related entries in `docs/KNOWN_ISSUES.md` and `docs/FEATURES.md`.
5. Inspect current code and tests. Archived prose is historical evidence, not authority.

## Choose and execute work

- Follow the priority stated at the top of **Now**. As of 2026-09-13 that is
  `KND-001` (ADR 0011), by user decision, ahead of `RRD-022` and `FAM-002`.
  An older `DONE` result is historical evidence, not current production
  certification.
- Work on the first unblocked task in **Now** unless the user specifies another task.
- Mark the task `IN PROGRESS` before code changes. Only one task may be in progress per worker.
- Re-check cited symbols and assumptions against the current trees of libkalburator, `../PlanStan`, and `../WildPalms` when affected.
- Add a failing or characterization test before changing risky behavior.
- For a revalidated task, preserve prior `Result` and `Verification` as
  historical evidence, then record the new reproducer, exact command output,
  affected repository revisions, configuration, and remaining acceptance.
- Prefer deleting duplicate consumer orchestration over adding adapters around it.
- Backward compatibility is not required during the consolidation. Coordinate and change all three repositories when that yields a cleaner contract.
- Do not add a new dated plan, handoff, response, session log, or return receipt. Put durable decisions in `docs/adr/`; put tasks in `docs/TASKS.md`; put defects in `docs/KNOWN_ISSUES.md`.
- Never edit files under `docs/archive/`.

## Cross-repository tasks

- `docs/TASKS.md` is also the coordination queue for consolidation work whose
  scope names `../PlanStan` or `../WildPalms`. Mark that task `IN PROGRESS`
  here, then read and follow the consumer repository's own `AGENTS.md` before
  editing it.
- Do not create a second roadmap, campaign, handoff, or receipt in a consumer
  repository for work already tracked here. Update the consumer's maintained
  architecture/current-status documentation when its facts change, and record
  task state, result, and verification in this queue.
- A consumer task is verified against the working libkalburator tree, not only
  against a pinned release. Keep preparatory adapters inert until the task that
  performs the named production ownership switch; two active runtime owners are
  never an acceptable intermediate result.

## Required checks

Run checks proportional to the change. A change that touches a consumer-facing contract is not done until the affected consumer builds and its relevant tests pass against the working libkalburator tree.

Minimum library check:

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Do not dismiss a failure because it is old. Record cause and product impact separately. Timeouts, unfinished futures, stuck run state, unsafe teardown, and failures on a consumer-used path are product defects even when an external service helped trigger them.

## Finish a task

In the same change:

1. Update the task in `docs/TASKS.md`: state, result, verification, and any newly unblocked task.
2. Add, resolve, or revise affected entries in `docs/KNOWN_ISSUES.md`.
3. Update `docs/FEATURES.md` if maturity, integration, or support changed.
4. Update `docs/ARCHITECTURE.md` or `docs/CONSUMING.md` if ownership or a consumer contract changed.
5. Update `docs/COMPATIBILITY.md` if targets, headers, schemas, versions, or dependencies changed.
6. Update `docs/ROADMAP.md` only when an outcome gate or priority changes.
7. Add an ADR only for a durable decision that future work might otherwise relitigate.

Documentation should state current facts or maintain concise lists. Delete obsolete current text; history is available in Git and the archive.

## Scope guard

The active goal is a safe, small, reusable runtime for the two real consumers. Do not add domains, providers, vendor interiors, generalized frameworks, or compatibility shims unless an active task requires them.
