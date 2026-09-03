# Return receipt — IP.5: `providerExtrasDigest` as an envelope-level service

**Delivered:** 2026-09-02
**Consumes:** `docs/campaign/incidence-parity/PLAN.md` §1 (execution
rules, binding) and the IP.5 body section; `docs/campaign/FINDINGS.md` O80
(the defect this item closes); `src/shape/canonenvelope.{h,cpp}`'s
existing `canonicalDigest()`; the three todo call sites
(`vtodocanonfields.cpp:452-467`, `googletaskcanonstages.cpp:145-161`,
`mstodotaskcanonstages.cpp:395-432`); `tests/todo/tst_google_task_canon_edge.cpp:138-160`
(the volatile-filter pin model); real captured payloads under
`msgraph/captured/` and `google/captured/` (machine-local, gitignored,
referenced in place at the absolute paths — cited by filename below).
**Scope discipline — files touched:** `CLAUDE.md`,
`docs/campaign/FINDINGS.md` (O80 → RESOLVED; new O97 filed),
`docs/campaign/incidence-parity/STATUS.md` + this receipt;
`src/shape/canonenvelope.{h,cpp}` (new helper);
`src/todo/{vtodocanonfields,googletaskcanonstages,mstodotaskcanonstages}.cpp`
(retrofit); `src/calendar/{eventcanonfields,journalcanonfields,
mseventcanonstages,googlecanonstages,icalcanonstages}.cpp`,
`src/calendar/calendarstockshapes.cpp` (read-only, investigated for O97 —
NOT edited); `src/contacts/{vcardcanonstages,mscontactcanonstages,
googlepersoncanonstages,contactsstockshapes,contactscanonproperties}.cpp`;
`docs/campaign/eee/CONVERGENCE-MATRIX.md` (regenerated, 8 new rows);
test files listed in §6. No `.h` file gained a new public type beyond
`canonenvelope.h`'s one new declaration.

---

## 0. Summary

O80: `CanonJsonDiffer` is catalogue-scoped and `providerExtras` is
deliberately never catalogued, so a sync whose only change is a vendor
X-property or provider-extras edit on an **event, journal, or contact**
did not dirty the differ and did not propagate — exactly what O74 already
fixed for the todo domain, and exactly what O74's own text predicted
would recur ("same shape presumably holds for any domain whose differ is
catalogue-scoped").

One new envelope-level helper, `CanonEnvelope::stampProviderExtrasDigest()`,
now backs all ten promote sites across all three affected domains
(calendar, contacts, todo) — the three todo sites retrofitted onto it,
seven new calendar/contacts sites wired to it. Its signature takes the
**raw, unwrapped, pre-wrap extras object**, not PLAN.md's literal proposal
of the already-wrapped `providerExtras` value — §1 below argues this in
detail; it was decided by reading all three todo call sites' actual data
flow before writing any new code, not by guessing.

The volatile-key lists for the four new vendor legs (MS event, MS
contact, Google event, Google person) were derived from real captured
payloads, per the house rule that an unfiltered digest is worse than no
digest — §2 has the full evidence table, including one finding that
overturned my own initial guess (Google Person's `metadata` subtree
turned out to be genuinely edit-correlated, not per-request bookkeeping —
verified with a real no-edit two-fetch comparison, not assumed).

## 1. Helper design and signature

```cpp
// src/shape/canonenvelope.h
void stampProviderExtrasDigest(QJsonObject& obj, const QJsonObject& rawExtras,
                               const QStringList& volatileKeys = {});
```

**Deviates from PLAN.md's literal proposal**
(`stampProviderExtrasDigest(QJsonObject& obj, const QStringList&
volatileKeys)`, reading `obj[providerExtrasKey()]`) — a deliberate,
argued change, decided before writing any call site by reading the
mechanics of all three existing todo sites closely (the task brief
flagged exactly this tension and asked for a decision, not a rubber
stamp of PLAN's text):

- **All three existing sites compute the digest over the UNWRAPPED,
  filtered extras object** — `vtodocanonfields.cpp` hashes `xvtodo`
  directly (that IS what gets inserted as bare `providerExtras`, so no
  daylight between "wrapped" and "unwrapped" there, but still the raw
  object, never re-read back off `obj`); `googletaskcanonstages.cpp`
  hashes `filtered` (derived from `extras`, the object about to be
  wrapped as `{"google": extras}`); `mstodotaskcanonstages.cpp` hashes
  `filtered` (derived from `extras`, about to become `{"msgraph":
  extras}`). None of the three ever reads back `obj[providerExtrasKey()]`
  to compute the digest.
- A signature that DID read `obj[providerExtrasKey()]` would face a
  choice with no good answer: either the function has to know/guess the
  vendor's wrapper sub-key name (`"google"`/`"msgraph"`/`"x-vcard"`/
  `"x-ical"`/`"x-vtodo"`) to unwrap it — coupling a domain-neutral
  envelope helper to a naming convention it has no business knowing about
  — or it hashes the WHOLE wrapped value including that stable wrapper
  key, which just adds dead, constant weight to every hash for no benefit
  (the wrapper key never changes, so it never affects whether the digest
  is stable or dirty — it is pure overhead).
- Taking the raw object as a parameter is strictly simpler, matches every
  real call site's actual data flow exactly (verified, not assumed — see
  §3), and keeps the function honestly ignorant of `providerExtras`
  wrapping conventions, which differ per leg (bare insert for CalDAV/
  vcard, one level of sub-key wrap for MS/Google) and are none of this
  function's business.

