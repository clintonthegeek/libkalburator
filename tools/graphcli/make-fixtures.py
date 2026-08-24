#!/usr/bin/env python3
"""Sanitize msgraph/captured/*.json extracts into committed fixtures.

tests/fixtures/vendor/microsoft/ holds representative, PII-scrubbed corpus
extracts (the machine-local originals in gitignored msgraph/captured/ stay
raw). Mirrors tools/googlecli/make-fixtures.py: consistent
pseudonymization — emails → personN@example.com, person names → Person N,
non-corpus subjects → Sample Event N, bodies redacted to shape-preserving
placeholders, Graph ids / changeKeys / etags / iCalUId blobs / webLinks /
join URLs deterministically re-minted. Structure, field vocabularies, and
wire shapes are preserved verbatim — that's the point of the corpus.
"""
import glob
import json
import os
import re
import sys

SRC = "msgraph/captured"
DST = "tests/fixtures/vendor/microsoft"

email_map = {}
name_map = {}
subject_counter = [0]
task_counter = [0]
list_counter = [0]
id_map = {}
text_map = {}


def sub_email(e):
    if e not in email_map:
        email_map[e] = f"person{len(email_map)+1}@example.com"
    return email_map[e]


def sub_name(n):
    n = n.strip()
    if not n:
        return n
    if n not in name_map:
        name_map[n] = f"Person {len(name_map)+1}"
    return name_map[n]


def sub_id(v):
    if v not in id_map:
        # Deterministic same-flavor replacement: iCalUId blobs are long hex;
        # changeKeys are base64 with == padding; Graph item ids start AQMk.
        n = len(id_map)
        if re.fullmatch(r"[0-9a-f]{40,}", v, re.I):
            m = "040000008200E00074C5B7101A82E00800000000" + ("%032X" % n)
        elif v.endswith("=="):
            m = "ZmFrZXZhbHVlJdGVzdGluZ3NlYXJjaA%03d==" % (n + 10)
        elif v.startswith("AQMk"):
            m = "AQMkADAwATFALEVIEW%03d" % n
        else:
            m = "fake-id-%03d" % n
        id_map[v] = m
    return id_map[v]


def sub_text(s):
    """Free-text redaction for subject/body-like strings."""
    s2 = s.strip()
    if not s2:
        return s
    if s in text_map:
        return text_map[s]
    if s2.startswith("CORPUS:"):
        text_map[s] = s          # corpus-sweep markers are already synthetic
    else:
        subject_counter[0] += 1
        text_map[s] = f"Sample Event {subject_counter[0]}"
    return text_map[s]


def sub_task_text(s):
    """Free-text redaction for todo task titles ("Sample Task N")."""
    s2 = s.strip()
    if not s2:
        return s
    if s2.startswith("CORPUS:"):
        return s          # corpus-sweep markers are already synthetic
    if s in text_map and text_map[s].startswith("Sample Task "):
        return text_map[s]
    task_counter[0] += 1
    t = f"Sample Task {task_counter[0]}"
    text_map[s] = t
    return t


def sub_list_name(n):
    """Todo list display names → neutral "Sample List N" (never Person N)."""
    n2 = n.strip()
    if not n2:
        return n
    if n in name_map:
        return name_map[n]
    list_counter[0] += 1
    m = f"Sample List {list_counter[0]}"
    name_map[n] = m
    return m


_NOT_SCRUBBED = object()


def scrub_key(k, v, ctx=None):
    """Key-driven scrub rules; returns REPLACEMENT or _NOT_SCRUBBED."""
    if isinstance(k, str) and k.endswith("@odata.etag") and isinstance(v, str):
        return sub_id(v)
    if not isinstance(v, str):
        return _NOT_SCRUBBED
    # OpenTypeExtension identity ids are vocabulary, not PII — keep verbatim.
    if k == "id" and v.startswith("microsoft.graph.openTypeExtension."):
        return v
    # todo payloads: titles / list names / body content get todo-specific
    # treatment BEFORE the generic person/subject rules.
    if k == "title":
        return sub_task_text(v)
    if ctx == "todo" and k == "displayName":
        return sub_list_name(v)
    if ctx == "todo" and k == "content":
        return "" if not v.strip() else "Sample body"
    if k in ("email", "address") and "@" in v and "example.com" not in v:
        return sub_email(v)
    if k in ("name", "displayName"):
        return sub_name(v)
    if k in ("id", "changeKey", "seriesMasterId", "occurrenceId",
             "transactionId", "iCalUId", "uid"):
        return sub_id(v)
    if k == "webLink":
        return "https://outlook.live.com/owa/?itemid=REDACTED"
    if k in ("joinUrl", "locationUri", "uniqueId", "sourceUrl"):
        return "https://redacted.example.com/" + (sub_id(v) if v else "")
    if k == "subject":
        return sub_text(v)
    if k in ("bodyPreview",):
        return ""
    return _NOT_SCRUBBED


