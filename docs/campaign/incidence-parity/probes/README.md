# Incidence-parity audit probes

Two standalone programs that reproduce, against the real tree, every claim
in `../2026-09-02-preflight-audit.md`. They exist so that no agent picking
up this campaign has to re-derive the evidence — or, worse, re-derive it
badly and file a finding that turns out to be a fixture artifact.

```sh
./run.sh            # builds both, prints both reports
./run.sh /some/dir  # same, binaries land in /some/dir
```

Requires a populated `build/` (`cmake -S . -B build && cmake --build build`).

## The two programs, and why they are two

| File | Links | Answers |
|---|---|---|
| `kcalendarcore-probe.cpp` | KCalendarCore only | "Is this defect ours or the toolkit's?" |
| `incidence-audit-probe.cpp` | `build/libkalburator.a` | "What does our pipeline actually do to a maximal component?" |

Keeping them separate is the point. O86 (GEO) reproduces in the first
program with no libkalburator in the picture, which is what makes it an
upstream defect we must *work around* rather than a bug we can fix in an
emitter. O79 and O85 do **not** reproduce there — KCalendarCore parses all
four VALARM trigger forms correctly and reports `enabled=1` — which is what
makes them ours.

## Deliberately not CMake targets

These are evidence instruments, not product. `run.sh` compiles them against
the already-built static libs instead of adding targets, so they cannot
accrete into the build graph, cannot break CI, and cannot be mistaken for
gates. **They are not gates.** The gate this audit argues for is IP.8's
round-trip fidelity gate, which belongs in `tests/` as a QTest slot. If you
find yourself wanting to make a probe fail the build, you want IP.8 instead.

## Two traps these probes encode

Both cost time during the audit; both are recorded in code comments so they
cost nobody else any.

1. **libical rejects single-label mail domains.** `mailto:a@x` makes the
   entire `ATTENDEE` property vanish at parse time. A probe written with
   `a@x` "discovers" that attendees never round-trip — a false alarm. Use
   `example.com`. Section B of the KCalendarCore probe pins this.

2. **You must unfold before parsing.** KCalendarCore folds at 75 octets and
   an `ATTENDEE` line routinely folds *before its colon*, so a per-line
   parse reports `ATTENDEE` lost and the continuation fragment
   (`CUTYPE=INDIVIDUAL`) as a spurious new property. The first revision of
   `incidence-audit-probe.cpp` had exactly this bug and briefly produced a
   wrong loss list. `icalPropertyNames()` now unfolds first.

## Recorded baseline

`kcalendarcore 6.29.0-1`, Qt 6.11.1, GCC 16.1.1, Manjaro, 2026-09-02.
Suite at time of audit: 214 slots, 210 green, 4 environmental
(Radicale/KDAV). If a probe's output diverges from the audit doc and the
tree has not changed, suspect a toolkit upgrade first and say so in your
receipt — O86 in particular is a property of KCalendarCore's version, not
of ours.