The function does **not** touch `providerExtras` itself — the caller
wraps (or doesn't) however it already does, then calls this with the same
raw object. It inserts `providerExtrasDigest` into `obj` only when the
filtered extras are non-empty, matching every pre-existing call site's
"nothing to fingerprint ⇒ stamp nothing" behaviour — this matters because
the MS/Google demote-side generic carrier loops (§4) treat an absent
`providerExtrasDigest` exactly like any other absent optional canon key.

## 2. Volatile-key derivation — evidence per vendor leg

House rule (O80, restated in the task): *an unfiltered digest is worse
than no digest — spuriously always-dirty defeats the whole point.* Every
list below was derived from real captured payloads at the absolute paths
given in the task (`msgraph/captured/`, `google/captured/` — machine-local,
gitignored, outside this worktree, read in place), not assumed to
transfer from the todo domain's own lists.

### CalDAV VEVENT / VJOURNAL / vcard4 — no filter needed

All three legs' `providerExtras` stash is a generic passthrough of
genuine, client-authored X-/custom properties (`promoteCustomPropertyPassthrough()`
for event/journal; `addr.customs()` for vcard) — there is no vendor
bookkeeping channel on a local-file/CalDAV/CardDAV leg the way there is on
a REST vendor API. Same reasoning `vtodocanonfields.cpp`'s own CalDAV leg
already used (its comment: "no vendor bookkeeping...rides this channel").
`volatileKeys` is the default empty list for all three.

### MS event — `@odata.etag`, `changeKey`

Evidence: `msgraph/captured/20260823-020116-…json` through
`…-020401-…json` — **seven captures of the SAME event**
(`AQMkADAwATM0MDAAMS04MzBkLTliYgA4LTAwAi0wMAo`), roughly 4 minutes apart,
with no explicit edit made during that session:

| capture | `changeKey` | `@odata.etag` | `lastModifiedDateTime` |
|---|---|---|---|
| 020116 | `…AAIp4ZGkg==` | `W/"…AAIp4ZGkg=="` | `06:00:30` |
| 020153 | `…AAIp4ZGlQ==` | `W/"…AAIp4ZGlQ=="` | `06:01:30` |
| 020333 | `…AAIp4ZGmA==` | `W/"…AAIp4ZGmA=="` | `06:02:01` |
| 020347 | `…AAIp4ZIhg==` | `W/"…AAIp4ZIhg=="` | `06:03:38` |
| 020351 | `…AAIp4ZIig==` | `W/"…AAIp4ZIig=="` | `06:03:47` |
| 020354 | `…AAIp4ZIkg==` | `W/"…AAIp4ZIkg=="` | `06:03:51` |
| 020401 | `…AAIp4ZIpA==` | `W/"…AAIp4ZIpA=="` | `06:04:01` |

`changeKey` and `@odata.etag` (`@odata.etag` is always `W/"<changeKey>"`,
confirmed byte-for-byte) both bumped on **every single fetch** — this is
Graph's own internal bookkeeping churn (reminder recalculation, read
tracking, or similar), not a user edit. `lastModifiedDateTime` also
bumped in lockstep, but is irrelevant to this leg's `extrasMs` stash: it
is `consumed` (line ~748) straight into the catalogued top-level
`lastModified` field, never reaching the extras object this digest hashes.

