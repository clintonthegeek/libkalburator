# Discipline log — architectural-redress campaign

> Per INVARIANTS §9. Append one line per smell observed in code you pass through, even when
> off-topic from your current task. Format:
>
> `YYYY-MM-DD` — `file:line` — inv N — one phrase of context. (commit/PR if fixed)
>
> No fix is required this session. The point is that the next agent sees the same smell
> named, not stumbles across it fresh. Resolved findings are crossed out, not deleted.

## Baseline

**The finding baseline is `AUDIT.md` (verified rebuild, 2026-05-29), not this file.** All
22 prior seed entries (which restated the 2026-05-28 audit) are superseded: several were
factually wrong and are corrected or refuted in `AUDIT.md` ("Corrected from the prior audit"
and "Refuted / non-issues" sections). Do **not** re-enter audit findings here — they live in
`AUDIT.md` with `file:line` evidence. This log is for **new** smells discovered while working,
beyond what the audit already catalogues.

Quick pointer to the audit's actionable spine (see `AUDIT.md` for evidence + fix direction):

- **CRITICAL** — calendar-typed sync core: `BackendRegistry` stores `SyncBackend*`;
  `ProviderManager` `dynamic_cast`s to it; non-calendar backends inherit calendar-typed
  `SyncBackend`; `CalendarManager` destructive CRUD is untested.
- **MAJOR** — `SyncEngine`/`RemoteCalendarBackend` god classes; `types/` behavior;
  `shape/→conflict/`; `engine/`+`contacts/`+`universal/` pull calendar headers; raw-pointer
  lifetimes; thread-unsafe `RawFilesBackend`; silent SQLite/DELETE failures; test gaps.
- **MODERATE/MINOR/UGLY** — see `AUDIT.md`.

## Open (new findings, post-rebaseline)

_(none yet — append below as work uncovers smells the audit did not already name.)_

## Resolved

(none yet)
