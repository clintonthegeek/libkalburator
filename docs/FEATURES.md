# Feature status

**Last verified:** 2026-09-08
**Maturity labels:**

- **Integrated** — exercised by PlanStan or WildPalms production code.
- **Implemented** — substantial code and automated tests exist, but no complete consumer workflow.
- **Experimental** — partial, lab-oriented, conditional, or live-service coverage only.
- **Incomplete** — a known stub, null factory, missing persistence, or broken path prevents a complete workflow.

None of these labels currently means a stable external API.

The generated [CONVERGENCE_MATRIX.md](CONVERGENCE_MATRIX.md) records detailed cross-vendor transformation and declared-loss behavior directly from the shape registries.

## Synchronization capabilities

| Identity substrate | Maturity | Consumers | Important limits |
|---|---|---|---|
| Cross-domain `IdentityStore` and `PersonDirectory` | Implemented | PlanEngine requested; existing identity tests | Installed via `Kalburator::Identity`; no stable API promise during consolidation |

| Capability | Maturity | Consumers | Important limits |
|---|---|---|---|
| Two-way record reconciliation | Integrated through public runtime on connected and injected WildPalms paths | PlanStan, WildPalms | PlanStan management cutover remains |
| Upload/download/mirror overrides | Implemented in public runtime; WildPalms facade maps intents | Both | Remaining consumer cleanup is tracked by downstream tasks |
| Per-side hash baselines | Integrated | Both | Runtime-owned lifecycle is implemented; consumer cutovers remain |
| Conflict policies and deferred conflicts | Integrated | Both | Runtime commands handle newly detected conflicts; persisted backlog observation and consumer cutover remain |
| ID aliasing and healing | Integrated | Both | Complex and persistence-coupled |
| Shape graph and canonical transformation | Integrated | Both | Several loss declarations remain open |
| Multi-pass/fixpoint convergence | Implemented in public runtime; WildPalms uses one runtime call | Both | Consumer policy tuning remains |
| Skip-unchanged tokens | Integrated | Both | Requires failure-path regression coverage |
| Parallel mappings and resource caps | Integrated | PlanStan | Default and consumer policies differ |
| Cancellation and progress | Integrated through the public runtime facade | Both | Facade link-loss and active-teardown contracts pass; typed telemetry remains a consumer presentation concern |
| Secret references and host secure storage | Integrated | PlanStan, WildPalms | KWallet-backed host adapters; library fallback is process-local and test-oriented |
| Mass-deletion guard | Integrated | Both | Presentation supplied by consumers |
| Calendar property synchronization | Integrated | PlanStan path | Three-way baseline is reloaded; mapping policy and asynchronous apply failures are honored |
| Journal/recovery | Partial | PlanStan legacy path | PlanStan owns staged user-edit journaling and replay; runtime owns reconciliation stores and reset, with no inert CalendarJournal claim |
| `CollectionRuntime` facade | Implemented kernel; local/DAV vertical slice certified; connected WildPalms topology/run slice adopted | Contract tests; PlanStan factory slice; WildPalms fake-device routes | Execution/intents/convergence/canonical record events/new-conflict commands/typed provider observation/mapping snapshots/typed run telemetry/physical collection commands/teardown are proven; remaining consumer presentation cleanup remains |

## Domains

| Domain | Implemented representations | Consumer integration | Maturity |
|---|---|---|---|
| Calendar | Canon, iCalendar, Org/iCalendar, Google Event, Microsoft Event, Palm | PlanStan and WildPalms | Integrated core; vendor providers incomplete |
| Contacts | Canon, vCard 3/4, Google Person, Microsoft Contact, Palm | WildPalms | Integrated for Palm/hub; vendor provider workflow incomplete |
| Todo | Canon, VTODO, todo.txt, Google Task, Microsoft To Do, Palm | WildPalms | Integrated for Palm/hub; vendor provider workflow incomplete |
| Note | Canon/text/Markdown, Palm memo | WildPalms | Integrated for Palm/hub |
| Outline | Canon, OPML, Org | No current consumer workflow found | Experimental |
| Blob | Raw record model | Infrastructure used by both | Integrated substrate |