def scrub(obj, ctx=None):
    if isinstance(obj, dict):
        # A todo/lists @odata.context scopes the payload: displayNames are
        # list names and body content is task text (not people/events).
        child = "todo" if (
            isinstance(obj.get("@odata.context"), str)
            and "/todo/" in obj["@odata.context"]) else ctx
        out = {}
        for k, v in obj.items():
            r = scrub_key(k, v, child)
            out[k] = scrub(v, child) if r is _NOT_SCRUBBED else r
        return out
    if isinstance(obj, list):
        return [scrub(x, ctx) for x in obj]
    return obj


def deepscrub(o, email_map=None, counter=None):
    """Belt-and-suspenders final pass over ALL string values."""
    EMAIL = re.compile(r'[A-Za-z0-9._%+\-]+@(?!example\.com)[A-Za-z0-9.\-]+\.[A-Za-z]{2,}')
    # %40-encoded emails inside Graph URLs (consumer accounts have no
    # outlook_* prefix — e.g. users('name%40gmail.com')).
    PCT_EMAIL = re.compile(r'[A-Za-z0-9._\-]+%40(?!example\.com)[A-Za-z0-9.\-]+\.[A-Za-z]{2,}')
    # @odata.context URLs embed the internal Exchange identity and raw item
    # ids — rewrite them wherever they appear.
    ACCOUNT = re.compile(r'outlook_[A-Z0-9]+(?:%40|@)[a-zA-Z.]+')
    ITEMID = re.compile(r"AQMk[A-Za-z0-9+/=.%_-]{20,}")
    if isinstance(o, dict):
        return {k: deepscrub(v, email_map, counter) for k, v in o.items()}
    if isinstance(o, list):
        return [deepscrub(x, email_map, counter) for x in o]
    if isinstance(o, str):
        s = o
        if "@" in s and "example.com" not in s:
            if email_map is None:
                email_map = {}
            def sub(m):
                e = m.group(0)
                if e not in email_map:
                    email_map[e] = "person%d@example.com" % (len(email_map) + 201)
                return email_map[e]
            s = EMAIL.sub(sub, s)
        if "graph.microsoft.com" in s or "%40" in s:
            s = ACCOUNT.sub("REDACTED_ACCOUNT", s)
            if email_map is None:
                email_map = {}
            def pctsub(m):
                e = m.group(0)
                if e not in email_map:
                    email_map[e] = "person%d@example.com" % (len(email_map) + 201)
                return email_map[e]
            s = PCT_EMAIL.sub(pctsub, s)
        s = ITEMID.sub("REDACTED_ITEM_ID", s)
        return s
    return o


def main():
    os.makedirs(DST, exist_ok=True)

    def latest(pattern):
        files = sorted(glob.glob(f"{SRC}/{pattern}"))
        return files[-1] if files else None

    picks = {
        "events-listing": "*me-calendar-events__top_10.json",
        "calendarview": "*calendarview_sta.json",
        "event-single": "*me-events-AQMkADAwATM0MDAA.json",
        "event-instances": "*me-events-AQMkADAwATM0MDAA-instances_startD.json",
        "contacts-listing": "*me_contacts__top_10.json",
        "todo-lists": "*me-todo-lists__top_10.json",
        "todo-tasks-listing": "*me-todo-lists-AQMkADAwATM0MDAA-tasks__top_25.json",
        # NOTE: the __extension.json capture is a plain tasks listing with no
        # extensions[] payload; the carrier channel is documented by this
        # single-task $entity GET instead (extensions() expand, 2026-08-24).
        "todo-task-carrier-extension":
            "20260824-150537-077-me-todo-lists-AQMkADAwATM0MDAA-tasks-AQMkADAwATM0MDAA.json",
    }
    failures = []
    for label, pattern in picks.items():
        src = latest(pattern)
        if not src:
            failures.append(label)
            print(f"SKIP {label}: no match")
            continue
        d = json.load(open(src))
        scrubbed = deepscrub(scrub(d))
        dst = f"{DST}/{label}.json"
        json.dump(scrubbed, open(dst, "w"), indent=1, sort_keys=False)
        print(f"{label}: {src} -> {dst}")
    return 1 if len(failures) == len(picks) else 0


if __name__ == "__main__":
    sys.exit(main())
