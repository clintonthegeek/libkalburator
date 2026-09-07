# Phase 0 — Repository, naming, versioning, licensing

**Status:** First-draft decisions pending maintainer confirmation.

## Name

**`libkalburator`** — confirmed by maintainer.

Namespace: `Kalburator::Sync::*` for all library types. Rationale: the
project may later grow a separate transport module or a Syncthing-
detection module; those would be `Kalburator::Transport::*`,
`Kalburator::Discovery::*` etc., all under the `Kalburator::` umbrella.

Target (CMake alias): `PlanStan::Kalburator` ... actually no — the
whole point is that the library is not PlanStan-owned. Use
`Kalburator::Sync` as the alias target, following CMake convention
where the alias namespace matches the project name.

## Repository location

**Phase 0 (now):** `~/dev/libkalburator/` with a local-only git
repo. Docs-only. No source yet.

**Phase 1–3 (extraction through smoke test):** stays at
`~/dev/libkalburator/` with source being written directly here. The
extraction is not a `git subtree split` from PlanStan; it is a
**rewrite with reference to PlanStan's existing source**, because the
reconciliation with Wild Palms' abstractions (layered architecture,
conflict framework, namespace change) means file contents diverge
enough that a literal subtree-split would miss the point.

PlanStan's `libs/sync/` stays in the PlanStan tree during Phase 1–3
and continues to work. Only after Phase 3 lands does PlanStan swap
its in-tree `libs/sync/` for a `FetchContent_Declare` or
`find_package` consumption of libkalburator.

**Phase 4+ (when publishable):** decide between:

1. **Stay at `~/dev/libkalburator/`** as a standalone git repo,
   pushed to a public forge (GitHub? KDE GitLab? Codeberg?).
2. **Adopt under a KDE-adjacent umbrella** — e.g. KDE Invent as a
   playground / incubator project if the KDE community expresses
   interest. Would require adopting KDE's CI, coding standards, CLA
   if any.
3. **Private forge**, publish-when-ready.

Deferred to Phase 4; Phase 0 only needs "it exists as a git repo at
`~/dev/libkalburator/`".

## License

**Proposal: LGPL-3.0-only** (with possible LGPL-2.1-or-later as a
fallback for KDE-ecosystem compatibility).

Rationale:

- Both host projects (PlanStan, Wild Palms) are GPLv3. LGPL keeps them
  compatible.
- LGPL allows commercial / closed-source apps to link the library,
  which is important for long-term library adoption beyond the two
  named consumers.
- KF6 and Qt6 are LGPL; following their convention reduces friction
  for KDE-ecosystem consumption.
- MPL-2.0 or Apache-2.0 are alternatives with similar permissiveness
  but less KDE-idiomatic.

**Do not** pick plain GPL: blocks the library from being used in
closed-source apps, which is almost always a mistake for an
infrastructure library. Rich potential consumers like Thunderbird /
Element / proprietary calendar clients would be locked out.

## Versioning policy

Semantic versioning (MAJOR.MINOR.PATCH). During Phase 1–5:

- Pre-1.0 — interface may break between minor versions; consumers
  pin exact version.
- 1.0 declared when Wild Palms' Full Sync Mode ships (Phase 4).

Post-1.0:

- MAJOR: `ICalendarBackend`, `IBlobBackend`, `ICalendarHost`, or
  `ICalendarCollection` surface breaks.
- MINOR: new backends, new methods on `...ConfigStore`, new
  optional fields on records.
- PATCH: bug fixes, internal changes.

PlanStan and Wild Palms should hold compatible MAJOR versions at all
times. A flag day to upgrade both together is acceptable for MAJOR
bumps; MINOR / PATCH should be seamless.

## CMake consumption model

Consumers use `find_package(Kalburator REQUIRED COMPONENTS Sync)`.

The library installs:

- `KalburatorConfig.cmake` + `KalburatorTargets.cmake` for `find_package`.
- Headers under `${includedir}/kalburator/sync/`.
- Shared lib as `libKalburatorSync.so` (following KDE naming).

Before the first external release, consumers can use
`FetchContent_Declare` pointing at a commit hash or a local path:

```cmake
FetchContent_Declare(Kalburator
    GIT_REPOSITORY /home/clinton/dev/libkalburator
    GIT_TAG        main)
FetchContent_MakeAvailable(Kalburator)
```

## Build system

- CMake 3.19+ (matches PlanStan + Wild Palms).
- `CMakePresets.json` with `dev`, `release`, and `coverage` presets.
- `PROJECT_IS_TOP_LEVEL` gating for in-tree vs standalone builds —
  the same pattern PlanStan uses for its internal libraries.
- ECM (KDE Extra CMake Modules) for consistency with KF6 idioms.
- `ctest` for the test suite. All tests run under
  `QT_QPA_PLATFORM=offscreen`.

