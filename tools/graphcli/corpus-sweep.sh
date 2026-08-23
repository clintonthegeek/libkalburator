#!/usr/bin/env bash
# corpus-sweep.sh — scenario matrix driver for the EEE Phase-0 vendor corpus.
# Creates CORPUS:<runid>:-prefixed objects against the live Graph account via
# graphcli, captures the interesting endpoints into msgraph/captured/, and
# leaves cleanup to `graphcli sweep-clean` (run with --clean to invoke it at
# the end). Each run mints a unique tag so overlapping runs cannot
# cross-contaminate each other's series; `graphcli sweep-clean <tag>` removes
# a single run's objects, bare `sweep-clean` still sweeps every run.
#
# Usage:
#   tools/graphcli/corpus-sweep.sh <scenario|list|all> [--clean]
#
# Requires: graphcli built (GRAPHCLI env overrides the binary path), python3.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GRAPHCLI="${GRAPHCLI:-$SCRIPT_DIR/../../build/tools/graphcli/graphcli}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Wide window so instances/calendarview see everything we create.
WIN_START="2026-08-01T00:00:00Z"
WIN_END="2028-01-01T00:00:00Z"

# Per-run tag: subjects become "CORPUS:<runid> <name>" so overlapping runs
# cannot cross-contaminate each other's series (see FINDINGS O57 tooling
# notes). The leading "CORPUS:" keeps `graphcli sweep-clean` (which matches
# the "CORPUS:" prefix) working unchanged; `sweep-clean CORPUS:<runid>`
# surgically cleans one run. Exported for the python helpers below.
CORPUS_TAG="CORPUS:$(date -u +%Y%m%dT%H%M%SZ)-$$"
export CORPUS_TAG

gcli() { "$GRAPHCLI" "$@"; }

die_no_graphcli() {
    [[ -x "$GRAPHCLI" ]] || { echo "graphcli not found at $GRAPHCLI (set GRAPHCLI=...)"; exit 2; }
}

# create_event <name> ; echoes the Graph event id or fails
create_event() {
    local file="$TMP/$1.json"
    cat > "$file"
    local out
    out=$(gcli create event "$file") || { echo "CREATE FAILED: $1" >&2; return 1; }
    echo "$out" | python3 -c "import sys,re; m=re.search(r'id = (.*)', sys.stdin.read()); print(m.group(1) if m else '')"
}

capture_all_views() {
    local label="$1" series_id="$2"
    gcli capture "/me/calendar/events?\$top=10" >/dev/null 2>&1
    gcli capture "/me/calendarview?startDateTime=$WIN_START&endDateTime=$WIN_END" >/dev/null 2>&1
    if [[ -n "$series_id" ]]; then
        gcli capture "/me/events/$series_id/instances?startDateTime=$WIN_START&endDateTime=$WIN_END" >/dev/null 2>&1
    fi
    echo "[$label] captured list + calendarview + instances"
}

# ---------------------------------------------------------------------------
# Scenario: recurrence range variants (UNTIL / COUNT / noEnd)
# ---------------------------------------------------------------------------
sweep_recurrence_ranges() {
    local id
    id=$(create_event rec-until <<EOF
{"subject":"${CORPUS_TAG} recurrence UNTIL","start":{"dateTime":"2026-09-03T14:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-03T15:00:00","timeZone":"UTC"},"recurrence":{"pattern":{"type":"weekly","interval":1,"daysOfWeek":["thursday"],"firstDayOfWeek":"sunday"},"range":{"type":"endDate","startDate":"2026-09-03","endDate":"2026-12-31","recurrenceTimeZone":"UTC"}}}
EOF
) || return 1
    capture_all_views "rec-until" "$id"

    id=$(create_event rec-count <<EOF
{"subject":"${CORPUS_TAG} recurrence COUNT","start":{"dateTime":"2026-09-03T16:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-03T17:00:00","timeZone":"UTC"},"recurrence":{"pattern":{"type":"weekly","interval":2,"daysOfWeek":["thursday"]},"range":{"type":"numbered","startDate":"2026-09-03","numberOfOccurrences":6}}}
EOF
) || return 1
    capture_all_views "rec-count" "$id"

    id=$(create_event rec-noend <<EOF
{"subject":"${CORPUS_TAG} recurrence noEnd","start":{"dateTime":"2026-09-04T09:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-04T09:30:00","timeZone":"UTC"},"recurrence":{"pattern":{"type":"daily","interval":1},"range":{"type":"noEnd","startDate":"2026-09-04"}}}
EOF
) || return 1
    capture_all_views "rec-noend" "$id"
}

