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

## 3. OPEN inbound items (the actual drift — needs libkalburator action)

None are release blockers; all are honest-reporting / test-double gaps. Logged
in `docs/campaign/FINDINGS.md` under the noted O-numbers so they enter the
tracked backlog.

| # | From | Item | Severity | Source doc |
|---|---|---|---|---|
| **O46** | WildPalms (2026-07-18) | Surface the read-only write-skip in `SyncResult` (both write gates; `target-readonly:`/`source-readonly:` warning or a skip-reason field). No behavior change — skip stays a no-op success; ask is *visibility* for UI edge badges. | Low (honesty gap) | `WildPalms/docs/2026-07-18-libkalburator-readonly-skip-reporting-rfc.md` |
| **O47** | WildPalms (2026-07-19) | `MockBlobBackend` never computes `BackendRecord::contentHash` (unlike `LocalBlobBackend`/`GenericSqliteBackend`). v0.93 deleted the `useQuickPath→SourceWins` downgrade that masked it, so `perrecorddiff.cpp`'s fail-loud empty-hash rule now manufactures a spurious `BothModified` conflict on any 2-pass TwoWay/AskUser sync through the mock. Suggested fix (a): compute SHA-256 in the mock when incoming hash is empty. | Low (lib-owned test double; WP has a local workaround) | `WildPalms/docs/2026-07-19-libkalburator-mockblobbackend-contenthash-gap-handoff.md` |

---

## 4. Recently CLOSED inbound items (context — do not re-open)

| Item | Resolution |
|---|---|
| **VTODO/VJOURNAL calendar dispatch** (PlanStan handoff 2026-06-28) | Shipped v0.80 (§2). Handoff doc carries the resolution note. |
| **WP-A1 calendarsOnly mode** (`docs/2026-06-10-wpa1-calendarsonly-mode-rfc.md`) | Mode + persisted flag shipped (v0.79); **both consumers signed off 2026-07-18** (`WildPalms/docs/2026-07-18-wpa1-calendarsonly-rfc-response-wildpalms.md`; PlanStan already uses `calendarsOnly=true`). Remaining ctor-default flip `true`→`false` is safe for both — **ready to close/apply.** |
| **fanout-collapse adoption** (PlanStan) | `PlanStan/docs/handoffs/2026-07-12-fanout-collapse-final-handoff.md`; PlanStan now pins v0.94. |
| **PlanStan integration-test API drift** | Compile drift RESOLVED at v0.74 (`PlanStan/docs/todo/stale-integration-tests-libkalburator-api-drift.md`); only runtime staleness of `EXCLUDE_FROM_ALL` tests remains — PlanStan-side, not a lib obligation. |

---

## 5. Superseded / stale docs in this repo (cleaned up 2026-07-19)

- `docs/2026-06-30-vtodo-vjournal-dispatch-alternatives.md` — a parallel
  alternatives analysis that independently recommended the same design
  ("Alternative A") already shipped as v0.80. Marked **SUPERSEDED** in-file;
  kept for provenance.

---

## 6. Suggested next libkalburator action

1. Apply **O46** (read-only skip visibility) — the narrow `warnings` addition at
   both write gates; matches the E1 "honest stats" and Akonadi Fix-B
   "no-ops must be discriminable" principles. Cheap, both UIs already consume
   `SyncResult::warnings`.
2. Apply **O47** fix (a) — compute the content hash in `MockBlobBackend` to match
   production `IBlobBackend`s. Removes a latent trap for every mock-based
   consumer test.
3. Close **WP-A1** — flip the `MultiProtocolDavProvider` `calendarsOnly` ctor
   default to `false` now that both consumers have signed off, and mark the RFC
   resolved.

All three are small and independent; none blocks a consumer.
