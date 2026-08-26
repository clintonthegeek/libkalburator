# P3 — Todo backends + kind-demux: status

**Status:** IN PROGRESS (design pass pinned 2026-08-25; implementation
next).

Goal (proposal §4 P3): `GoogleTasksBackend` + `GraphTodoTaskBackend`,
plus the **kind-demux deliverable** (mixed DAV collections surface as TWO
ProviderBackendSpecs). Exit: fixture-promotion corpora replayed through
the real backends; live checkpoints per invariant 1. P3 also GATES the
vtodo-parity campaign (`docs/campaign/vtodo-parity/STATUS.md`) — these
backends are its W1/W2 Google/MSToDo test legs.

## Design decisions (pinned 2026-08-25, from the design pass)

- **File placement:** `src/todo/googletasksbackend.{h,cpp}` +
  `src/todo/graphtodotaskbackend.{h,cpp}`; test bed `tests/todo/`
  (`mockgoogletasksserver`, `mockgraphtodoserver`, backend suites, live
  suites) — vendor mock libs live in the domain dir per the
  tests/contacts precedent.
- **Base URLs are VERSION-LESS** (O71 house rule): Google Tasks base
  `https://tasks.googleapis.com`, paths author `/v1/...` verbatim;
  Graph base `https://graph.microsoft.com`, paths `/me/todo/...`.
- **Envelopes fit stock clients:** Tasks listings return `{items[],
  nextPageToken?}` ⇒ `GoogleApiClient::fetchCollection` unchanged;
  todoTask listings return `{value[]}` ⇒ `GraphApiClient::fetchCollection`
  unchanged. No People-style rawRequest walk needed anywhere.
- **GoogleTasksBackend:**
  - Discovery: GET `/v1/users/me/lists`; each taskList = collection.
    List ids may contain colons (wire notes §2) — ids go in PATHS
    verbatim (path segment, never query).
  - Reads: FULL paged listing every fetch (`showCompleted=true&showHidden=true`
    so completed + deleted surface); **NO sync tokens — Tasks API has
    none** (recon: zero in-repo precedent; do not invent one).
    Tombstones = items with `deleted:true` ⇒ cache remove. Report FULL
    merged set per engine diff contract.
  - Record id = server task id. Write seams strip `created`/`updated`/
    `id` at create (O68 generalization); PATCH in place; delete accepts
    204/200, 404-as-success (idempotent). addIdAlias bridged on minted
    ids.
  - NO carrier channel (declared "none exists", O66(c)) — demote's
    Dropped rulings stand; honest losses. `position`/`parent` ride
    canon read-only in v1 (no move support).
  - Due degrades to midnight-UTC `.000Z` (stage-owned; loss profile row).
- **GraphTodoTaskBackend:**
  - Discovery: GET `/me/todo/lists` (wellknownListName recorded);
    collection paths `/me/todo/lists/{id}/tasks`. Ids ending `=` NEVER
    URL-encoded in paths (O66(d)).
  - Reads: **expanded full listings, never delta** (O70-family: delta on
    todoTasks is unprobed AND `$expand` on change tracking is already
    proven broken for contacts; expanded listing is the single
    record+carrier surface):
    `$expand=extensions($filter=Id eq 'Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon')`.
    Union-merge enrichment over cached rich copies (listing authoritative,
    GraphContactsBackend semantics).
  - Writes: create strips `extensions[]` (inline-create = WIRE-LIE,
    echoed-not-persisted) → POST → nav POST carrier rows to
    `/me/todo/lists/{id}/tasks/{taskId}/extensions` (UPSERT per O73);
    never trust create echo. Update = PATCH plain fields only; carrier
    changes routed through nav channel.
  - **dueDateTime-required-with-recurrence rule:** a create/update whose
    demoted body carries `recurrence` but no `dueDateTime` FAILS LOUD
    (typed op failure naming the rule) — we do not fabricate dates
    (no silent best-effort writes). Server-side dueDateTime rewrite when
    recurrence present (O66(b)) is tolerated by construction: next
    expanded listing delivers server truth.
  - Delete mirrors GraphContactsBackend: 204/200 success; 404 triggers
    one confirming re-list (gone ⇒ success, present ⇒ fail loud).
