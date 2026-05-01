# RemoteBackend::pushItems — serial HTTP bottleneck

**Status:** Open — not yet investigated
**Observed:** 2026-05-01, PlanStan CalDAV ↔ Local sync, existing collection

## Symptom

Full sync of a CalDAV ↔ Local mapping took several minutes. The backend
is issuing one HTTP `PUT`/`POST` per incidence, sequentially — each
round-trip waits for the server's response before the next is dispatched.
With a large calendar (hundreds of events), this serialises hundreds of
network round-trips into a single `pushItems` call.

## Root cause

`RemoteBackend::pushItems` creates one `KJob` per incidence and
`exec()`s or awaits each one before moving to the next. The fetch side
likely has a similar shape (one `REPORT`/`GET` per item after the
initial PROPFIND).

This predates the engine-merger refactor and was not changed during
Phases D–G. The new `PushOperation*` contract is a better fit for
concurrent dispatch than the old `writeFinished` signal, but the
internal KJob strategy was never updated to exploit it.

## Possible approaches

**Concurrent KJob dispatch (preferred):** Fire all KIO jobs in parallel
up to a configurable connection limit (e.g. 4–8), collect completion
callbacks, and call `op->complete()` / `op->fail()` only after all jobs
settle. This keeps the `pushItems` API contract unchanged and is the
lowest-risk change.

**WebDAV batch (if server supports it):** Some CalDAV servers accept
multi-put via `PATCH` or custom batch extensions. Narrow scope: only
worth it if concurrent dispatch alone is insufficient.

**Progress granularity:** Regardless of strategy, `PushOperation`
should emit progress updates (items pushed / total) during a large push
so the sync UI can show meaningful progress rather than appearing frozen.
`PushOperation` already has the plumbing for this; it just isn't called
by `RemoteBackend`.

## Scope

`libkalburator/src/calendar/remotebackend.cpp` — `pushItems` and
`fetchItems` implementations. No API surface changes needed.

## Related

- `docs/perf/2026-04-25-batched-ctag.md` — batched PROPFIND already
  reduces the CTag pre-pass from N round-trips to 1; the item-level
  push/fetch is a separate bottleneck that remains.
- If `FetchOperation` gets a similar concurrent-dispatch treatment,
  the fetch side of the sync should also be audited.
