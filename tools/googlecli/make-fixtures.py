#!/usr/bin/env python3
"""Sanitize google/captured/*.json extracts into committed fixtures.

tests/fixtures/vendor/google/ holds representative, PII-scrubbed corpus
extracts (the machine-local originals in gitignored google/captured/ stay
raw). Consistent pseudonymization: emails → personN@example.com, names →
Person N, phones → +1555-01xx, source ids/resourceNames renumbered,
free-text locations redacted. Structure, field vocabularies, etags, and
wire shapes are preserved verbatim — that's the point of the corpus.
"""
import json
import glob
import os
import re
import sys

SRC = "google/captured"
DST = "tests/fixtures/vendor/google"

email_map = {}
name_map = {}
phone_counter = [1000]
id_map = {}


def sub_email(e):
    if e not in email_map:
        email_map[e] = f"person{len(email_map)+1}@example.com"
    return email_map[e]


def sub_name(n):
    if n not in name_map:
        name_map[n] = f"Person {len(name_map)+1}"
    return name_map[n]


def scrub(obj):
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            if k in ("email",) and isinstance(v, str) and "@" in v and "example.com" not in v:
                out[k] = sub_email(v)
            elif k in ("displayName", "displayNameLastFirst", "givenName",
                       "familyName", "middleName", "unstructuredName",
                       "formattedValue") and isinstance(v, str):
                # formattedValue on addresses may be a place, redact too
                out[k] = sub_name(v)
            elif k in ("value",) and isinstance(v, str) and v.startswith("+"):
                phone_counter[0] += 1
                out[k] = f"+1555-{phone_counter[0]:04d}"
            elif k == "canonicalForm" and isinstance(v, str) and v.startswith("+"):
                out[k] = None if v is None else "+1555010000"
            elif k == "resourceName" and isinstance(v, str):
                if v not in id_map:
                    id_map[v] = f"people/c{1000000000000000000 + len(id_map)}"
                out[k] = id_map[v]
            elif k == "id" and isinstance(v, str) and len(v) >= 16 and v.isalnum():
                if v not in id_map:
                    id_map[v] = f"{len(id_map)+1:016x}"
                out[k] = id_map[v]
            elif k == "biographies":
                out[k] = []
            elif k == "biography" and isinstance(v, str):
                out[k] = ""
            else:
                out[k] = scrub(v)
        return out
    if isinstance(obj, list):
        return [scrub(x) for x in obj]
    return obj


def main():
    os.makedirs(DST, exist_ok=True)
    picks = {
        "calendarList": "*calendarList*.json",
        "events-primary": "*primary-events_maxResult*250*.json",
        "events-singleEvents": "*singleEvents=true*",
        "instances-payday": "*_60q30c1g60o30e1-instances*.json",
        "checkpoint-event": "*oovgd8dorgkfp556.json",
        "contacts-connections": sorted(glob.glob(f"{SRC}/*connections*"))[-1],
        "contact-groups": "*contactGroups*.json",
    }
    for label, pattern in picks.items():
        files = sorted(glob.glob(f"{SRC}/{pattern}")) if "*" in str(pattern) else [pattern]
        if not files:
            print(f"SKIP {label}: no match")
            continue
        src = files[-1] if isinstance(files, list) else files
        d = json.load(open(src))
        scrubbed = deepscrub(scrub(d))
        dst = f"{DST}/{label}.json"
        json.dump(scrubbed, open(dst, "w"), indent=1, sort_keys=False)
        print(f"{label}: {src} -> {dst}")




def deepscrub(o, email_map=None, counter=None):
    """Belt-and-suspenders final pass: regex-scrub any string value for
    email/phone shapes the key-based rules missed."""
    import re
    EMAIL = re.compile(r'[A-Za-z0-9._%+\-]+@(?!example\.com)[A-Za-z0-9.\-]+\.[A-Za-z]{2,}')
    PHONE = re.compile(r'\+?1?[ \-.]?\(?\d{3}\)?[ \-.]\d{3}[ \-.]\d{4}')
    if email_map is None:
        email_map = {}
    if counter is None:
        counter = [2000]
    def sub_email(m):
        e = m.group(0)
        if e not in email_map:
            email_map[e] = "person%d@example.com" % (len(email_map) + 101)
        return email_map[e]
    def sub_phone(m):
        counter[0] += 1
        return "+1555-%04d" % counter[0]
    if isinstance(o, dict):
        return {k: deepscrub(v, email_map, counter) for k, v in o.items()}
    if isinstance(o, list):
        return [deepscrub(x, email_map, counter) for x in o]
    if isinstance(o, str):
        return PHONE.sub(sub_phone, EMAIL.sub(sub_email, o))
    return o


if __name__ == "__main__":
    sys.exit(main())