- **Kind-demux (MultiProtocolDavProvider):**
  - `createBackends()` partitions discovered cal collections by content
    types: VEVENT/VJOURNAL-capable ⇒ existing `"cal"` spec; VTODO-capable
    ⇒ NEW `"todo"` spec. ProviderManager's spec loop registers
    `<providerId>:todo` with zero consumer changes (verified: it iterates
    N specs).
  - Both specs host FILTERED VIEWS over the shared `RemoteCalendarBackend`
    via `Sinks::FilteredCollectionBackend` (borrowed views; disjoint
    record sets ⇒ no write contention; hrefs stay stable).
  - **⚠ Filter gap flagged:** RecordFilter predicates speak canon-JSON;
    CalDAV records are RAW iCal bytes ({calendar, ical}). The demux view
    must discriminate VEVENT vs VTODO in raw bytes (component-kind sniff,
    icalcomponentscan precedent). Extend FilteredCollectionBackend/record
    filtering minimally WITHOUT breaking existing sinks tests; kind
    dispatch precedent lives in `icalcanonstages.cpp:42-68`.
  - Vendor calendar backends stay hard-pinned `supportsVTodo=false` /
    contentTypes={VEVENT} (already true).
- **Registration scope:** P3 ships backends+mocks+tests only; provider/
  contribution remains P4.

## Reusable assets (do not rebuild)

- Transport/auth: `GoogleApiClient` + `GraphApiClient`; auth via
  `src/google/googleauth.h` / `src/graph/graphauthenticator.h`;
  token caches under KALBURATOR_{GOOGLE,MSGRAPH}_DIR.
- Edge stages landed + loss profiles declared:
  `src/todo/googletaskcanonstages.*`, `src/todo/mstodotaskcanonstages.*`;
  fixtures `tests/fixtures/vendor/google/task-*`,
  `tests/fixtures/vendor/microsoft/todo-*` with promotion slots in
  `tests/todo/tst_{google_task,ms_todotask}_canon_edge.cpp`.
- Backend templates: `GoogleCalendarBackend` (Google side),
  `GraphContactsBackend` (Graph side — expanded-listing + nav-carrier
  discipline is exactly this phase's shape).
- Live-suite pattern: tests/contacts/*_live.cpp (QSKIP without creds;
  CORPUS probes; unconditional cleanup; sweep-clean after).

## Checklist

- [x] Design pass (2026-08-25): stage shapes, wire rules (O66/O67/O68/O73),
      client-envelope fit, demux machinery + filter-gap flag — decisions
      above
- [x] P3.b: `MockGoogleTasksServer` + `MockGraphTodoServer` + transport
      pins (landed 2026-08-25 — Tasks double w/ maxResults pagination +
      default-omits-completed/deleted semantics + O68 400-rejections;
      todoTask double w/ expand-on/off, inline-create wire-lie (echoed,
      not stored), nav-POST UPSERT, PATCH-with-extensions ⇒ 500; 10+12
      slots green)
- [x] P3.c: `GoogleTasksBackend` (landed 2026-08-25 — tasklist discovery
      w/ supportsVTodo DTO surfacing; full paged listings every fetch
      w/ showCompleted+showHidden pinned; deleted:true tombstones; O68
      strip seam (id/created/updated); alias bridging; PATCH-in-place;
      404-delete-as-success (no re-list — Tasks not flaky); atomic cache
      persistence, no token (API has none). 9 slots green)
- [x] P3.d: `GraphTodoTaskBackend` (landed 2026-08-25 — expanded full
      listings never delta; union-merge enrichment; wire-lie-safe create
      (extensions stripped, nav-POST carriers, alias bridged to '=' ids);
      O66(b) gate fails LOUD pre-network on recurrence-without-due for
      create AND update; PATCH-in-place; 404-then-relist deletes;
      wellknownListName discovery; atomic cache persistence. 11 slots
      green)
- [ ] P3.e: kind-demux multi-spec split + suites (incl. raw-bytes filter)
- [ ] P3.f: live checkpoints both vendors (invariant 1) + fixture corpora
      replay through real backends

## Open questions

1. (P3.e) Does FilteredCollectionBackend's filter operate on raw bytes at
   all, or canon-JSON only? Resolve during implementation; extension must
   keep sinks suite green.
2. (P3.f probe) Do Google Tasks creates reject any fields beyond
   created/updated/id (e.g. etag)? Observe at live checkpoint; extend
   strip seam only on evidence.
