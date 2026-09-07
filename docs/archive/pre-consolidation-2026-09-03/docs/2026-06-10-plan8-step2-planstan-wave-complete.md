# PlanStan → libkalburator: Plan 8 step-2 consumer wave COMPLETE

**Date:** 2026-06-10
**From:** PlanStan dev
**Re:** `2026-06-10-plan8-isynchost-runsyncfuture-consumer-wave-rfc.md` (step 2)
and our ack (`2026-06-10-plan8-consumer-wave-response-planstan.md`).
**PlanStan state:** branch `feature/plan8-step2-consumer-wave`, built against
**v0.69**, full suite at the canonical 21-Not-Run baseline.

## Step 3 is UNBLOCKED from PlanStan's side

`grep -rn runSyncFuture src/ tests/` in PlanStan is **empty**. Detail:

- 8 live call sites → `runSync(SyncRequest)` (7 in `tst_sync_conflicts`, 1 in
  `tst_sync_caldav_conflicts`' shared `runSyncAndFlush`). Contract notes
  honored (QTRY wait; these tests never read results, so no
  `resultAt(0)` exposure). The caldav suite was compile- AND run-verified
  against the local Radicale: 8/10 pass; the 2 failures (firstSync,
  recurrenceRoundTrip) were A/B-isolated as pre-existing with the migration
  stashed — filed PlanStan-side, NOT a v0.68/v0.69 regression flag from us.
- 2 textual migrations in the stale `EXCLUDE_FROM_ALL` `tst_sync_dialog`
  (file has unrelated pre-existing API drift; not compile-verified, stated
  for honesty).
- 3 prod doc-comments + 1 test doc-comment reworded.

Remaining `runSyncFuture` consumers are WildPalms' two PROD calls (your
handoff `06fd77c` covers them) and your own `syncruncoordinator.cpp` internal
call — not ours.

## ISyncHost: nothing further needed from libkalburator

- Registration is now **unconditional** in CC (the gating asymmetry from our
  ack is gone; registry content ≡ CC's `m_backends` for calendar backends).
  CC also unregisters on teardown — registered instances no longer dangle in
  the app-owned registry after a collection closes.
- The `static_cast<SyncBackend*>` bridge in `mirrorProviderBackends` is now
  `dynamic_cast` (your FINDINGS "From Plan 3" hazard, host-side, closed).
- CC's `backendById`/`backends()` overrides REMAIN, per v0.69's explicit
  "hosts that carry their own backend storage keep their overrides" blessing —
  but they now have zero CC-internal consumers, so deleting them later is a
  contained change. Do not wait on us for any step-3 scheduling.

## One observation for your FINDINGS, take or leave

While widening registration we hit (and fixed PlanStan-side) a teardown
ordering trap worth a line in the campaign notes: a host whose
`BackendRegistry` and controller are QObject siblings under one parent gets
creation-order child destruction — the registry (created first) dies before
the controller, so any controller-destructor interaction with the registry
must tolerate a dead pointer (we null via a `destroyed()` connection).
`ProviderManager::~ProviderManager → disconnectAll → unregisterProviderBackends`
has the same shape if a provider is still connected at app exit and the
manager outlives the registry — may be worth an audit-supplement glance.
