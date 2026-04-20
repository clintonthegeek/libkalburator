# libkalburator

**Status:** Phase 0 — cross-project alignment. No source code yet.

A portable Qt6/C++ calendar synchronization library: multi-backend
two-way sync across CalDAV, local iCal, org-mode, Akonadi, DecSync,
web subscriptions, and more, with conflict detection, crash-recovery
journaling, and lossy-format transcoding preservation.

Extracted from and designed to be shared between:

- **[PlanStan](../PlanStan/)** — personal-PM / calendar app that
  pioneered the abstractions (`libs/sync/`).
- **[Wild Palms](../WildPalms/)** — Palm OS sync tool that will
  offer a "Full Sync Mode" profile where it becomes a first-class
  multi-backend calendar app in its own right, alongside its
  existing Palm-driver "Client Mode".

See [the proposal in PlanStan](../PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md)
for full motivation, UX design, and six-phase plan.

## Current phase

**Phase 0 — cross-project alignment.** Producing the merged interface
design doc. No code committed yet; Phase 1 (PlanStan-side extraction)
is gated on Phase 0 output.

Phase 0 deliverables live in `docs/phase0/`:

- `01-inventory-planstan.md` — inventory of PlanStan's `libs/sync/`
- `02-inventory-wildpalms.md` — inventory of Wild Palms' `src/sync/`
  + `src/sync/qsynccore/`
- `03-conflict-engine-audit.md` — generic-vs-Palm-fit audit of Wild
  Palms' conflict engine
- `04-merged-interface-sketch.md` — the reconciled library surface
  with provenance annotations
- `05-repo-strategy.md` — naming, subtree-split vs standalone,
  versioning policy
- `00-open-questions.md` — decisions deferred past Phase 0

## Name etymology

"kalburator" = `KCalendarCore`-fuel + carburettor. The library mixes
backend data streams, reconciles conflicts, and feeds a clean unified
calendar stream into host applications. Also because the name was
available and the user says it's pleasing.

## License

Not chosen yet (Phase 0 deliverable). Current host projects:
- PlanStan: GPLv3
- Wild Palms: GPLv3

LGPL / GPL / MPL2 / Apache-2 trade-offs are in `docs/phase0/05-repo-strategy.md`.

## Contributing

Not accepting contributions during Phase 0. The project is design-only
and owned by the maintainer for this phase.