# ---------------------------------------------------------------------------
# Scenario: monthly pattern variants (absolute + relative)
# ---------------------------------------------------------------------------
sweep_recurrence_monthly() {
    local id
    id=$(create_event rec-abs-monthly <<EOF
{"subject":"${CORPUS_TAG} absoluteMonthly day 13","start":{"dateTime":"2026-09-13T12:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-13T13:00:00","timeZone":"UTC"},"recurrence":{"pattern":{"type":"absoluteMonthly","interval":1,"dayOfMonth":13},"range":{"type":"endDate","startDate":"2026-09-13","endDate":"2027-09-13"}}}
EOF
) || return 1
    capture_all_views "rec-abs-monthly" "$id"

    id=$(create_event rec-rel-monthly <<EOF
{"subject":"${CORPUS_TAG} relativeMonthly second monday","start":{"dateTime":"2026-09-14T12:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-14T13:00:00","timeZone":"UTC"},"recurrence":{"pattern":{"type":"relativeMonthly","interval":1,"daysOfWeek":["monday"],"index":"second","firstDayOfWeek":"sunday"},"range":{"type":"endDate","startDate":"2026-09-14","endDate":"2027-09-14"}}}
EOF
) || return 1
    capture_all_views "rec-rel-monthly" "$id"
}

