# Live Session Runbook — Tier A2 / A3 / A4 (2026-08-24)

**For:** the human operator with the live Outlook.com and Google accounts.
**Everything here is deliberately USER-RUN** (house rule: live checkpoints
are never agent-run). Estimated total: ~45–60 min.

**Prep (once):**

    cmake --build build --target graphcli googlecli ms-roundtrip -j8

Scopes were widened this session: `graphcli` now asks for `Tasks.ReadWrite`,
`googlecli` for `auth/tasks`. **You must re-consent** (`login`, not just a
cached token run) or task calls return 401/403 — that is expected, redo
the login step.

---

## A2 — Task-side corpus captures

### Graph side (`/me/todo`)

    # 1. Re-auth (new scope):
    ./build/tools/graphcli/graphcli login

    # 2. Discover your task lists:
    ./build/tools/graphcli/graphcli capture "/me/todo/lists?\$top=10"
    #   → msgraph/captured/<ts>-me-todo-lists.json — note a list "id".

    # 3. Capture its tasks:
    ./build/tools/graphcli/graphcli capture "/me/todo/lists/<LIST_ID>/tasks?\$top=25"

    # 4. If you have a task with recurrence, capture it singly too:
    ./build/tools/graphcli/graphcli capture "/me/todo/lists/<LIST_ID>/tasks/<TASK_ID>"

If your account has NO recurring task: create one probe via the raw POST
hatch (recurring daily task + open-extension carrier in the SAME create —
this doubles as the A3 drill):

    cat > /tmp/opencode/probe-task.json <<'EOF'
    {
      "title": "CORPUS:probe-recur",
      "recurrence": {"pattern": {"type": "weekly", "interval": 1,
                                 "firstDayOfWeek": "monday",
                                 "daysOfWeek": ["monday"]},
                     "range": {"type": "noEnd", "startDate": "2026-08-24"}},
      "extensions": [{
        "@odata.type": "microsoft.graph.openTypeExtension",
        "extensionName": "kalburator.canon",
        "x-canon-priority": "5"
      }]
    }
    EOF
    ./build/tools/graphcli/graphcli post "/me/todo/lists/<LIST_ID>/tasks" \
        /tmp/opencode/probe-task.json
    # then capture it back and CHECK whether "extensions" survived:
    ./build/tools/graphcli/graphcli capture "/me/todo/lists/<LIST_ID>/tasks?\$filter=startswith(title,'CORPUS')"

    # Cleanup when done:
    ./build/tools/graphcli/graphcli sweep-clean

### Google side (Tasks API)

`googlecli capture` accepts ABSOLUTE URLs; re-auth first for the new scope:

    ./build/tools/googlecli/googlecli auth
    # 1. Task lists:
    ./build/tools/googlecli/googlecli capture \
        "https://tasks.googleapis.com/tasks/v1/users/@me/lists"
    # 2. One list's tasks (note a list id):
    ./build/tools/googlecli/googlecli capture \
        "https://tasks.googleapis.com/tasks/v1/lists/<LIST_ID>/tasks?showHidden=true&maxResults=50"

No Google Tasks fixtures exist yet — these captures become
`tests/fixtures/vendor/google/tasks*.json` after sanitizing with
`tools/googlecli/make-fixtures.py` conventions (hand the captured files
back to me; I will sanitize + write the promotion slots).

---

## A3 — Carrier-survival drills (the O61(e) question)

| Channel | Drill | Survival check |
|---|---|---|
| People clientData | Create/update a test contact carrying `clientData: [{"key":"x-canon-categories","value":"[\"Friends\"]"}]` via googlecli patch/create paths, then GET it back | clientData row still present? |
| todoTask open extension | The probe-task.json POST above | `"extensions"` array present on GET? |
| contact open extension | `graphcli create-test-contact`, then `patch` adding an extensions block, then GET | extensions survive PATCH-in-place? |
| calendar event SVEP | Already PROVEN offline-only by O61(e); skip unless bored | — |

Record each verdict as SURVIVED / STRIPPED-ON-CREATE / STRIPPED-ON-PATCH /
STRIPPED-BY-SERVER with the captured JSON as evidence. These four verdicts
decide whether our Reversible rulings are live-Reversible or
offline-only-per-channel, and feed directly into the matrix annotations.

---

## A4 — Phase-6 live checkpoint

After A2 captures land:

    # Round-trip drill against the REAL Graph account (calendar domain,
    # proven surface from the 7.B checkpoint):
    ./build/tools/msroundtrip/ms-roundtrip roundtrip \
        msgraph/captured/<fresh-event-capture>.json

Then the full pipeline: pick ONE real event capture from each vendor,
hand both back; I run promote → cross-vendor translate → replay → compare
with you watching the wire, per the O61 discipline (probe objects tagged
CORPUS:, cleaned afterwards).

---

## What to hand back

1. All new files under `msgraph/captured/` and the googlecli capture dir —
   just tell me they exist; never commit raw captures.
2. The four carrier-survival verdicts (A3 table above).
3. Any 4xx bodies that surprised you (those are FINDINGS).

That's everything I need to sanitize fixtures, write promotion slots,
annotate the matrix, and close Tier A.