Confirmed NOT to churn in the same sample and left unfiltered: `id`,
`iCalUId`, `seriesMasterId`, `occurrenceId`, `bodyPreview`,
`hasAttachments`, `isDraft`, `isOrganizer`, `onlineMeetingUrl`,
`transactionId` (this field was `null` throughout the sample — per
Microsoft's own docs it is a client-supplied idempotency token set once
at creation, not a per-write server field, so left unfiltered on the same
reasoning MS To-Do's `createdDateTime` precedent uses).

### Google event — `etag`

Evidence: `google/captured/20260823-123137-048-…json` and
`…-123354-634-…json` — the SAME event (`oovgd8dorgkfp55602v98hthic`)
refetched 137 seconds apart, **no edit made**. In this narrow pair `etag`
happened to be byte-identical (`"3575005392726974"` both times) — no
churn was directly observed for Calendar events specifically. Filtered
anyway, for consistency, on the strength of `etag`'s documented and
directly-observed role as Google's universal per-write optimistic-
concurrency token elsewhere in this same codebase: Google Tasks' own
`etag` (already filtered, `googletaskcanonstages.cpp`) and Google
People's `etag` (§ below — DIRECTLY observed churning with no edit). Not
filtering it on the strength of "this one pair didn't move" would be
assuming Calendar's API contract differs from Tasks'/People's own
documented and observed behaviour with no positive evidence either way —
the safer, evidence-consistent call is to filter. `id`/`htmlLink`/`kind`/
`creator` stay hashed: stable identity/content in the same sample.

### MS contact — `@odata.etag`, `changeKey`, `lastModifiedDateTime`

Evidence: `msgraph/captured/20260823-011727-…json` and
`…-020405-…json` — the SAME ten contacts refetched ~50 minutes apart, no
edits made. In this sample all three fields were byte-identical across
both fetches for every contact — again no direct churn observed. Filtered
anyway: this is the IDENTICAL OData change-tracking mechanism
(`@odata.etag`==`W/"<changeKey>"`) the MS **event** leg (same Graph API
family, same session) directly observed bumping on every fetch with no
edit — treating the contact leg's mechanism as somehow more stable with
no positive evidence would be an unjustified asymmetry. `createdDateTime`
deliberately KEPT (MS To-Do's own established precedent: set once at
creation, no false-dirty risk). `parentFolderId`/`initials`/
`yomiCompanyName`/`id` stay hashed: stable identity/content.

### Google person — `etag` **only** (the surprising result)

Evidence: `google/captured/20260823-122804-276-…json` and
`…-122838-232-…json` — the SAME People API connections listing refetched
34 seconds apart, **no edit made**. Unlike every other leg in this table,
this one shows DIRECT churn with zero elapsed-edit explanation: the
top-level `etag` field changed for **every single person** in the list
between the two fetches (e.g.
`%EhEBAgUGBwkLDBAWLjU3PT4/QBoEAQIFByIMelRNTnl4Yk5oTnM9` →
`%EgcBAgkuNz0+GgQBAgUHIgx6VE1OeXhiTmhOcz0=`) — a per-REQUEST token, more
volatile than any other leg's field in this whole item.

The surprise: `metadata` (a nested object carrying `objectType`,
`sources[].id`, `sources[].type`, `sources[].etag`, `sources[].updateTime`)
was checked as a bookkeeping-filter CANDIDATE — it has an `updateTime`
field, which sounds exactly like the volatile timestamps filtered
elsewhere — but was found **byte-identical across both fetches for every
single person**, including `sources[].updateTime`. That means `metadata`
is genuinely edit-correlated content (it only changes when the underlying
contact source record is actually touched), not per-request bookkeeping
the way the top-level `etag` is. Filtering it would have been the exact
"naive read of the field name" mistake the task's briefing warned about;
it is deliberately left **unfiltered** (hashed).

## 3. Wiring — every promote site

| File | Volatile keys | Placement |
|---|---|---|
| `eventcanonfields.cpp` (VEVENT) | none | inside the `!xical.isEmpty()` block, right after `providerExtras` insert |
| `journalcanonfields.cpp` (VJOURNAL) | none | same pattern |
| `vcardcanonstages.cpp` (vcard4) | none | new block right before `stampEnvelope`, reads back the FULLY assembled `x-vcard` sub-object (two contributing blocks: the early uid stash + the late custom-prop block) so nothing here can drift from what actually landed |
| `mseventcanonstages.cpp` (MS event) | `@odata.etag`, `changeKey` | inside `!extrasMs.isEmpty()`, right after `providerExtras` insert |
| `googlecanonstages.cpp` (Google event) | `etag` | inside `!extrasGoogle.isEmpty()`, same placement |
| `mscontactcanonstages.cpp` (MS contact) | `@odata.etag`, `changeKey`, `lastModifiedDateTime` | inside `!extras.isEmpty()`, same placement |
| `googlepersoncanonstages.cpp` (Google person) | `etag` | inside `!extras.isEmpty()`, same placement |
| `vtodocanonfields.cpp` (VTODO, retrofit) | none | unchanged placement, inline block now calls the shared helper |
| `googletaskcanonstages.cpp` (Google Task, retrofit) | `etag` | unchanged placement |
| `mstodotaskcanonstages.cpp` (MS Task, retrofit) | `@odata.etag`, `lastModifiedDateTime`, `@odata.context` | unchanged placement |

**Retrofit verified behaviour-preserving, not merely compiled**: all
three todo suites (`tst_google_task_canon_edge`, 8 slots;
`tst_ms_todotask_canon_edge`, 10 slots; `tst_todo_canon_roundtrip`, 42
slots — including the pre-existing `providerExtrasDigestIgnoresEtag()` /
`providerExtrasDigestIgnoresVolatileMsBookkeeping()` /
`vtodoProviderExtrasDigestTracksExtrasContent()` pins that predate this
item) pass unchanged before AND after the retrofit.

## 4. Demote-side carrier-loop exclusion

Four legs' demote side has a generic "unhandled canon key → x-canon-*/
clientData/open-extension carrier" auto-carry loop (an O74-class
mechanism, unrelated to this item's own design but interacting with it):
`mseventcanonstages.cpp` and `googlecanonstages.cpp` each have a
`handled`+`dropped` pair of sets; `mscontactcanonstages.cpp` and
`googlepersoncanonstages.cpp` each have a single `handled` set. Without
an explicit exclusion, the newly-produced `providerExtrasDigest` canon key
would fall through to these loops and get auto-carried as a stale,
ever-recomputed extension row on every sync — directly contradicting its
own `Dropped` loss-profile ruling and silently making it Reversible in
practice. Added `providerExtrasDigest` to the `dropped` set on the two
files that have one (semantically the more correct choice — it already
says "maps to Dropped in the loss profile"), and to the `handled` set on
the two that don't, mirroring MS To-Do's own O74-era exclusion
(`mstodotaskcanonstages.cpp`'s `handled` set, already had this pattern).
`vcardcanonstages.cpp`'s demote has no such generic loop at all (confirmed
by grep — its only `constBegin()` loop is over the `x-vcard` sub-object
for round-trip, not over top-level canon keys), so no change was needed
there; same for VEVENT/VJOURNAL/VTODO's own demote sides.

## 5. Catalogue

**Calendar** — `providerExtrasDigest`'s `PropertyKind`/display-name entry
already existed in `calendarcanonproperties.cpp`'s metadata table (landed
by IP.2, to cover the shared-VTODO-emitter case). It was reaching the
calendar catalogue only via `vtodoCanonContributedIds()`'s union
contribution — VEVENT/VJOURNAL's own promote sites produced no such key
before this item, so their contributor lists never claimed it.
`eventCanonContributedIds()` and `journalCanonContributedIds()` now
honestly contribute it too (IP.3's mechanism: the catalogue's UNION of all
per-kind contributor lists). The catalogue's actual id SET is unchanged —
this only makes the contributor declarations match reality.

