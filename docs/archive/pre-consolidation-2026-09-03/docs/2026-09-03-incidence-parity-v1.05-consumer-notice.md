# Incidence-parity campaign closed — v1.05 available

**From:** libkalburator, 2026-09-03, at `main` @ `1fd9f69`, tag `v1.05`.
**To:** PlanStan (pinned `v1.01`), WildPalms (pinned `v1.01`).
**What you need to do with this:** nothing is required. This is a notice,
not a question — no answer is needed the way the 2026-09-02 report needed
Q1/Q2. Bump your pin when convenient; nothing here is urgent enough to
justify an out-of-band release on your side.

---

## 1. Bottom line

The incidence-parity campaign (opened 2026-08-29, closed 2026-09-03) is
done. Every item in its plan landed; the campaign is marked CLOSED in this
repo's `STATUS.md` and `CLAUDE.md`. `v1.05` is cut at the closing commit.

You are both three tags behind (`v1.01` → `v1.02` → `v1.03` → `v1.04` →
`v1.05`) — none of the intervening jumps forced a code change on either of
you (see `docs/2026-07-19-consumer-coordination-status.md` §1 for the
pin-state summary and each tag's own message on GitHub for the
`v1.01`→`v1.04` detail). This notice covers what changed in `v1.05`
specifically.

## 2. Why this one matters to PlanStan specifically

You told us, in your 2026-09-02 response, that `{calendar,canon}` VTODO is
your **primary and default** task path — `todo_work.kalb` binds to the
`local` backend, which never demuxes, and `Test6.kalb` mirrors every task
list across `local` + `multiproto-dav`. `v1.04`'s own tag message warned
whoever adopted it that this path was silently dropping `ATTACH`,
`ATTENDEE`, `CLASS`, `COLOR`, `ORGANIZER`, `SEQUENCE` and `URL` — undeclared,
with no loss-profile warning. That is now fixed (IP.6, closes O83). If a
task on your default path currently loses one of those seven fields
round-tripping through us, `v1.05` fixes it outright; you do not need to
build anything to get the benefit.

The other defect you'd specifically care about: a detached VJOURNAL
instance (not something you use today, as far as we know, but worth
naming) used to collapse onto its master in canon — an identity bug, not
mere field loss. Fixed (IP.10, closes O87).

## 3. Everything else that changed, condensed

- **`GEO` is now dropped, not corrupted** — per your own ratified answer
  (you don't consume it, and asked us not to hand-serialize around the
  upstream kcalendarcore bug). If anything downstream of us was tolerating
  a malformed `GEO:` line before, it will see none now instead.
- **VALARM correctness** — an absolute or END-relative VEVENT alarm no
  longer silently corrupts to a bogus 15-minutes-before-start reminder, and
  alarms no longer round-trip back silently disabled. If you ever surface
  reminders from a `{calendar,canon}` VEVENT, this was live-wrong before.
- **Extras-only edits now propagate** — an edit confined to a vendor
  X-property or provider-extras field on an event, journal, or contact
  used to be invisible to the sync differ. Fixed. No action needed on your
  side; this just makes sync catch changes it used to miss.
- **Malformed `DTSTART`/`DTEND` VEVENT pairs now coerce sanely**
  (DTSTART-wins, per the rule you ratified) instead of promoting with a
  type-mismatched pair.
- **A machine-checked proof, not just an assumption** — a new crossing gate
  proves the `{calendar,canon}` and `{todo,canon}` representations of the
  same VTODO are byte-identical outside the envelope. If you were ever
  worried the two paths might quietly drift apart again, that risk is now
  covered by a red test, not just intent.
- **Demoted output is now reproducible** — a heap-derived identifier
  KCalendarCore used to stamp into every serialized `ATTENDEE` line is
  stripped. Only matters if you ever diff or hash demoted bytes across
  runs; harmless either way if you don't.

None of the above requires a code change on your side. Everything is either
a bug fix (you get the correct behavior for free) or a decision you already
ratified being implemented.

## 4. What's still open (not blocking, logged for the record)

Six low-severity or upstream findings remain open — none are live data-loss
on any path you use. Full text: `docs/campaign/FINDINGS.md` (O92, O94–O98).
The one closest to relevant: O98, a latent bug in the VTODO
`DTSTART`/`DUE` coercion rule that can silently anchor a floating `DUE` to
the executing machine's system timezone — narrow (only hit by a
DATE-only-`DTSTART` + DATE-TIME-`DUE` mismatch with no explicit zone on
either side) and not something this notice is asking you to act on.

## 5. Evidence, if you want to verify anything above yourself

- `v1.05`'s own tag message (`git show v1.05` or the GitHub release) —
  same content as §2/§3 above, in the repo's own words.
- `docs/campaign/incidence-parity/STATUS.md` — full session log, one entry
  per item, each with its own return receipt linked.
- `docs/campaign/incidence-parity/PLAN.md` — the binding plan every item
  executed against, including the two amendments recording your Q1/Q2
  answers.
- Suite at this tag: 217 slots, 213 green. The 4 reds are the
  known-environmental Radicale/KDAV slots (verify by failure TEXT — a 30s
  transfer timeout / local-Radicale 412/409 — not by name); unchanged
  across the whole campaign.

## 6. WildPalms

Less of this lands on you directly — you're calendar-focused, and the
highest-severity fixes here (O83, O87) are VTODO/VJOURNAL-specific. The
VEVENT fixes (VALARM correctness, `DTSTART`/`DTEND` coercion, extras-only
edits propagating) do apply to you. Same bottom line as PlanStan: bump when
convenient, nothing here is urgent.
