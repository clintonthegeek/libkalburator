# Consumer coordination status — libkalburator ↔ PlanStan ↔ WildPalms

**Date:** 2026-07-19
**Maintainer note:** This is the single "where do the three repos stand" page.
It exists because inbound RFCs/handoffs from the two consumer projects were
accumulating faster than this repo's roadmap acknowledged them. Update it
whenever a consumer files a new RFC/handoff, when an open item is resolved, or
when a pin moves. It is a **status index**, not a design doc — each row points
at the authoritative doc.

---

## 1. Release / pin state (as of 2026-07-19)

| Repo | Pins libkalburator at | Notes |
|---|---|---|
| **libkalburator** | — (self) | `main` @ `98f605b`, released tag **v0.94** (sync-graph-redesign Phase 1, closed 2026-07-16) |
| **PlanStan** | **v0.94** | `CMakeLists.txt:69` `PLANSTAN_LIBKALBURATOR_GIT_TAG "v0.94"`. Current. |
| **WildPalms** | **v0.94** | `CMakeLists.txt:69`. **Mid-port**: leaping v0.77→v0.94 (`WildPalms/docs/2026-07-18-libkalburator-v077-to-v094-gap-analysis-and-leap-plan.md`), Phase 1 in progress. |

**Both consumers pin the current head tag.** The drift is *not* version lag —
it is unlogged inbound coordination (§3) surfaced by WildPalms' big v0.77→v0.94
catch-up.

---

## 2. What shipped since v0.79 (the last version the VTODO/VJOURNAL handoff saw)

Condensed; full detail in each campaign's archive + this repo's `CLAUDE.md`.

| Tag | Theme | One line |
|---|---|---|
| **v0.80** | **Calendar per-kind canon dispatch** | VTODO/VJOURNAL no longer transcode to empty bytes — `{calendar,ical}`↔`{calendar,canon}` stages dispatch on component kind; envelope `kind` discriminator; shared VTODO helpers; first-class VJOURNAL; fail-loud guard; N1 component-scoped recurrence fix. Resolved the 2026-06-28 PlanStan handoff. |
| v0.80–v0.82 | sync-convergence (Tracks A–C) | roadmap `docs/campaign/2026-07-03-sync-convergence-roadmap.md` |
| v0.83/v0.84 | sync-hardening (D1 threading, O16–O27) | `docs/campaign/archive/2026-07-05-sync-hardening-phases.md` |
| v0.85–v0.91 | **sync-excellence** (E1–E13 + CP-A/B/C) | honest stats, async-backend rework, RFC 6578 sync-collection, canon timestamp fix, bounded write-dispatch. FINDINGS O26–O45. Closed 2026-07-09. |
| v0.92 | per-item fetch/write progress relay | — |
| **v0.93** | **fanout-collapse** | single per-account cal + contacts backends; slug collection ids; deleted `MappingScheduler`, `BaselineStore v2`, `CalendarBaselineStore`, `RemoteCalendarBackend::create`, id-prefix machinery; `createBackends()` spec-list contract. |
| **v0.94** | **sync-graph-redesign Phase 1** | L1 skip-invalidation un-freeze; L2 fixpoint passes; per-LC `WiringPolicy`; providers emit Connecting/Connected/Error connection states. Closed 2026-07-16. |

> The v0.80 calendar per-kind dispatch is the item the earlier brainstorm/spec/plan
> in this repo produced (`docs/superpowers/specs/2026-06-28-calendar-per-kind-canon-dispatch-design.md`,
> `docs/superpowers/plans/2026-06-28-calendar-per-kind-canon-dispatch.md`). **Confirmed
> shipped** — commits `5498804` → `d89a863` → `eae5bb1` (+ `b879fec` N1), handoff
> resolution `7e8b5e6`.

---

## 2b. UNRELEASED — conflict-resolution repair (branch `feature/conflict-resolution-repair`, 2026-08-21)

Answers PlanStan's `docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-handoff.md`.
Full response: **`docs/2026-08-21-conflict-resolution-repair-response.md`** — read that
before answering any consumer question about conflicts.

Four defects, all rooted in the canon-upgrade campaign promoting record data to
canonical Shape JSON without telling the conflict code:

| # | Defect | Status |
|---|---|---|
| A | `ConflictInfo::source/targetIcalData` carried canonical JSON, not native iCal | Fixed |
| B | An `Unmonitored` resolution wrote one DB column and never touched data — **conflict resolution was wholly non-functional in the only mode PlanStan uses** | Fixed |
| C | `resumeAfterConflict` never read the caller's `mergedIcal`; every Custom Merge was silently replaced by the auto-merge | Fixed |
| D | `Duplicate` byte-patched `UID:` against canonical JSON, so "Keep Both" produced a colliding clone | Fixed — closes PlanStan's `sync-dialog-keepboth-duplicate-not-created.md` |

**All API changes additive; neither consumer needs a code change.** Five
behavior changes are consumer-visible — see §"Consumer-visible changes" in the
response doc, especially: an `Unmonitored` run can now write data where it
previously never would, and `conflictDetected` now carries a populated
`conflictId`.

**Two things PlanStan must NOT wait on:** `baselineIcalData` stays empty, so the
3-way diff path remains unreachable (**O48** — baseline *bytes* are stored
nowhere; needs a storage decision); and a rehydrated `CustomMerge` loses the
user's payload across an app restart (**O52** — same schema decision).