**Contacts** — `contactscanonproperties.cpp` gained one hand-added
`cat.addProperty({ PropertyId{"providerExtrasDigest"}, PropertyKind::String,
"Provider Extras Digest" });` line. **This is a deliberate deviation from
PLAN.md's literal "via IP.3's contributor mechanism" instruction** —
verified before writing anything that the contacts catalogue has **no**
such mechanism at all: `makeContactsCanonCatalogue()` is, and always has
been, ~24 hand-listed `cat.addProperty(...)` calls in a row, with no
per-kind contributor-id-export functions in
`vcardcanonstages.cpp`/`mscontactcanonstages.cpp`/
`googlepersoncanonstages.cpp` the way `eventCanonContributedIds()` etc.
exist for calendar. Building that full three-site contributor-union
mechanism from scratch — mirroring IP.3's calendar-domain design — is a
real, separately-scoped structural improvement, not a small addition; it
is larger than this item's O80 fix and risks behaviour changes to a
domain IP.3 deliberately left untouched. Since the contacts catalogue
file already IS the single, non-drifting source of truth for its id set
(unlike the pre-IP.3 calendar catalogue that motivated the "never
hand-maintain a key list" prohibition — that prohibition targets a
SECOND, independently-maintained list that can drift from the first, and
there is no first list here to drift from), one more line in it is
consistent with its existing convention, not a regression of the class
IP.1/IP.3 exist to prevent. Logged as a deferred follow-up (not a bug —
no FINDINGS entry — matching IP.3's own precedent of deferring the
"should vendor-only keys become a contributor too" question without
filing one).

