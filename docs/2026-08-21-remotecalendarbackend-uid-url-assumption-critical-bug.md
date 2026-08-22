# CRITICAL — RemoteCalendarBackend assumes every item's URL is `<calendar>/<uid>.ics`; false for any item another CalDAV client created

**Status:** RESOLVED 2026-08-22 (branch `fix/o54-uid-url-assumption`) —
the fix landed exactly per the "Recommended fix" section below; see
FINDINGS **O54** for the closure summary, audit results for the other
call sites and CardDAV/LocalBackend, and the regression test. The writeup
below is kept as-found.

**Date found:** 2026-08-21, live PlanStan session (same day as the
conflict-resolution-repair work — unrelated to it; this is a distinct,
pre-existing defect independent of that campaign).

**Severity:** critical, and broader than the conflict-resolution bugs this
session also fixed. **It requires no conflict to trigger** — a plain,
uncontested edit to any pre-existing item is enough. It reproduces on the
**first edit-and-sync of any item that PlanStan itself did not create.**
Since adopting an existing Nextcloud/iCloud/Fastmail/etc. calendar is
PlanStan's normal onboarding path, and virtually every item on a
newly-adopted calendar was created by whatever app the user used before
PlanStan, this is not an edge case — it is close to "the first real edit a
new user makes will fail to sync."

## Symptom

Editing an item locally, syncing, and having the write silently and
permanently fail:

```
RemoteCalendarBackend::setRawIcsAsync: Failed, HTTP status: 400
error: "Error transferring https://…/inbox-1/8fecdc8c-…-9755f3065d85.ics -
        server replied with status code 400"
body: "<?xml version=\"1.0\" encoding=\"utf-8\"?>
       <d:error xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\">
         <s:exception>Sabre\DAV\Exception\BadRequest</s:exception>
         <s:message>Calendar object with uid already exists in this
                     calendar collection.</s:message>
       </d:error>"
SyncRunCoordinator: mapping completed: success: false ... target: "... E1"
  Error: "Write to inbox-1 failed"
```

This repeats identically on every subsequent sync — the write never
succeeds, and nothing marks it as a permanent failure to stop retrying
(same shape as every other "silently loops forever" bug this session
found, but a different root cause).

## Root cause — confirmed, not theorized

`RemoteCalendarBackend::generateItemUrl(davUrl, uid)`
(`src/calendar/remotecalendarbackend.cpp:824`) builds an item's write URL
by string-concatenating `<calendar-collection-url>/<uid>.ics`. This is used
**pervasively** — every call site that needs to know where an item lives
on the server calls this function (confirmed by direct grep: lines 1315,
1439, 1452, 1483, 1493, 1531, 2803, 2895, 3067 all call it). There is no
per-item URL cache anywhere in the class — confirmed by grep for
`m_itemUrls`/`hrefForUid`/any uid→URL map: none exists. The only caches
that exist are **keyed by URL**, not by UID (`m_localEtags`,
`m_contentCache`, the ETag cache) — the class remembers "what did I last
see at this URL" perfectly, but has no memory of "what URL does this UID
live at."

That assumption — filename equals UID — is only true for items PlanStan
(or another UID-named-file client) created. CalDAV does not require it;
SabreDAV/Nextcloud (and most servers) assign whatever filename the
**creating** client chose, and that filename is permanent for the life of
the object. `setRawIcsAsync` (`:3189`) calls `generateItemUrl()`, finds no
cached ETag for its *guessed* URL (because the real object lives
elsewhere, so the ETag cache — also keyed by the guessed, wrong URL —
naturally misses too), sends an unconditional PUT to a URL that does not
exist, and SabreDAV's specific "uid already exists in this calendar
collection" error is exactly what it says when a client tries to PUT a new
resource whose UID collides with an existing one at a *different* URL.

## Live confirmation (read-only CalDAV inspection, this session)

A read-only diagnostic script (PROPFIND + GET only, listed every item in
the affected `inbox-1` calendar) found:

```
file: 1755247320.R237.ics
uid:  8fecdc8c-cf00-4b74-b2dc-f6d84790b74d
summary: copy paylor vid  tovhs
```

The failing write's target was
`.../inbox-1/8fecdc8c-cf00-4b74-b2dc-f6d84790b74d.ics` — a URL that does
not exist. The item's real, permanent URL is `1755247320.R237.ics` (the
`.R237` suffix suggests it was originally created by a client that names
recurrence-exception objects that way — Apple Calendar and several others
do this; irrelevant to the fix, just explains why the filename looks
unusual). No duplicate UIDs were found in the calendar — this is not
server-side data corruption or test pollution, it is a clean, reproducible
client-side URL-derivation bug.

## Where the real URL is available and thrown away

