# Consumer coordination status — libkalburator ↔ PlanStan ↔ WildPalms

**Date:** 2026-07-19
**Maintainer note:** This is the single "where do the three repos stand" page.
It exists because inbound RFCs/handoffs from the two consumer projects were
accumulating faster than this repo's roadmap acknowledged them. Update it
whenever a consumer files a new RFC/handoff, when an open item is resolved, or
when a pin moves. It is a **status index**, not a design doc — each row points
at the authoritative doc.

---

## 1. Release / pin state (as of 2026-08-25)

| Repo | Pins libkalburator at | Notes |
|---|---|---|
| **libkalburator** | — (self) | `main`, tags up to **v1.03** (EEE Tier A closed: all four vendor edges + fixture promotion, O66/O67 carrier verdicts, A4 live checkpoint both directions, `tools/groundtrip`; 2026-08-25 — §2e). Fully additive; consumers unaffected. |
| **PlanStan** | **v0.97** | Not yet bumped to v0.98+ (all additive, no code change forced). Unaffected by O55/O56 (no sqlite-hub endpoints; engine-stable ids everywhere). Note O56's hold-gate changes Unmonitored AskUser semantics for it too: a run with an unresolved conflict now writes NOTHING (all-or-nothing per mapping). |
| **WildPalms** | **v1.00 (bumped post-O55)** | Filed the O55 followup (2026-08-22); **RESOLVED same day in v1.01** (§2c) — pin bump only; v1.00-poisoned profiles self-heal. |

**Both consumers pin a recent head tag; no forced bump from v0.98** — all
conflict-resolution-repair changes are additive (§2b).

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

## 2b. RELEASED v0.98 (2026-08-22) — conflict-resolution repair

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

## 2c. OPEN inbound — updated 2026-08-22 (O54, O55, O56 all RESOLVED; none open)

## 2d. NEW (2026-08-23) — vendor-convergence (EEE) campaign opened; GraphCLI experiment live; **consumer-relevant Graph behavior findings**

Not consumer-facing yet — **zero pin/code impact** — recorded here because it
changes the roadmap picture. The vendor-convergence campaign
(`docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md`, readiness
assessment in `docs/2026-08-22-canon-domains-and-cross-format-readiness.md`)
has started Phase 0: the `graphcli` experiment tool (`tools/graphcli/`,
opt-in build `KALBURATOR_BUILD_GRAPHCLI`) is capturing a real-payload corpus
against live Microsoft Graph accounts into gitignored `msgraph*/captured/`.