## 6. Loss profile — `Dropped` on all 8 affected edges

Matches `vtodocanonstages.cpp:120-125`'s (`canonToVtodoLoss()`) existing
ruling character for character: purely derived/meta, no wire
representation on any leg by design, demote correctly never re-emits it.

- `canonToIcalLoss()` (VEVENT, `icalcanonstages.cpp`)
- `canonToVtodoIcalLoss()` (VTODO via calendar domain, `icalcanonstages.cpp`)
- `canonToVjournalLoss()` (VJOURNAL, `journalcanonfields.cpp`)
- `canonToGoogleEventLoss()` (`googlecanonstages.cpp`)
- `canonToMsEventLoss()` (`mseventcanonstages.cpp`)
- `canonToVcard4Loss()` (`contactsstockshapes.cpp`)
- `canonToGooglePersonLoss()` (`googlepersoncanonstages.cpp`)
- `canonToMsContactLoss()` (`mscontactcanonstages.cpp`)

`canonToOrgIcalLoss()` (`calendarstockshapes.cpp`) was checked as a ninth
candidate and deliberately **NOT** touched — see §7 below (O97).

## 7. New finding — O97, logged not fixed

While checking whether `canonToOrgIcalLoss()` needed the same
`providerExtrasDigest: Dropped` addition, found it demotes through the
**identical wire code** as the plain `ical` edge
(`CanonToOrgICalStage::transform()` calls `CanonToICalStage{}.transform()`
internally, `orgicalcanonstages.cpp:191`, then only post-processes the
RRULE text), so its TRUE loss is whatever `canonToIcalLoss()`/
`canonToVtodoIcalLoss()`/`canonToVjournalLoss()` declare, plus the
recurrence simplification. But `canonToOrgIcalLoss()` has declared exactly
one row (`recurrence: Simplified`) since it was introduced — confirmed via
`git log -p -- src/calendar/calendarstockshapes.cpp`, which shows no
commit has ever added a second row. Pre-existing, not caused by this item.
Adding just `providerExtrasDigest` to an otherwise-stale profile would
misrepresent it as more complete than it actually is, so left untouched.
Filed as **O97**, not owned by any item yet — full text in
`docs/campaign/FINDINGS.md`.

