# ADR 0008: Recurrence override identity and family assembly

- **Status:** Accepted
- **Date:** 2026-09-11
- **Supersedes:** the CalDAV and LocalBackend clauses of the W1 detached-exceptions
  contract (`docs/archive/pre-consolidation-2026-09-03/docs/campaign/vtodo-parity/`,
  2026-08-26). The composite-identity clauses of W1 stand unchanged.

## Context

A recurring series is a master component plus zero or more detached overrides,
each distinguished by `RECURRENCE-ID` and sharing the master's `UID`. The
backends we target do not agree on what the unit of storage is.

| system | unit of storage | how an override is addressed |
|---|---|---|
| CalDAV (RFC 4791) | the UID family, one resource | not addressable; a component inside the resource |
| vdir / local files | the UID family, one file | not addressable; same |
| Google Calendar | one event per override | own id, formed as `<masterId>_<originalStartUtc>` |
| Microsoft Graph | one event per override | own id, plus `seriesMasterId` |

RFC 4791 §4.1 requires that components sharing a UID "MUST be contained in the
same calendar object resource", and §5.3.2.1's `CALDAV:no-uid-conflict`
forbids a second resource reusing a UID. This is enforced in practice, not
merely specified: our own Radicale rig answers 409 `no-uid-conflict` to a
second resource carrying an already-used UID, and Sabre (Nextcloud, Baïkal)
does the same.

W1 chose per-override composite record ids, which matches Google and Graph
natively — Google's instance id is literally the same construction — and also
matches where IETF `calext`'s series draft points. That choice was right. What
W1 got wrong was the transport rule it derived for CalDAV: minting a distinct
`<uid>-<stamp>.ics` href per override. That was validated against this repo's
`FakeCalDavServer`, which has no UID-conflict check at all and whose source
comment encodes "several resources may share one UID" as a premise. Against
any real server it is a 409.

Two write paths exist today and both mishandle overrides, in different ways:

- The staging flush (`StagingController` → `SyncBackend::startSync()`) writes
  each incidence separately, addressed by bare UID, on both
  `LocalBackend` and `RemoteCalendarBackend`. A master and its override
  therefore collide on one filename or one href and one is silently destroyed.
- The record path (`applyRecords()`) mints the distinct-href form above, which
  real servers reject.

Neither has ever run against a real server carrying a real override.

## Decision

**1. The per-override record stays canonical.** A record id is a bare UID for a
master and `composeRecordIdentity(uid, recurrenceId)` for an override, exactly
as W1 specified. Family assembly is pushed down into the backends whose
transport demands it, namely CalDAV and local files. It is a first-class,
tested seam in those backends, not an incidental helper.

**2. Apply is family-ordered.** The engine guarantees that within one UID
family the master is written before its overrides, and deletes are applied
last. This is not an optimization: neither Google nor Graph permits creating
an override at all. Both require modifying a server-materialized occurrence,
which cannot exist before the master does.

**3. Conflict granularity stays per-override.** On CalDAV, where an ETag covers
the whole resource, the backend performs read-modify-write under `If-Match`
and retries on 412. The precision Google and Graph give for free is not thrown
away to make CalDAV simpler.

**4. Orphan overrides are preserved verbatim.** An override whose master is
absent is legal under RFC 4791 and common in practice. It is carried through
and surfaced as a standalone occurrence. No master is ever synthesized, because
that would write data the user did not author.

**5. `RANGE=THISANDFUTURE` is never emitted, on the event side as well as the
todo side.** A this-and-future edit is realized as a split: tighten the old
master's `UNTIL`, create a new master, and rebase later overrides onto it.
This matches Google's own documented behaviour for the same operation.

**6. Divergence is reported, not prevented.** An edit that one backend accepts
and another rejects — Graph's `ErrorOccurrenceCrossingBoundary` has no CalDAV
analogue — is attempted on every leg and reported per leg. We do not validate
user edits up front against the strictest backend a calendar happens to be
bound to.

## Implementation status (2026-09-11)

**Done and pinned.** `LocalBackend` now writes one UID family per `.ics` file.
`startSync()` groups staged writes by UID and merges into whatever the file
already holds, because a flush routinely carries only part of a family.
`removeItem()` removes a single component and rewrites the rest, so deleting
one occurrence no longer destroys its master, and an override whose master is
deleted survives as an orphan. `AsyncFileWriter` gained a family write.
Verified by `tests/backends/tst_localbackend_write_batching.cpp` (no network,
no env gate) and live: RRD-014's detached exception now durably survives the
full mutation battery, which it never did before.

**Open, with a named blocker.** CalDAV family assembly is NOT implemented.
An attempt was built and backed out after measurement, and the reason matters
more than the attempt:

> The engine transcodes every record through canon on its way between
> backends, and a canon object represents ONE incidence. By the time a record
> reaches `RemoteCalendarBackend`, its bytes are a single re-serialized
> component, not the family it came from. Family assembly therefore cannot
> live only in the backend — the family has already been taken apart upstream.

That reframes decision 1's "push assembly down into the backend". Assembly is
still the right idea, but the assembly point has to be somewhere the family is
still whole, or canon has to carry enough for the writer to rebuild it. Three
candidates, none yet chosen:

1. Give the writer the family by having the engine group a batch by UID before
   it hands it over, so the backend receives whole families rather than
   reconstructing them.
2. Keep per-record transcode and have the CalDAV writer re-read the resource
   and merge each component into it, which is a true read-modify-write and
   costs a round trip per family.
3. Carry the family in canon, which contradicts "a canon object is one
   incidence" and is listed only for completeness.

A second finding from the same attempt: the local and remote record layers
disagree on granularity. `RemoteCalendarBackend` emits one record per
component; `LocalBackend` emits one per file, per W1's deliberate exemption.
Compounding the local side to match was tried and made things worse on its own,
because the local record WRITE path still names files by raw record id. Both
sides must change together, and only once the assembly point above is settled.

Until then `tst_rrd014_mutations::recurrenceIdentitySurvivesEveryMutation()`
asserts the local half and carries the remote half as a `QEXPECT_FAIL`, so the
gap is visible in every run and trips loudly when it starts working.

## Consequences

- `LocalBackend::startSync()` and `RemoteCalendarBackend::startSync()` must
  group staged writes by UID and write the whole family as one unit. Because a
  flush may carry only part of a family, both must read the existing resource
  and merge rather than overwrite.
- `RemoteCalendarBackend` gains an `If-Match` retry loop. The CalDAV writer is
  no longer a blind PUT.
- `applyRecords()`' distinct-href minting for overrides
  (`generateItemUrlForCreate`) is wrong against real servers and must converge
  on the same family assembly.
- `FakeCalDavServer` must enforce `no-uid-conflict`, or it will keep certifying
  designs that real servers reject.
- The engine needs a notion of a UID family to order applies. Per-record diffs
  are unaffected; only apply ordering changes.
- Google and Microsoft Graph calendar backends are not registered in PlanStan's
  `AppController::seedBuiltinContributions()`, so they are a design constraint
  rather than a live one. There is no commitment to exposing them, and they
  should not be wired until family-ordered apply exists.
- The event-side `RANGE=THISANDFUTURE` emitter in `eventcanonfields.cpp` must
  be changed to the unconditional-false form already applied on the todo side,
  and pinned.