## Directory layout (to be created in Phase 1)

```
libkalburator/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSES/
│   └── LGPL-3.0-only.txt
├── cmake/
│   └── KalburatorConfig.cmake.in
├── docs/
│   ├── phase0/             (this directory)
│   ├── api/                (Doxygen output, phase 1+)
│   └── guides/             (host-integration guides, phase 3+)
├── src/
│   ├── CMakeLists.txt
│   ├── blob/               (lower layer)
│   │   ├── backendrecord.{h,cpp}
│   │   ├── iblobbackend.{h,cpp}
│   │   ├── blobsyncengine.{h,cpp}
│   │   ├── localblobbackend.{h,cpp}
│   │   └── mockblobbackend.{h,cpp}
│   ├── calendar/           (upper layer)
│   │   ├── icalendarbackend.{h,cpp}
│   │   ├── icalendarhost.{h,cpp}
│   │   ├── icalendarcollection.{h,cpp}
│   │   ├── calendarsyncengine.{h,cpp}
│   │   ├── localcalendarbackend.{h,cpp}
│   │   ├── caldavbackend.{h,cpp}
│   │   ├── orgbackend.{h,cpp}
│   │   ├── akonadibackend.{h,cpp}  (optional)
│   │   ├── decsyncbackend.{h,cpp}
│   │   ├── subscriptionbackend.{h,cpp}
│   │   ├── holidaybackend.{h,cpp}
│   │   └── mockcalendarbackend.{h,cpp}
│   ├── conflict/
│   │   ├── conflictrecord.{h,cpp}
│   │   ├── conflictstore.{h,cpp}
│   │   ├── conflictpolicy.{h,cpp}
│   │   ├── conflicthandler.{h,cpp}
│   │   └── automaticconflicthandler.{h,cpp}
│   ├── transcoding/
│   │   ├── propertytranscoder.{h,cpp}
│   │   ├── rruletranscoder.{h,cpp}
│   │   ├── transcodingregistry.{h,cpp}
│   │   ├── incidencediff.{h,cpp}
│   │   └── syncdiff.{h,cpp}
│   ├── journal/
│   │   ├── calendarjournal.{h,cpp}
│   │   ├── baselinestore.{h,cpp}
│   │   ├── idmappingstore.{h,cpp}
│   │   └── asyncfilewriter.{h,cpp}
│   └── types/
│       ├── backendconfiguration.{h,cpp}
│       ├── logicalcalendar.{h,cpp}
│       ├── syncmapping.{h,cpp}
│       ├── calendartype.h
│       ├── datadomain.h
│       └── synctypes.h
├── tests/
│   ├── CMakeLists.txt
│   ├── blob-smoke/
│   ├── calendar-smoke/
│   ├── caldav-against-radicale/
│   ├── conflict-resolution/
│   └── fixtures/
└── examples/
    ├── minimal-calendar-host/
    └── minimal-blob-host/
```

## Not-in-scope for Phase 1 (deferred)

- Doxygen API docs — Phase 3+.
- Integration guides (how to write an `ICalendarHost`, how to add a
  new backend) — Phase 3+.
- Akonadi resource wrapping libkalburator — a separate project, not
  libkalburator itself.
- D-Bus service wrapping libkalburator — a separate project.

## Dependency policy

Hard dependencies:

- Qt6 (6.8+) — Core, Sql, Network.
- KF6 CalendarCore — the incidence model.
- KF6 DAV — for the CalDAV backend.
- KF6 KIOCore — for KIO-backed file operations.
- KF6 Holidays — for holiday subscriptions.

Optional dependencies:

- KPim6 AkonadiCore — for the Akonadi backend (`HAVE_AKONADI`).

Nothing else. In particular, **no** dependency on:

- KDE Frameworks beyond the six listed.
- boost, abseil, fmt — stick to Qt + std.
- KWallet, KCrashHandler — hosts supply their own.

## Stewardship

Phase 0 owner: Clinton (maintainer).
Phase 1–3 owner: Clinton (as part of PlanStan's roadmap).
Phase 4+ governance: TBD; likely benevolent-dictator (Clinton) until
a second contributor appears.

Contributions from the Wild Palms side during Phase 4 are expected:
Wild Palms adopting the library will reveal real bugs and feature
gaps; those are upstreamed as PRs.

## Tracking

- This repo's own `main` branch carries Phase 0 docs.
- Phase 1 work lands on feature branches, merges to `main` when
  green.
- Phase 1–3 should ship as git tags (`v0.1-phase1`, `v0.2-phase2`,
  `v0.3-phase3`, `v1.0` at Phase 4 completion).
- PlanStan tracks the integration via `docs/superpowers/plans/…` and
  the eventual PR that swaps `libs/sync/` for a `find_package`.
- Wild Palms tracks the integration in its own roadmap.