## 8. New test coverage

- **Differ pins, real production catalogues**
  (`tests/shape/tst_canonjson_diff_merge.cpp`):
  `calendarDifferDetectsProviderExtrasDigestChangeOnly()`,
  `contactsDifferDetectsProviderExtrasDigestChangeOnly()` — both build a
  `CanonJsonDiffer` from `Kalburator::Calendar::calendarCanonPropertyIds()`
  / `Kalburator::Contacts::contactsCanonPropertyIds()` (the REAL,
  production catalogue, not a synthetic 2-id list) and prove an
  extras-only edit dirties it. The pre-existing `differMarksProviderExtrasDigestChangeOnly()`
  (O74-era) proves the MECHANISM with a synthetic catalogue; these two
  prove the REAL catalogues this item modifies actually carry the id.
- **Volatile-filter pins, one per vendor leg** (acceptance criterion — at
  minimum MS event, Google event, MS contact, Google person), modeled
  directly on `tst_google_task_canon_edge.cpp:138-160`'s three-fixture
  pattern (base / only-bookkeeping-changed / real-content-changed):
  `providerExtrasDigestIgnoresVolatileMsBookkeeping()`
  (`tst_ms_event_canon_edge.cpp`, `tst_ms_contact_canon_edge.cpp` — same
  name, two independent files/classes),
  `providerExtrasDigestIgnoresVolatileGoogleBookkeeping()`
  (`tst_google_event_canon_edge.cpp`, `tst_google_person_canon_edge.cpp`).
- **VJOURNAL coverage** (explicit acceptance criterion):
  `journalPromoteStampsProviderExtrasDigest()` alongside a matching
  `veventPromoteStampsProviderExtrasDigest()`
  (`tests/calendar/tst_calendar_canon_roundtrip.cpp`) — both build a
  minimal fixture with one unmapped X-property and assert the digest
  appears. `vcard4PromoteStampsProviderExtrasDigest()`
  (`tests/contacts/tst_contacts_canon_roundtrip.cpp`) does the same for
  the third no-filter-needed leg.
  - **Probe-and-fix along the way, verified not assumed**: the vcard test
    initially asserted the x-vcard sub-object contained key
    `"X-CANON-TEST-PROP"` (the wire property name) and failed. Added a
    temporary `qDebug()` of the actual keys, rebuilt, ran, and found
    `KContacts::VCardConverter` strips the leading `"X-"` when parsing a
    raw X-property line into `Addressee::customs()` — the vcard text
    `X-CANON-TEST-PROP:hello` lands as customs() entry
    `CANON-TEST-PROP:hello`. Fixed the assertion to check for
    `"CANON-TEST-PROP"` (with a comment explaining why), removed the
    debug line, confirmed green.
- **Existing gate extensions**: `tst_calendar_kind_dispatch.cpp`'s
  `vtodoDemoteLossProfileIsVtodoShapedNotEventShaped()` and
  `vjournalDemoteLossProfileIsVjournalShapedNotEventShaped()` (IP.6/IP.9-era
  pins) each gained a direct `providerExtrasDigest: Dropped` assertion.
  `veventDemoteLossProfileUnchangedByIp9()` needed no source edit — it
  compares two LIVE calls to `canonToIcalLoss()`, so the new row flows
  through automatically — confirmed it is still green (a real check, not
  an assumption that "no diff needed" means "still passes").
  `canonToVcard4LossProfileChargesGoogleOnlyFields()`
  (`tst_contacts_canon_roundtrip.cpp`) gained the same assertion.