## Backends and providers

| Family | Backend implementation | Provider/provisioning | Consumer use | Maturity |
|---|---|---|---|---|
| Local calendar | Yes | Direct/application configuration | PlanStan | Integrated |
| CalDAV | Yes | CalDAV and multiprotocol DAV | PlanStan; WildPalms has incomplete remote-route wiring | Local/DAV public-runtime contract certified; production provider presentation/cutover remains |
| CardDAV | Yes | CardDAV and multiprotocol DAV | Configuration surfaces; limited end-to-end evidence | Implemented |
| DecSync | Yes | Direct | PlanStan | Integrated |
| Holiday/subscription | Yes | Direct | PlanStan | Integrated |
| Org calendar | Conditional | Host-owned adapter | PlanStan | Integrated but build ownership is circular |
| Akonadi | Conditional | Akonadi provider | PlanStan/WildPalms optional | Conditional |
| Palm calendar/contacts/todo/memo | Consumer implementations | Palm device session | WildPalms | Integrated; four-domain fake-device/runtime certification passes |
| Generic SQLite hub | Yes | No provider needed | WildPalms | Integrated |
| Filtered collection views | Yes | Programmatic | WildPalms routes | Integrated |
| Raw/Markdown files | Yes | Directly configurable concrete backends; not stock provider contributions | WildPalms-related paths | Consumer provisioning required |
| Google Calendar/People/Tasks | Yes | No stock end-user provider/account lifecycle | No consumer | Experimental |
| Microsoft Calendar/Contacts/To Do | Yes | No stock end-user provider/account lifecycle | No consumer | Experimental |

## UI and tooling

| Feature | Maturity | Note |
|---|---|---|
| Collection picker and provider widgets | Integrated | Compiled into the monolithic sync target |
| Conflict presentation | Integrated through consumer UIs | Runtime events/commands cover current-run conflicts; restart/rebind backlog and consumer cutover remain |
| Google/Microsoft lab CLIs and live probes | Experimental | Valuable verification tools, not consumer workflows |
| Fake DAV/vendor servers | Integrated | Reusable CalDAV/CardDAV fixtures are provided by `Kalburator::TestSupport` in test-enabled source builds |
| Test coverage report | Implemented | `test-report` distinguishes hermetic, optional, live-credential, consumer-contract, executed, and runtime-skipped coverage |
| Reference consumer | Implemented | In-tree engine seam and an external `find_package(Kalburator)` consumer build verified against the installed package |

## Known incomplete implementations

- `CollectionRuntime` owns factory endpoints, admits connected provider-owned
  endpoints, executes all run intents with bounded convergence, scopes external
  resources, and publishes canonical record events. Typed provider/discovery
  facts, unresolved-conflict replay, mapping/run/collection observation,
  PlanStan monitored-run policy, operational recovery, and atomic
  provider/persisted topology remain API-007, API-008, API-009, RUN-007,
  RUN-008, and TOP-004. Physical collection commands are implemented by
  TOP-005; consumer cutover remains outstanding.
- WildPalms connected and injected-backend runs use the library convergence
  coordinator; only an explicitly named conflict-store accessor remains for
  legacy fixture coverage.
- CardDAV contact collections are discovery-only; collection creation is explicitly unsupported and no create action is exposed.
- Raw-files and generic-SQLite remain explicit configured backends, not stock provider contributions; their null registry entries were removed.
- Snapshot restore was removed as an unused stub, and reconciliation reset/recovery is runtime-owned while PlanStan alone replays staged user-edit journals.
- Google and Microsoft implementations lack consumer-ready provider/configuration integration.
- Installed package and namespaced public headers exist for the exported targets; no stable API promise during consolidation.