`RemoteCalendarBackend::processFetchedItems()`
(`src/calendar/remotecalendarbackend.cpp:2330`) is where every fetched
item's real URL is known simultaneously with its parsed content:

```cpp
// :2350-2351 — urlKey IS the item's real, correct URL, straight from the
// server's own listing (allItems, built from the CTag/REPORT machinery
// upstream in the same fetch).
QString urlKey = normalizeUrlKey(item.url().url().toString());
...
if (fetchedItemsMap.contains(urlKey)) {
    const KDAV::DavItem &davItem = fetchedItemsMap[urlKey];
    icalData = QString::fromUtf8(davItem.data());
    ...
```

`icalData` gets parsed into a `KCalendarCore::Incidence` upstream of this
function (by the caller building `fetchedIncidences`), which is where the
UID becomes known — but nothing ever records "this UID's real URL is
`urlKey`" anywhere. The information is present, in scope, at exactly the
right moment, and is discarded.

## Recommended fix

Add a `QHash<QString, QString> m_uidToUrl` (uid → normalized real URL,
same normalization `normalizeUrlKey()` already uses everywhere else) to
`RemoteCalendarBackend`. Populate it in `processFetchedItems()` once the
incidence's UID is known (need to move UID extraction earlier in that
function, or populate it from the caller once `fetchedIncidences` is
built — either is a small, local change since both already have `urlKey`
and the parsed incidence in scope together).

Add a `resolveItemUrl(uid, calendarId)` helper (or fold the lookup directly
into `generateItemUrl()`'s callers) that checks `m_uidToUrl` first and
falls back to `generateItemUrl()`'s guess **only** when the UID is
genuinely new (a real client-side create, which has no server URL yet by
definition — that path is correct as-is and must not change). Every
`generateItemUrl()` call site that handles an **update** (not a create) —
`setRawIcsAsync` (:3189) is the one this session hit, but the same audit
should cover every other call site at lines 1315, 1439, 1452, 1483, 1493,
1531, 2803, 2895, 3067 individually; some may be create-only paths where
the guess is correct and should stay untouched.

`m_uidToUrl` needs the same lifecycle care every other per-item cache in
this class already has: cleared/updated on delete (`noteItemErased()`
already exists as the pattern to follow — it's called from every delete
path in this file, e.g. `:1334`, `:1511`, `:2571`, `:2919`, `:3156`,
`:3638`), and populated on create too (the create paths, e.g. around
`:2825` `noteItemWritten(normalizeUrlKey(createdItem.url()...), ...)`,
already know the real URL for a just-created item — that's exactly a
"first entry" for `m_uidToUrl` and should be added at the same call sites).

## Suggested regression test

`tests/calendar/tst_remotecalendarbackend*.cpp` (the existing suite for
this class) needs a case that: seeds a `MockBackend`-equivalent or a fake
DAV server response where an item's discovered URL does **not** match
`<calendar>/<uid>.ics` (any filename scheme is fine, e.g.
`1755247320.R237.ics` as found live), fetches it once (so `m_uidToUrl`
gets populated), edits its content, and asserts the resulting write PUTs
to the item's **original** discovered URL — not a freshly-generated one.
Should be shown RED against the current code first (it will PUT to the
wrong, guessed URL) before landing the fix, per this repo's own
falsifiability convention (INVARIANTS §5).

## Why this wasn't caught by the conflict-resolution-repair test suite

Every `MockBackend`-based test in `tst_syncengine_unification.cpp` and
`tst_calendar_conflict.cpp` uses `MockBackend`, which is a pure in-memory
stub with no URL concept at all — it can't exercise this bug by
construction. It only reproduces against a real (or realistically faked)
CalDAV server where filename and UID can diverge. The conflict-resolution
work's "applied stored resolution" tests all passed cleanly precisely
because they never touch `RemoteCalendarBackend` or real URLs — this bug
and the conflict-resolution bugs are in genuinely disjoint code paths that
happened to be exercised back-to-back in the same live PlanStan session.

## Not investigated further this session

- Whether any of the other 8 `generateItemUrl()` call sites are
  create-vs-update-ambiguous in the same way (each needs individual
  judgment — see "Recommended fix" above).
- Whether the equivalent bug exists in `RemoteContactsBackend` (CardDAV) —
  not audited, but the same "assume filename == identifier" pattern is a
  reasonable thing to check for given how this one was found.
- Whether `LocalBackend`'s conceptually-similar `<uid>.ics` filename
  convention (confirmed correct behavior there — PlanStan creates every
  local file itself, so the assumption actually holds) has any analogous
  gap for a locally-adopted/imported file that didn't originally come from
  PlanStan (e.g. a raw `.ics` dropped into a watched directory by another
  tool). Worth a quick check, not assumed safe.