**⚠ Consumers should read FINDINGS O57 (all addenda).** A substantial share
of what it documents is host-application logic the library will NOT absorb:
RSVP flows (Graph-native `accept`/`tentativelyAccept`+counter-proposal only;
eventMessage actions don't exist on consumer accounts), invite-ingestion
timing (inbox-delivered invites auto-appear on calendars in ~75s; junked
invites are never processed, and un-junking doesn't retro-process), emailed
iTIP REPLYs as an out-of-band mutation source (changeKey moves with no Graph
write), and above all **(t) attendee alias expansion** — vendors rewrite
invitee addresses to canonical internal identities, so naive email-string
matching of attendees across vendors yields phantom non-responders and
phantom accepters that never converge. Until the identity layer lands
(campaign §5), hosts must treat cross-vendor attendee diffs as advisory.

A future **Microsoft Graph backend** is pinned as Phase 7 of the proposal —
now with a full work breakdown (§7.a/§7.b there). **MVP scope deliberately
mirrors PlanStan's current event model** (solo events, no attendees/iTIP):
attendees ride the canon read-only, RSVP flows deferred until PlanStan ships
organization features. Consumers: nothing to do; all additive when it lands.

**Progress 2026-08-23 (tag v1.02):** EEE Phase 2 landed and live-checkpointed
— `google-event ⇄ calendar/canon` edge stages with declared loss profile,
Google-side corpus fixtures committed (`tests/fixtures/vendor/google/`),
Stage D mock Graph server test bed (`tests/graph/`), googlecli transport
lab authorized. Google wire-truth findings in **O59** — note (f): Google
silently drops consent-screen-unapproved OAuth scopes (relevant to any
future Google transport work). Still zero consumer impact; campaign status:
`docs/campaign/eee/STATUS.md`.

## 2e. Tier A CLOSED (2026-08-25, tag v1.03) — vendor edges complete; wire knowledge consolidated

Still **zero pin/code impact** for consumers — everything is additive.
Tier A of the EEE campaign closed: all four vendor edges
(`google-person`, `ms-contact`, `google-task`, `ms-todotask`) + the two
event edges live-checkpointed in BOTH directions (A4, FINDINGS **O67**),
sanitized task fixtures committed with promotion slots, O66/O67 carrier
verdicts annotated into CONVERGENCE-MATRIX + loss profiles. Roundtrip
harnesses `tools/msroundtrip` / `tools/groundtrip` let hosts replay the
checkpoint protocol without throwaway code.

**Durable REST-API knowledge for BOTH vendors now lives in one page:**
`docs/campaign/eee/vendor-rest-api-wire-notes.md` (Google Calendar/Tasks +
MS Graph create/read/carrier/identity behaviors, O57–O67 consolidated).
Consumers building their own vendor transports should read it before
touching either API — highlights: Google insert rejects read-only
created/updated but HONORS client iCalUID; Graph create mints a fresh uid,
rewrites organizer, auto-provisions Teams meetings; both vendors rewrite
organizer to the authenticating account; Graph consumer GET-by-id is
unreliable (use listings/delta).

| # | From | Item | Status |
|---|---|---|---|
| ~~**O54**~~ | PlanStan (live session, 2026-08-21) | `RemoteCalendarBackend` guessed every item's write URL as `<calendar>/<uid>.ics`; false for any item another CalDAV client created — first edit-and-sync of an adopted calendar's pre-existing items failed permanently (SabreDAV 400). | **RESOLVED 2026-08-22** (branch `fix/o54-uid-url-assumption`): `m_uidToUrl` cache + `resolveItemUrl()` across all update/delete/read paths, per the recommended fix shape; CardDAV audited clean. Regression test RED→GREEN in `tst_remotecalendarbackend_convergence`. Suite 177/179 (identical pre-existing baseline). Consumers need no code change; bump the pin when convenient. Closure summary: FINDINGS **O54**. |
| ~~**O55**~~ | WildPalms (handoff, 2026-08-21) | TwoWay sync between a bare-id backend and the `GenericSqliteBackend` hub churns and empties the hub from pass 2 on — `perRecordDiff()` joins strictly by raw `BackendRecord::id`, no aliasing for the hub's `<collectionId>\x01<origId>` prefix. Silent data loss. | **RESOLVED 2026-08-22, shipped v1.00**: engine-side id aliasing (`WriteOperation::idAliases()`, `BaselineStore` schema v8 `blob_id_aliases`, resolved in `perRecordDiff()`), plus `EngineDiff::identityConflicts` fail-loud guard. Root cause: v0.77 converged by accident; B4 removed the accidental abort. RED→GREEN gate `tst_engine_id_aliasing`. Response: `docs/2026-08-22-o55-hub-record-id-aliasing-response.md`; closure: FINDINGS **O55**. |
| ~~**O56**~~ | WildPalms (followup handoff, 2026-08-22, at v1.00) | Recategorization scenario: after a hub-side edit + back-propagation, pass 2 misjoins again (phantom AskUser conflict with empty error AND phantom delete emptying the hub despite the unresolved conflict). Two defects: alias/baseline anchors crossed per-batch (dual rows, bidirectional aliases); destructive ops applied under an unresolved AskUser conflict. | **RESOLVED 2026-08-22, shipped v1.01** (branch `fix/o55-followup-recategorization`): anchor-stable persisting (chain-resolve to sink; crossings become no-ops; baselines key at sink), load-time heal for v1.00-poisoned stores (cycle-breaking sink resolution + baseline dedup — no manual recovery), and an all-or-nothing hold gate (`unifiedContinueAfterConflicts` writes NOTHING while any AskUser conflict defers unresolved). Three RED→GREEN slots in `tst_engine_id_aliasing`. Suite 180 total / 177 passing, identical pre-existing baseline. Pin bump only; PlanStan sees one behavior change (hold-gate semantics). Response: `docs/2026-08-22-o56-recategorization-followup-response.md`; closure: FINDINGS **O56**. |

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

## 7. Known pre-existing failures (NOT introduced by §3)

Updated 2026-08-25: the full suite is **191 passing / 195 total** (EEE
Tier A included). The four reds are environmental, verified PRE-EXISTING
(reproduce at pre-session `e1846a3`; KDAV 30s-transfer-timeout pattern vs
the local Radicale):

- **`tst_backend_signals`**, **`tst_backend_thread_relocation`**,
  **`tst_backend_reentrancy_pin`**, **`tst_remotecalendarbackend`** —
  live-Radicale-state-dependent; re-check when Radicale state resets.

(The third historical red,
`tst_calendar_canon_roundtrip::canonPersonalClassificationProducesPrivateAndStash`,
was RESOLVED 2026-08-23 as FINDINGS **O58** — a parameter-blind test assert;
production behavior was always correct.)