**One thing PlanStan should act on: O53.** The batch conflict dialog is modal
and runs inside `onWorkerSyncCompleted` while other mappings are in flight.
Pre-existing and not introduced here, but PlanStan's own
`syncMaxConcurrentMappings()` default of 4 makes it live.

New FINDINGS: **O48, O49, O50 (fixed), O51, O52, O53**.

---

## 2c. OPEN inbound — 2026-08-21

| # | From | Item | Status |
|---|---|---|---|
| **O54** | PlanStan (live session, 2026-08-21) | `RemoteCalendarBackend` guesses every item's write URL as `<calendar>/<uid>.ics`; false for any item another CalDAV client created — first edit-and-sync of an adopted calendar's pre-existing items fails permanently (SabreDAV 400). **CRITICAL, urgent** — no conflict needed to trigger, close to "the first real edit a new user makes will fail." | OPEN, not yet fixed. Full writeup: `docs/2026-08-21-remotecalendarbackend-uid-url-assumption-critical-bug.md`. **Filed as the first item the next session must read and fix** (see `CLAUDE.md` top). |
| **O55** | WildPalms (handoff, 2026-08-21, catching up past a dormant v0.77 pin) | TwoWay sync between a bare-id backend and the `GenericSqliteBackend` hub churns and empties the hub from pass 2 on — `perRecordDiff()` joins strictly by raw `BackendRecord::id` with no aliasing for the hub's `<collectionId>\x01<origId>` prefix. Regression v0.77→v0.93+, no full bisect yet. Silent — sync reports success. | OPEN, non-blocking, **can wait** — WildPalms itself is not currently in active development. Full writeup: `~/dev/WildPalms/docs/2026-08-21-libkalburator-hub-record-id-join-churn-handoff.md`. Tracked as FINDINGS **O55**. |

---

## 3. Inbound items — ALL RESOLVED 2026-07-19 (branch `feature/consumer-rfcs-o46-o47-wpa1`)

None were release blockers; all were honest-reporting / test-double gaps.
Fixed with tests; suite green (171/173, the 2 failures pre-existing at v0.94 —
see §7).

| # | From | Item | Resolution |
|---|---|---|---|
| **O46** | WildPalms (2026-07-18) | Surface the read-only write-skip in `SyncResult`. | Both write gates emit a stable-prefix `target-readonly:<col>` / `source-readonly:<col>` warning; no behavior change. `tst_calendar_readonly_skip`. |
| **O47** | WildPalms (2026-07-19) | `MockBlobBackend` never computes `contentHash` → spurious `BothModified` conflict on 2-pass mock syncs post-v0.93. | Mock hashes on write (SHA-256, matching `LocalBlobBackend`) when incoming hash empty; caller hash preserved. `tst_mockblobbackend::computesContentHashWhenIncomingEmpty`. |
| **WP-A1** | WildPalms (RFC 2026-06-10, sign-off 2026-07-18) | calendarsOnly ctor-default residue. | Flipped `MultiProtocolDavProvider` `calendarsOnly` ctor default (+ member) `true`→`false` to agree with the sole real construction. `tst_multiprotocoldavprovider`. RFC marked RESOLVED. |

---

## 4. Recently CLOSED inbound items (context — do not re-open)

| Item | Resolution |
|---|---|
| **VTODO/VJOURNAL calendar dispatch** (PlanStan handoff 2026-06-28) | Shipped v0.80 (§2). Handoff doc carries the resolution note. |
| **WP-A1 calendarsOnly mode** (`docs/2026-06-10-wpa1-calendarsonly-mode-rfc.md`) | Mode + persisted flag shipped (v0.79); both consumers signed off 2026-07-18; ctor-default flip applied 2026-07-19 (§3). RFC marked RESOLVED. |
| **fanout-collapse adoption** (PlanStan) | `PlanStan/docs/handoffs/2026-07-12-fanout-collapse-final-handoff.md`; PlanStan now pins v0.94. |
| **PlanStan integration-test API drift** | Compile drift RESOLVED at v0.74 (`PlanStan/docs/todo/stale-integration-tests-libkalburator-api-drift.md`); only runtime staleness of `EXCLUDE_FROM_ALL` tests remains — PlanStan-side, not a lib obligation. |

---

## 5. Superseded / stale docs in this repo (cleaned up 2026-07-19)

- `docs/2026-06-30-vtodo-vjournal-dispatch-alternatives.md` — a parallel
  alternatives analysis that independently recommended the same design
  ("Alternative A") already shipped as v0.80. Marked **SUPERSEDED** in-file;
  kept for provenance.

---

## 6. Next libkalburator action

O46, O47, and WP-A1 are **done** (§3) on branch `feature/consumer-rfcs-o46-o47-wpa1`.
Once merged, cut a release tag (v0.95) and notify both consumers — no pin bump
is forced (all three are additive/non-breaking for consumers; WP-A1 only flips
a default the sole real caller already overrode).

## 7. Known pre-existing failures (NOT introduced by §3; carried from v0.94)

The full suite is 171/173. The two reds predate this branch (documented in
`CLAUDE.md` at the v0.94 close) and are untouched by the O46/O47/WP-A1 work:

- **`tst_calendar_canon_roundtrip::canonPersonalClassificationProducesPrivateAndStash`**
  — `classification=personal` no longer stashes `X-CANON-CLASSIFICATION:personal`
  on demote. A genuine calendar-canon classification-encoding regression, in
  `icalcanonstages.cpp` (not touched here). Worth its own fix.
- **`tst_remotecalendarbackend`** — the long-standing Radicale live-state flake.

Neither shares a code path with §3.