# ---------------------------------------------------------------------------
# Scenario: exceptions — override one instance, cancel another.
# The heart of the master/override model. Requires a fresh series.
# ---------------------------------------------------------------------------
sweep_exceptions() {
    local id occ_id
    id=$(create_event exc-series <<EOF
{"subject":"${CORPUS_TAG} exception source series","start":{"dateTime":"2026-10-01T10:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-10-01T11:00:00","timeZone":"UTC"},"recurrence":{"pattern":{"type":"weekly","interval":1,"daysOfWeek":["thursday"]},"range":{"type":"numbered","startDate":"2026-10-01","numberOfOccurrences":10}}}
EOF
) || return 1
    capture_all_views "exc-series-created" "$id"

    # Pull the first TWO occurrence ids: patch the first (override),
    # delete the second (cancellation).
    local inst_file
    inst_file=$(python3 - <<'PYEOF'
import glob, json, os
files = sorted(glob.glob(os.path.join(os.environ.get("MSGRAPH_DIR", "msgraph"), "captured", "*instances*.json")), key=os.path.getmtime)
for f in reversed(files):
    try:
        d = json.load(open(f))
    except Exception:
        continue
    if any(e.get("subject", "").startswith(os.environ["CORPUS_TAG"]) for e in d.get("value", [])):
        print(f); break
PYEOF
)
    [[ -n "$inst_file" ]] || { echo "[exc] no CORPUS instances capture found"; return 1; }

    mapfile -t occ_ids < <(python3 - "$inst_file" <<'PYEOF'
import json,sys
d=json.load(open(sys.argv[1]))
vals=[e for e in d.get("value",[]) if e.get("subject","").startswith(os.environ["CORPUS_TAG"])]
for e in sorted(vals,key=lambda x:x["start"]["dateTime"])[:2]:
    print(e["id"])
PYEOF
)
    [[ ${#occ_ids[@]} -ge 2 ]] || { echo "[exc] need two occurrences, got ${#occ_ids[@]}"; return 1; }

    cat > "$TMP/override.json" <<EOF
{"subject":"${CORPUS_TAG} OVERRIDDEN occurrence"}
EOF
    gcli patch event "${occ_ids[0]}" "$TMP/override.json" >/dev/null \
        && echo "[exc] override applied to occurrence 1" || echo "[exc] override FAILED"

    gcli delete event "${occ_ids[1]}" >/dev/null 2>&1 \
        && echo "[exc] cancellation applied to occurrence 2" \
        || echo "[exc] occurrence delete failed"

    sleep 2
    capture_all_views "exc-after-mutations" "$id"
    echo "[exc] expect ONE 'exception'-type instance + a gap where occurrence 2 was"
}

# ---------------------------------------------------------------------------
# Scenario: all-day + authored-in-ET
# ---------------------------------------------------------------------------
sweep_time_shapes() {
    local id
    id=$(create_event allday <<EOF
{"subject":"${CORPUS_TAG} all-day event","isAllDay":true,"start":{"dateTime":"2026-09-10T00:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-11T00:00:00","timeZone":"UTC"}}
EOF
) || return 1
    capture_all_views "allday" "$id"

    id=$(create_event authored-et <<EOF
{"subject":"${CORPUS_TAG} authored Eastern","start":{"dateTime":"2026-09-15T14:00:00","timeZone":"Eastern Standard Time"},"end":{"dateTime":"2026-09-15T15:00:00","timeZone":"Eastern Standard Time"},"isReminderOn":false}
EOF
) || return 1
    capture_all_views "authored-et" "$id"
}

# ---------------------------------------------------------------------------
# Scenario: richness — multi-location, attachment, categories/sensitivity
# ---------------------------------------------------------------------------
sweep_richness() {
    local id
    id=$(create_event multilocation <<EOF
{"subject":"${CORPUS_TAG} two locations","start":{"dateTime":"2026-09-20T10:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-20T11:00:00","timeZone":"UTC"},"location":{"displayName":"Primary place"},"locations":[{"displayName":"Primary place"},{"displayName":"Secondary place, remote"}]}
EOF
) || return 1
    capture_all_views "multilocation" "$id"

    id=$(create_event sensitivity <<EOF
{"subject":"${CORPUS_TAG} private oof","start":{"dateTime":"2026-09-21T10:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-21T11:00:00","timeZone":"UTC"},"sensitivity":"private","showAs":"oof","importance":"high","categories":["Purple category"],"reminderMinutesBeforeStart":45}
EOF
) || return 1
    cat > "$TMP/attach.json" <<EOF
{"@odata.type":"#microsoft.graph.fileAttachment","name":"corpus-note.txt","contentBytes":"SGVsbG8gZnJvbSBjb3JwdXMgc3dlZXAh"}
EOF
    gcli capture "/me/events/$id" >/dev/null 2>&1
    echo "[richness] (attachment POST left as manual step; see notes)"
}

# ---------------------------------------------------------------------------
# Scenario: negative corpus — malformed bodies, expect 4xx JSON
# ---------------------------------------------------------------------------
sweep_negative() {
    local file="$TMP/neg-notz.json"
    cat > "$file" <<EOF
{"subject":"${CORPUS_TAG} NEG no timezone","start":{"dateTime":"2026-09-22T10:00:00"},"end":{"dateTime":"2026-09-22T11:00:00"}}
EOF
    gcli create event "$file" && echo "[neg] no-tz event ACCEPTED (surprising!)" \
        || echo "[neg] no-tz event rejected (error shape above is the corpus)"

    file="$TMP/neg-badmonth.json"
    cat > "$file" <<EOF
{"subject":"${CORPUS_TAG} NEG month 13","start":{"dateTime":"2026-09-23T10:00:00","timeZone":"UTC"},"end":{"dateTime":"2026-09-23T11:00:00","timeZone":"UTC"},"recurrence":{"pattern":{"type":"absoluteYearly","interval":1,"dayOfMonth":1,"month":13},"range":{"type":"noEnd","startDate":"2026-09-23"}}}
EOF
    gcli create event "$file" && echo "[neg] month-13 ACCEPTED (surprising!)" \
        || echo "[neg] month-13 rejected"
}

# ---------------------------------------------------------------------------
# Scenario: contacts — fully typed + phonetics + dates
# ---------------------------------------------------------------------------
sweep_contacts() {
    local file="$TMP/contact-full.json"
    cat > "$file" <<EOF
{"givenName":"Ada","surname":"Lovelace","middleName":"Augusta","nickName":"Ada","fileAs":"Lovelace, Ada","jobTitle":"Analyst","department":"Engines","companyName":"Analytical Engines","businessPhones":["+1 613 555 0100"],"mobilePhone":"+1 613 555 0101","emailAddresses":[{"address":"ada@example.com","name":"Ada Lovelace"},{"address":"ada2@example.com","name":"Ada L"}],"homeAddress":{"street":"1 Charles St","city":"London","state":null,"countryOrRegion":"UK","postalCode":"W1J"},"birthday":"1815-12-10T00:00:00Z","categories":["${CORPUS_TAG}"]}
EOF
    gcli create contact "$file" >/dev/null \
        && echo "[contacts] full contact created" || echo "[contacts] full contact FAILED"
    gcli capture "/me/contacts?\$top=10" >/dev/null 2>&1
    echo "[contacts] captured /me/contacts"
}

# ---------------------------------------------------------------------------

list_scenarios() {
    cat <<EOF
recurrence_ranges   UNTIL / COUNT / noEnd series
recurrence_monthly  absoluteMonthly day-13 / relativeMonthly second-monday
exceptions          override + cancel occurrences of a fresh series
time_shapes         all-day + authored-in-Eastern
richness            multi-location + sensitivity/showAs/importance/categories
negative            malformed bodies (expect 4xx error-shape corpus)
contacts            fully typed contact with phonetics and dates
all                 everything above
EOF
}

main() {
    die_no_graphcli
    local scenario="${1:-list}"
    [[ "$scenario" == "list" ]] && { list_scenarios; exit 0; }

    case "$scenario" in
        recurrence_ranges)  sweep_recurrence_ranges ;;
        recurrence_monthly) sweep_recurrence_monthly ;;
        exceptions)         sweep_exceptions ;;
        time_shapes)        sweep_time_shapes ;;
        richness)           sweep_richness ;;
        negative)           sweep_negative ;;
        contacts)           sweep_contacts ;;
        all)
            sweep_recurrence_ranges
            sweep_recurrence_monthly
            sweep_exceptions
            sweep_time_shapes
            sweep_richness
            sweep_negative
            sweep_contacts
            ;;
        *) echo "unknown scenario: $scenario (use 'list')"; exit 2 ;;
    esac

    if [[ "${2:-}" == "--clean" ]]; then
        echo "--- sweep-clean ---"
        gcli sweep-clean
    else
        echo "Left CORPUS objects (tag: $CORPUS_TAG) in place. Clean this run with:"
        echo "  graphcli sweep-clean $CORPUS_TAG"
        echo "Or everything: graphcli sweep-clean"
    fi
}

main "$@"
