# Handoff: the Organizer (outline) domain + OrgGrove + ShadowPlan arc

**Date:** 2026-05-26
**For:** a fresh session picking up the hierarchical-outline / ShadowPlan-sync line of work.
**One-line state:** OrgGrove (tree-sitter org foundation) is **built & shipped**; the
libkalburator `outline` domain has an **approved spec + ready plan** (not yet implemented);
the WildPalms/ShadowPlan integration is **specced-at-a-high-level, not yet planned**.

---

## Why this exists (the goal that started it)

WildPalms wants to sync **ShadowPlan**, a Palm outliner whose lists are hierarchical trees of
nodes that are simultaneously heading + task + note — the first Palm app that goes far beyond
the stock PIM apps. Forcing it through the flat `todo` canon loses the tree. The right model is
a generic **outliner** domain (the "Organizer shape"): a tree of nodes each optionally bearing
task + note facets. `todo` (flat) and `note` (single text) become *projections* of it.

The dependency chain (build bottom-up):
```
OrgGrove (tree-sitter org parser lib)   ← DONE, shipped
    └─ libkalburator `outline` domain    ← spec + plan READY, not implemented
          └─ WildPalms ShadowPlan peer + conduit  ← future spec
```

---

## 1. OrgGrove — DONE ✅ (2026-05-26)

- **Repo:** https://codeberg.org/clintonthegeek/OrgGrove (public), branch `master`, pinned
  commit **`c7b7743`**. Registered in `~/dev/bootstrap-laptop.sh` + `~/dev/laptop-setup.md`
  (+ `~/Sync`).
- **What it is:** standalone Qt6/C++ lib. `OrgGrove::Parser().parse(bytes) → Document` and
  `OrgGrove::serialize(Document) → bytes`. Model: `Document{title, todoKeywords, children}` +
  recursive `Headline{level, todoKeyword, isDone, priority(optional<int>), title, tags,
  planning{scheduled,deadline,closed}, properties(QMap), body, children}`.
- **How:** vendors MIT `nvim-orgmode/tree-sitter-org` grammar (`third_party/`, pinned
  SHA `219c0b27`) + links **system** tree-sitter runtime via pkg-config (mirrors
  `markoff-parser`). Exports `OrgGrove::OrgGrove` for FetchContent/find_package.
- **Scope:** structural parse + vocab-aware TODO/priority/tags/planning/properties/body +
  `#+TITLE:`/`#+TODO:`. **Out:** inline markup/links (body is verbatim), KCalendarCore,
  incremental edit, Emacs-oracle.
- **Prereq for any consumer:** `pkg-config --exists tree-sitter` (system `libtree-sitter`).
- Spec/plan in OrgGrove repo: `docs/2026-05-25-orggrove-design.md`, `-plan.md`. 7/7 tests green.

## 2. libkalburator `outline` domain — spec + plan READY, NOT implemented

- **Branch:** `feature/outline-domain` (off `main` @ canon tip `b629c8a`).
- **Docs (committed `3a464af`):** `docs/2026-05-25-outline-domain-design.md` (spec) +
  `docs/2026-05-25-outline-domain-plan.md` (7-task TDD plan).
- **What it builds:** a new `outline` domain mirroring `note`: canonical `OutlineNode` tree
  (record grain = one whole tree; node-level merge in canon space via a tree-aware differ —
  coarse first), with two peers — **org** (rich, via OrgGrove) and **OPML** (thin, drops task
  fields honestly).
- **Key plan facts for the implementer:**
  - Execute with subagent-driven-development or executing-plans, task-by-task TDD.
  - **Task 5 (org peer) consumes OrgGrove** — thin `OrgGrove::Headline` ⇄ canon `OutlineNode`
    adapter, NOT a hand-rolled parser. It adds the OrgGrove FetchContent dep
    (`KALBURATOR_ORGGROVE_SOURCE_DIR` override for dev →
    `-DKALBURATOR_ORGGROVE_SOURCE_DIR=/home/clinton/dev/OrgGrove`) and the
    `pkg-config tree-sitter` prereq.
  - Locked decisions (plan top): coarse differ first; OPML drops task fields; no explicit
    `order` field (array index is order).
  - Build dir convention: this repo uses `build/Qt-Debug/`? NO — check; the plan's commands use
    `build-dev`. Verify the actual configured build dir before running (libkalburator standalone
    vs PlanStan-hosted differ). Run the existing suite green first as a baseline.
- **Verification gate:** every libkalburator change must keep PlanStan's ctest baseline green
  (see memory `feedback_planstan_pretest_for_upstream`).

## 3. WildPalms / ShadowPlan integration — FUTURE (needs its own spec)

Per outline spec §10, deferred to a follow-on after the outline domain lands:
- `(outline, shadowplan-palm)` peer wiring ShadowStan's `libs/shadow` reverse-engineered codec
  (`~/dev/ShadowStan`, standalone repo, NOT yet a WP submodule, no remote yet).
- `(outline, shadowplan-json)` peer (ShadowStan's JSON serialization = a 2nd peer).
- The ShadowPlan **conduit rewrite to the current WildPalms plugin ABI** — the existing
  ShadowStan conduit targets a DELETED SDK surface (`K.8b T13: iconduit/isyncconduit/
  ibackendplugin deleted`); it must be rewritten as a static `Kalburator::Plugin` registered in
  `palmruntime.cpp`.
- Companion-DB resolution (tag IDs → names *into* canon, not separate sync objects) +
  whole-file granularity for the level-encoded `ShadP-*` lists.

---

## Parked, unrelated to the above (earlier this session, still live)

- **libkalburator:** canon-convergence promoted to `main` (`b629c8a`), pushed; old
  `feature/canon-upgrade-convergence` branch deleted.
- **WildPalms** `feature/canon-adoption-phase1` (pushed, in sync): all 5 PIM domains on canon;
  re-pinned to libkalburator main `b629c8a`; clean-fetch configure verified. **WP `main` merge
  remains GATED on device verification + Phase 6 loss-UX** — do not merge to WP main yet.
- **Plucker conduit** closed out (pushed + gitlink committed).

## Suggested next action

Execute the outline-domain plan on `feature/outline-domain` (build against local OrgGrove via
`-DKALBURATOR_ORGGROVE_SOURCE_DIR=/home/clinton/dev/OrgGrove`). Confirm the libkalburator build
dir + green baseline first. Then write the ShadowPlan integration spec.