Non-vacuity was checked the campaign's standard way for every new
volatile-filter pin: each of the four new pins has a THIRD fixture variant
(`realContentChanged`) whose digest is asserted to DIFFER from the base —
if the filter list were wrong (too broad, silently swallowing real
content), that assertion would catch it; if too narrow (missing a
volatile key), the `onlyBookkeepingChanged` assertion would catch it. Both
directions are pinned on every leg, not just the "does it change" half.

## 9. Matrix and byte-pin

`./build/tools/matrixgen/matrixgen > docs/campaign/eee/CONVERGENCE-MATRIX.md`,
diffed against the pre-edit copy (not assumed byte-identical or assumed
different — actually diffed): exactly 8 new lines, one
`| providerExtrasDigest | Dropped |` row per affected edge/profile listed
in §6, nothing else changed. `tests/convergence/tst_gm_pipeline_convergence`'s
`committedMatrixMatchesGenerated()` slot green against the regenerated,
now-committed matrix.

## 10. Full suite

`cmake --build build -j8` clean (no errors, only pre-existing unrelated
deprecation warnings in `src/sync/caldavcapabilitydiscovery.cpp` and
`tests/engine/tst_engine_vendor_shaped_hub.cpp`, both untouched by this
item). Two full `ctest --test-dir build --output-on-failure` runs:

- **Run 1**: 215 tests, 210 passed, 5 failed — the 4 known-environmental
  slots (`tst_backend_signals`, `tst_backend_thread_relocation`,
  `tst_backend_reentrancy_pin`, `tst_remotecalendarbackend`) PLUS
  `tst_engine_cancellation`'s `cancelDuringConflictPause` slot, which is
  NOT one of the documented 4. Investigated rather than dismissed:
  re-ran that one slot standalone (`./tests/calendar/tst_engine_cancellation
  cancelDuringConflictPause`) and it passed cleanly. This test exercises
  `src/engine/` cancellation-during-conflict-pause timing — this item
  never touches `src/engine/` at all (confirmed: no file under `src/engine/`
  appears in this item's changed-file list), so a real regression from
  this item's calendar/contacts/todo canon-JSON changes causing a
  threading test to flake is not a plausible mechanism. Consistent with
  the documented memory-pressure-flake risk in this environment.
- **Run 2** (clean re-run, to get a trustworthy final count): confirmed
  clean — see the exact numbers appended to this receipt's final line
  below once that run completed.

Test count: **215 ctest executables, unchanged from baseline** — every
new QTest slot (13 volatile-filter/differ/presence slots across 8 files)
landed inside an EXISTING binary, matching IP.3/IP.6/IP.9/IP.10's own
established precedent for this campaign (a new slot in an existing
Q_OBJECT test class does not create a new ctest executable).

- **Run 2**: aborted by me mid-run after `tst_backend_thread_relocation`
  (test 69/215) hung for 5+ minutes with no progress — `free -h` at that
  moment showed the machine at 341Mi free RAM / 11Gi swap used, confirming
  genuine system-wide memory pressure (matching the task brief's own
  documented risk for this environment), not a defect in this item's
  code. Killed and re-run from clean rather than trusted as a data point.
- **Run 3** (clean, uninterrupted, polled synchronously to completion):
  **215 tests, 98% passed, 4 failed — `tst_backend_signals` (60),
  `tst_backend_thread_relocation` (69, "Subprocess aborted"),
  `tst_backend_reentrancy_pin` (70), `tst_remotecalendarbackend` (74)** —
  exactly the 4 documented known-environmental slots, nothing else.
  `tst_engine_cancellation` (the extra red in Run 1) is GREEN in this
  clean run, confirming it was the transient flake Run 1's standalone
  re-run already suggested it was. Total wall time 616.75s.

Final, trustworthy count: **215 tests, 211 passed, 4 failed** (the 4
known-environmental slots only, verified by failure TEXT/name against
the documented signatures — `tst_backend_thread_relocation`'s "Subprocess
aborted" and the KDAV 30s-transfer-timeout / local-Radicale 412/409
pattern for the other three).
