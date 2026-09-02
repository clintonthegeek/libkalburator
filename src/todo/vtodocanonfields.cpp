#include "vtodocanonfields.h"

#include "canonenvelope.h"
#include "icalcomponentscan.h"
#include "icaltimestamp.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::canonicalDigest;

KCalendarCore::Todo::Ptr parseTodo(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(data));
    return inc.dynamicCast<KCalendarCore::Todo>();
}

QByteArray serializeTodo(const KCalendarCore::Todo::Ptr &todo)
{
    if (!todo)
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(todo).toUtf8();
}

/// Encode a KCalendarCore DateTime to a JSON object:
///   { "dateTime": <ISO>, "tz": <iana-id>, "floating": <bool> }
/// For date-only: { "date": <ISO-date>, "allDay": true }
QJsonObject dateTimeToJson(const QDateTime &dt, bool allDay = false)
{
    if (!dt.isValid())
        return {};
    QJsonObject obj;
    if (allDay || (dt.time() == QTime(0, 0) && dt.timeSpec() == Qt::LocalTime)) {
        obj.insert(QStringLiteral("date"), dt.date().toString(Qt::ISODate));
        obj.insert(QStringLiteral("allDay"), true);
    } else {
        obj.insert(QStringLiteral("dateTime"), dt.toUTC().toString(Qt::ISODate));
        const bool floating = (dt.timeSpec() == Qt::LocalTime);
        obj.insert(QStringLiteral("floating"), floating);
        if (!floating && dt.timeSpec() == Qt::TimeZone) {
            const QString tzId = QString::fromLatin1(dt.timeZone().id());
            if (!tzId.isEmpty())
                obj.insert(QStringLiteral("tz"), tzId);
        }
    }
    return obj;
}

/// Reverse of dateTimeToJson.
QDateTime jsonToDateTime(const QJsonObject &obj)
{
    if (obj.isEmpty())
        return {};
    if (obj.contains(QStringLiteral("date"))) {
        const QDate d = QDate::fromString(
            obj.value(QStringLiteral("date")).toString(), Qt::ISODate);
        return d.isValid() ? QDateTime(d, QTime(0,0,0), QTimeZone::utc()) : QDateTime{};
    }
    const QString dtStr = obj.value(QStringLiteral("dateTime")).toString();
    if (dtStr.isEmpty())
        return {};
    const QDateTime dtUtc = QDateTime::fromString(dtStr, Qt::ISODate);
    if (!dtUtc.isValid())
        return {};
    const QString tzId = obj.value(QStringLiteral("tz")).toString();
    if (tzId.isEmpty())
        return dtUtc;
    const QTimeZone tz(tzId.toLatin1());
    return tz.isValid() ? dtUtc.toTimeZone(tz) : dtUtc;
}

/// Status string from KCalendarCore status enum.
QString statusToString(KCalendarCore::Incidence::Status s)
{
    switch (s) {
    case KCalendarCore::Incidence::StatusNeedsAction:  return QStringLiteral("needsAction");
    case KCalendarCore::Incidence::StatusInProcess:    return QStringLiteral("inProcess");
    case KCalendarCore::Incidence::StatusCompleted:    return QStringLiteral("completed");
    case KCalendarCore::Incidence::StatusCanceled:     return QStringLiteral("cancelled");
    default:                                           return {};
    }
}

/// Reverse of statusToString.
KCalendarCore::Incidence::Status statusFromString(const QString &s)
{
    if (s == QStringLiteral("needsAction"))  return KCalendarCore::Incidence::StatusNeedsAction;
    if (s == QStringLiteral("inProcess"))    return KCalendarCore::Incidence::StatusInProcess;
    if (s == QStringLiteral("completed"))    return KCalendarCore::Incidence::StatusCompleted;
    if (s == QStringLiteral("cancelled"))    return KCalendarCore::Incidence::StatusCanceled;
    return KCalendarCore::Incidence::StatusNone;
}

/// W4 — parse an org-mode completion-anchored repeater marker (the value
/// of a generic X-ORG-REPEATER custom property, e.g. ".+1w" / "++2d")
/// into a `completionAnchor` canon JSON object. Mirrors org-io's regex
/// (`(\.\+|\+\+|\+)(\d+)([hdwmy])`; longest sigil first so `.+`/`++` are
/// not shadowed by the bare `+` alternative) but only CatchUp (`++`) and
/// Restart (`.+`) are in W4 scope — a bare `+` is a plain
/// RFC5545-representable recurrence (Cumulative), not a completion
/// anchor, and returns an empty (invalid) object, same as anything
/// unparseable.
QJsonObject parseOrgRepeaterToCompletionAnchor(const QString& marker)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(\.\+|\+\+|\+)(\d+)([hdwmy])$)"));
    const QRegularExpressionMatch m = re.match(marker.trimmed());
    if (!m.hasMatch())
        return {};
    const QString sigil = m.captured(1);
    QString type;
    if (sigil == QStringLiteral(".+"))
        type = QStringLiteral("restart");
    else if (sigil == QStringLiteral("++"))
        type = QStringLiteral("catchUp");
    else
        return {};  // bare '+' == Cumulative — out of W4 scope
    const int interval = m.captured(2).toInt();
    const QString unit = m.captured(3);
    if (interval <= 0)
        return {};
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), type);
    obj.insert(QStringLiteral("interval"), interval);
    obj.insert(QStringLiteral("unit"), unit);
    return obj;
}

/// W4 — unit alphabet ([hdwmy], mirrors org-io) → RFC5545 RRULE FREQ,
/// for the derived-recurrence demote. Mirrors the mapping style of
/// recurrencepatternconverter.cpp's vocabulary tables.
QString completionAnchorFreqForUnit(const QString& unit)
{
    if (unit == QStringLiteral("h")) return QStringLiteral("HOURLY");
    if (unit == QStringLiteral("d")) return QStringLiteral("DAILY");
    if (unit == QStringLiteral("w")) return QStringLiteral("WEEKLY");
    if (unit == QStringLiteral("m")) return QStringLiteral("MONTHLY");
    if (unit == QStringLiteral("y")) return QStringLiteral("YEARLY");
    return {};
}

} // namespace

namespace Kalburator::Todo {

QJsonObject todoFieldsToCanon(const KCalendarCore::Todo::Ptr& todo,
                              const QByteArray& originalBytes)
{
    QJsonObject obj;

    // ---- created / lastModified --------------------------------------------
    // Phase B5 finding (same fix as eventcanonfields.cpp — see its comment):
    // KCalendarCore::Incidence::created()/lastModified() default to
    // construction-time "now" when the source has no explicit CREATED/
    // LAST-MODIFIED property, so trusting them directly re-derives a
    // different "now" on every independent re-parse of the same bytes and
    // permanently defeats change detection for such VTODOs.
    const QDateTime created = Kalburator::Calendar::extractICalPropertyLiteral(
        originalBytes, QStringLiteral("CREATED"));
    const QDateTime lastMod = Kalburator::Calendar::extractICalPropertyLiteral(
        originalBytes, QStringLiteral("LAST-MODIFIED"));
    if (created.isValid())
        obj.insert(QStringLiteral("created"),      created.toUTC().toString(Qt::ISODate));
    if (lastMod.isValid())
        obj.insert(QStringLiteral("lastModified"), lastMod.toUTC().toString(Qt::ISODate));

    // ---- summary / description ---------------------------------------------
    const QString summary = todo->summary();
    const QString description = todo->description();
    if (!summary.isEmpty())
        obj.insert(QStringLiteral("summary"), summary);
    if (!description.isEmpty())
        obj.insert(QStringLiteral("description"), description);

    // ---- descriptionHtml (X-ALT-DESC) — Reversible carrier -----------------
    {
        const QString altDesc = todo->nonKDECustomProperty("X-ALT-DESC");
        if (!altDesc.isEmpty())
            obj.insert(QStringLiteral("descriptionHtml"), altDesc);
    }

    // ---- status ------------------------------------------------------------
    {
        const QString status = statusToString(todo->status());
        if (!status.isEmpty())
            obj.insert(QStringLiteral("status"), status);
    }

    // ---- percentComplete ---------------------------------------------------
    {
        const int pct = todo->percentComplete();
        if (pct > 0)
            obj.insert(QStringLiteral("percentComplete"), pct);
    }

    // ---- priority ----------------------------------------------------------
    {
        const int pri = todo->priority();
        if (pri > 0)
            obj.insert(QStringLiteral("priority"), pri);
    }

    // ---- categories --------------------------------------------------------
    {
        const QStringList cats = todo->categories();
        if (!cats.isEmpty()) {
            QJsonArray arr;
            for (const auto& c : cats)
                arr.append(c);
            obj.insert(QStringLiteral("categories"), arr);
        }
    }

    // ---- start / due (W6.2 malformed-input coercion) -----------------------
    // Probe-confirmed (2026-08-28, KCalendarCore::ICalFormat): a DTSTART/DUE
    // DATE-vs-DATE-TIME mismatch survives independent parsing — dtStart()/
    // dtDue() come back as two ordinary QDateTimes, each still individually
    // detectable as date-only via the same heuristic dateTimeToJson already
    // uses (time()==00:00 && timeSpec()==LocalTime); KCalendarCore's single
    // incidence-level allDay() flag reflects only DUE's date-only-ness, not
    // a fused/collapsed view, so it is NOT used for detection here — this is
    // the "branch 1" path from the W6.2 recon's Open decision 5, resolved by
    // probe rather than assumed.
    {
        QDateTime start = todo->dtStart();
        QDateTime due = todo->dtDue();

        // Rule (a): DTSTART/DUE DATE-vs-DATE-TIME mismatch ⇒ coerce START to
        // DUE's type (binding response-doc wording: 2026-08-25-vtodo-parity-
        // handoff-response.md §W6 point 2). This is DELIBERATELY not tasks.org's
        // own (symmetric) rule — tasks.org rewrites whichever side is
        // DATE-only up to match the DATE-TIME side, regardless of which
        // property that is; the binding text instead always defers to DUE's
        // type, even when that means truncating a DATE-TIME DTSTART down to
        // DATE-only. See the VP.f return receipt for the explicit divergence
        // note (Open Decision 4).
        if (start.isValid() && due.isValid()) {
            const auto isDateOnly = [](const QDateTime& dt) {
                return dt.time() == QTime(0, 0) && dt.timeSpec() == Qt::LocalTime;
            };
            const bool startDateOnly = isDateOnly(start);
            const bool dueDateOnly = isDateOnly(due);
            if (startDateOnly != dueDateOnly) {
                if (dueDateOnly) {
                    // START is DATE-TIME, DUE is DATE-only ⇒ truncate START to
                    // a DATE-only value at the same calendar date.
                    start = QDateTime(start.date(), QTime(0, 0), Qt::LocalTime);
                } else {
                    // START is DATE-only, DUE is DATE-TIME ⇒ promote START to a
                    // DATE-TIME value: midnight, in DUE's zone (falls back to
                    // UTC when DUE itself carries no explicit zone — e.g. a
                    // floating DUE — since a DATE-only value has no zone of
                    // its own to inherit).
                    QTimeZone tz = due.timeZone();
                    if (!tz.isValid())
                        tz = QTimeZone::utc();
                    start = QDateTime(start.date(), QTime(0, 0), tz);
                }
            }
        }

        // Rule (b): DUE <= DTSTART ⇒ drop DTSTART from canon (evaluated AFTER
        // rule (a)'s reconciliation, so the comparison uses already-coerced
        // values per the recon's design sketch step 3).
        const bool dropStart = start.isValid() && due.isValid() && due <= start;

        if (start.isValid() && !dropStart) {
            const QJsonObject startObj = dateTimeToJson(start);
            if (!startObj.isEmpty())
                obj.insert(QStringLiteral("start"), startObj);
        }
        if (due.isValid()) {
            const QJsonObject dueObj = dateTimeToJson(due);
            if (!dueObj.isEmpty())
                obj.insert(QStringLiteral("due"), dueObj);
        }
        // Rule (c): DURATION-without-DTSTART ⇒ drop DURATION. Probe-confirmed
        // (2026-08-28) zero-code no-op: KCalendarCore::Todo exposes no direct
        // DURATION accessor, and ICalFormat's parser resolves a DURATION
        // property into dtDue() at parse time — with no DTSTART to add the
        // duration to, dtDue() simply comes back invalid, so there is nothing
        // for this promote code to do. Pinned by
        // vtodoPromoteDropsDurationWithoutDtstart in tst_todo_canon_roundtrip.cpp.
    }

    // ---- completed ---------------------------------------------------------
    {
        const QDateTime comp = todo->completed();
        if (comp.isValid())
            obj.insert(QStringLiteral("completed"), comp.toUTC().toString(Qt::ISODate));
    }

    // ---- recurrence (verbatim lines — invariant 3) -------------------------
    {
        const QStringList recLines = Kalburator::Calendar::extractComponentRecurrenceLines(
            originalBytes, "VTODO", todo->uid());
        if (!recLines.isEmpty()) {
            QJsonArray arr;
            for (const auto& l : recLines)
                arr.append(l);
            obj.insert(QStringLiteral("recurrence"), arr);
        }
    }

    // ---- recurrenceId / recurrenceRange ------------------------------------
    // Mirrors the event path (eventcanonfields.cpp): detached-exception
    // identity is promoted so an exception VTODO is distinguishable from its
    // master in canon (vtodo-parity VP.c-step-1a).
    {
        const QDateTime recId = todo->recurrenceId();
        if (recId.isValid()) {
            QJsonObject recIdObj;
            recIdObj.insert(QStringLiteral("dateTime"), recId.toUTC().toString(Qt::ISODate));
            obj.insert(QStringLiteral("recurrenceId"), recIdObj);

            // RANGE=THISANDFUTURE → recurrenceRange
            if (todo->thisAndFuture())
                obj.insert(QStringLiteral("recurrenceRange"), QStringLiteral("thisAndFuture"));
        }
    }

    // ---- seriesSplitOf (W3) — X-CANON-SERIES-SPLIT-OF carrier -------------
    // Canon-only key (no pre-existing X-prop this rides "for free" the way
    // providerExtras["x-vtodo"] round-trips props already on an incidence)
    // linking a series-split new master back to its old master's uid —
    // Reversible carrier, mirrors the X-ALT-DESC precedent above. See
    // docs/campaign/vtodo-parity/2026-08-27-w3-series-split-contract.md.
    {
        const QString splitOf = todo->nonKDECustomProperty("X-CANON-SERIES-SPLIT-OF");
        if (!splitOf.isEmpty())
            obj.insert(QStringLiteral("seriesSplitOf"), splitOf);
    }

    // ---- completionAnchor (W4) — generic X-ORG-REPEATER promote seam ------
    // Recognizes a custom X-ORG-REPEATER property carrying an org-mode
    // completion-anchored repeater marker (".+1w" Restart / "++2d"
    // CatchUp) and derives the catalogued completionAnchor key so the
    // differ treats an anchor advance as an ordinary field change (never
    // a conflict — see tests/shape/tst_canonjson_diff_merge.cpp).
    //
    // NOTE (W4 open decision 1, org-leg wiring): org-io stores the
    // repeater ONLY in OrgRoundtripData.repeaterString, off-incidence,
    // and deliberately does NOT put X-ORG-REPEATER on the incidence
    // itself (incidence-purity invariant pinned by
    // tst_orgbackend_external.cpp:611-615,631-634). This generic seam is
    // vendor-agnostic and fully testable without org-io. Wiring
    // OrgBackend to inject X-ORG-REPEATER from m_roundtripData at fetch
    // time (respecting incidence purity — injected at the canon-promote
    // boundary, never by mutating the incidence) is DEFERRED: this repo's
    // standalone build cannot link `planstan-org-io`
    // (KALBURATOR_HAVE_ORG_IO=ON requires a host project to supply that
    // target; a standalone `cmake -DKALBURATOR_HAVE_ORG_IO=ON` here fails
    // at moc time with "orgfilemanager.h: No such file or directory" —
    // verified 2026-08-27, see the W4 return receipt). TODO: wire
    // OrgBackend once an org-io-enabled build is available to verify
    // against the purity pins above.
    {
        const QString repeaterMarker = todo->nonKDECustomProperty("X-ORG-REPEATER");
        if (!repeaterMarker.isEmpty()) {
            const QJsonObject anchor = parseOrgRepeaterToCompletionAnchor(repeaterMarker);
            if (!anchor.isEmpty())
                obj.insert(QStringLiteral("completionAnchor"), anchor);
        }
    }

    // ---- alarms (VALARM, W5 shape extension) --------------------------------
    // Additive JSON keys on top of the pre-existing {type, offset, text} row
    // shape (old rows stay valid: absent "at"/"related"/"repeatCount"/
    // "repeatIntervalSecs" means exactly what the pre-W5 shape meant).
    // hasTime()/hasEndOffset()/hasStartOffset() are mutually exclusive at the
    // KCalendarCore::Alarm level (probe-confirmed 2026-08-28) — promote just
    // preserves that exclusivity, checked in that priority order.
    //
    // W5 bug fix bundled in: pre-W5 code unconditionally read
    // alarm->startOffset() regardless of trigger form, so an absolute-
    // trigger or END-related alarm silently corrupted to a bogus
    // "offset: 0" on promote. This block now branches on the alarm's actual
    // trigger form instead.
    {
        const auto alarms = todo->alarms();
        if (!alarms.isEmpty()) {
            QJsonArray arr;
            for (const auto& alarm : alarms) {
                QJsonObject a;
                a.insert(QStringLiteral("type"), int(alarm->type()));
                if (alarm->hasTime()) {
                    a.insert(QStringLiteral("at"), alarm->time().toUTC().toString(Qt::ISODate));
                } else if (alarm->hasEndOffset()) {
                    a.insert(QStringLiteral("offset"), alarm->endOffset().asSeconds());
                    a.insert(QStringLiteral("related"), QStringLiteral("end"));
                } else {
                    // default / hasStartOffset() — unchanged pre-W5 shape.
                    a.insert(QStringLiteral("offset"), alarm->startOffset().asSeconds());
                }
                if (!alarm->text().isEmpty())
                    a.insert(QStringLiteral("text"), alarm->text());
                // REPEAT/DURATION pairing (Open decision 3, probe-confirmed
                // 2026-08-28): KCalendarCore::Alarm::snoozeTime() has a
                // nonzero CLASS DEFAULT (5 seconds) even when no DURATION
                // property was present in the source at all — it is NOT zero,
                // so "snoozeTime() != 0" cannot distinguish "explicit
                // DURATION" from "never set". There is no public API to
                // detect literal DURATION presence short of a raw-bytes
                // VALARM scanner (out of this item's scope). Promote
                // therefore emits the pair whenever repeatCount() > 0,
                // trusting whatever snoozeTime() KCalendarCore parsed
                // (falling back to its 5s class default for an
                // already-malformed REPEAT-without-DURATION source) — the
                // same "trust the parsed accessor" posture this file already
                // takes for the offset field, with no raw-bytes cross-check.
                if (alarm->repeatCount() > 0) {
                    a.insert(QStringLiteral("repeatCount"), alarm->repeatCount());
                    a.insert(QStringLiteral("repeatIntervalSecs"), alarm->snoozeTime().asSeconds());
                }
                arr.append(a);
            }
            obj.insert(QStringLiteral("alarms"), arr);
        }
    }

    // ---- location / geo ----------------------------------------------------
    {
        const QString location = todo->location();
        if (!location.isEmpty())
            obj.insert(QStringLiteral("location"), location);
    }
    {
        if (todo->hasGeo()) {
            QJsonObject geoObj;
            geoObj.insert(QStringLiteral("lat"), todo->geoLatitude());
            geoObj.insert(QStringLiteral("lon"), todo->geoLongitude());
            obj.insert(QStringLiteral("geo"), geoObj);
        }
    }

    // ---- relatedTo (RELATED-TO hierarchy) ----------------------------------
    {
        // KCalendarCore only exposes RelTypeParent (single RELATED-TO per type).
        const QString parentUid = todo->relatedTo(KCalendarCore::Incidence::RelTypeParent);
        if (!parentUid.isEmpty()) {
            QJsonArray arr;
            QJsonObject rel;
            rel.insert(QStringLiteral("uid"),     parentUid);
            rel.insert(QStringLiteral("reltype"), QStringLiteral("PARENT"));
            arr.append(rel);
            obj.insert(QStringLiteral("relatedTo"), arr);
        }
    }

    // ---- providerExtras["x-vtodo"] — unmapped X- properties ---------------
    {
        const auto customProps = todo->customProperties();
        if (!customProps.isEmpty()) {
            QJsonObject xvtodo;
            for (auto it = customProps.constBegin(); it != customProps.constEnd(); ++it)
                xvtodo.insert(QString::fromLatin1(it.key()), it.value());
            if (!xvtodo.isEmpty()) {
                QJsonObject extras;
                extras.insert(QStringLiteral("x-vtodo"), xvtodo);
                obj.insert(providerExtrasKey(), extras);

                // ---- providerExtrasDigest (O74) --------------------------
                // Fingerprint of the extras this promote captured, so the
                // catalogue-scoped differ (which never sees providerExtras
                // itself, by design) can still detect an X-prop-only edit.
                // No filtering needed on this leg: the vtodo/CalDAV extras
                // stash is genuine X- custom properties only — no vendor
                // bookkeeping (etag-equivalents, server timestamps) rides
                // this channel the way it does on the MS/Google legs.
                obj.insert(QStringLiteral("providerExtrasDigest"), canonicalDigest(xvtodo));
            }
        }
    }

    return obj;
}

QByteArray canonObjectToVtodoBytes(const QJsonObject& obj)
{
    if (obj.isEmpty())
        return {};

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo);

    // ---- uid ---------------------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            todo->setUid(uid);
    }

    // ---- created / lastModified --------------------------------------------
    // O41 write-side fix (same as eventcanonfields.cpp — see its comment):
    // strip the KCalendarCore-injected "now" default post-serialization
    // when canon never had the corresponding key.
    bool hadCreated = false;
    bool hadLastModified = false;
    {
        const QString created = obj.value(QStringLiteral("created")).toString();
        if (!created.isEmpty()) {
            const QDateTime dt = QDateTime::fromString(created, Qt::ISODate);
            if (dt.isValid()) {
                todo->setCreated(dt);
                hadCreated = true;
            }
        }
    }
    {
        const QString lastMod = obj.value(QStringLiteral("lastModified")).toString();
        if (!lastMod.isEmpty()) {
            const QDateTime dt = QDateTime::fromString(lastMod, Qt::ISODate);
            if (dt.isValid()) {
                todo->setLastModified(dt);
                hadLastModified = true;
            }
        }
    }

    // ---- summary / description ---------------------------------------------
    {
        const QString summary = obj.value(QStringLiteral("summary")).toString();
        if (!summary.isEmpty())
            todo->setSummary(summary);
    }
    {
        const QString description = obj.value(QStringLiteral("description")).toString();
        if (!description.isEmpty())
            todo->setDescription(description);
    }

    // ---- descriptionHtml → X-ALT-DESC (Reversible) ------------------------
    {
        const QString html = obj.value(QStringLiteral("descriptionHtml")).toString();
        if (!html.isEmpty())
            todo->setNonKDECustomProperty("X-ALT-DESC", html);
    }

    // ---- seriesSplitOf → X-CANON-SERIES-SPLIT-OF (Reversible, W3) ---------
    {
        const QString splitOf = obj.value(QStringLiteral("seriesSplitOf")).toString();
        if (!splitOf.isEmpty())
            todo->setNonKDECustomProperty("X-CANON-SERIES-SPLIT-OF", splitOf);
    }

    // ---- status ------------------------------------------------------------
    {
        const QString statusStr = obj.value(QStringLiteral("status")).toString();
        if (!statusStr.isEmpty()) {
            const KCalendarCore::Incidence::Status status = statusFromString(statusStr);
            if (status != KCalendarCore::Incidence::StatusNone)
                todo->setStatus(status);
            else {
                // Degraded: vendor-specific status not representable in VTODO. Map to
                // NEEDS-ACTION but keep the original verbatim (invariant 4) so it is
                // recoverable — emit as an X- custom property the forward stage will
                // round-trip back into providerExtras["x-vtodo"].
                todo->setStatus(KCalendarCore::Incidence::StatusNeedsAction);
                todo->setNonKDECustomProperty("X-CANON-STATUS", statusStr);
            }
        }
    }

    // ---- percentComplete ---------------------------------------------------
    {
        const QJsonValue pct = obj.value(QStringLiteral("percentComplete"));
        if (!pct.isUndefined())
            todo->setPercentComplete(pct.toInt());
    }

    // ---- priority ----------------------------------------------------------
    {
        const QJsonValue pri = obj.value(QStringLiteral("priority"));
        if (!pri.isUndefined())
            todo->setPriority(pri.toInt());
    }

    // ---- categories --------------------------------------------------------
    {
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty()) {
            QStringList catList;
            for (const auto& c : cats)
                catList << c.toString();
            todo->setCategories(catList);
        }
    }

    // ---- start / due (W6.2 bonus fix: DATE-value round-trip) ---------------
    // Probe-confirmed (2026-08-28): KCalendarCore's iCal writer decides
    // VALUE=DATE vs a full DATE-TIME purely from the incidence-level
    // allDay() flag — NOT from the QDateTime's own time-of-day/timeSpec
    // shape (a LocalTime-midnight QDateTime with allDay() left false still
    // serializes as a full DATE-TIME). Call order relative to setDtStart/
    // setDtDue does not matter (probe-verified both orders produce
    // identical output). Previously this block never called setAllDay(true)
    // at all, so a demoted {"date": ...} canon value silently re-emitted as
    // DTSTART/DUE:...T000000Z instead of DTSTART/DUE;VALUE=DATE:... — the
    // W6.2 bonus fix.
    bool hadExplicitStart = false;
    bool anyDateOnly = false;
    {
        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        if (!startObj.isEmpty()) {
            const QDateTime dt = jsonToDateTime(startObj);
            if (dt.isValid()) {
                todo->setDtStart(dt);
                hadExplicitStart = true;
                if (startObj.contains(QStringLiteral("date")))
                    anyDateOnly = true;
            }
        }
    }
    {
        const QJsonObject dueObj = obj.value(QStringLiteral("due")).toObject();
        if (!dueObj.isEmpty()) {
            const QDateTime dt = jsonToDateTime(dueObj);
            if (dt.isValid()) {
                todo->setDtDue(dt);
                if (dueObj.contains(QStringLiteral("date")))
                    anyDateOnly = true;
            }
        }
    }
    // Single incidence-level flag: promote's rule (a) coercion guarantees
    // start/due already agree on date-only-ness by the time they reach
    // canon, so setting this once for "either side is date-only" is safe —
    // there is no case where only one of a present pair is date-only.
    if (anyDateOnly)
        todo->setAllDay(true);

    // ---- completed ---------------------------------------------------------
    {
        const QString compStr = obj.value(QStringLiteral("completed")).toString();
        if (!compStr.isEmpty()) {
            const QDateTime dt = QDateTime::fromString(compStr, Qt::ISODate);
            if (dt.isValid())
                todo->setCompleted(dt);
        }
    }

    // ---- recurrence — re-inject verbatim RRULE/RDATE/EXDATE lines ----------
    // KCalendarCore cannot accept raw recurrence lines directly during
    // construction; we inject them via the serialised iCal text below.
    // For now, store them for post-serialization injection.
    const QJsonArray recurrenceArr = obj.value(QStringLiteral("recurrence")).toArray();

    // ---- completionAnchor → derived RRULE (W4) ------------------------------
    // Builds a standard RFC5545 RRULE from {interval, unit} so non-org
    // consumers see an ordinary recurring task; the verbatim org repeater
    // marker (if any) already rides providerExtras["x-vtodo"] via the
    // generic custom-prop re-emit below — no extra X-prop here (binding
    // spec: "no X-prop duplication").
    //
    // Only derives when canon carries no verbatim recurrence lines of its
    // own (invariant 3: a verbatim RRULE always wins). In practice this
    // never collides: completionAnchor is only promoted from an
    // X-ORG-REPEATER marker, and a VTODO carrying that marker does not
    // also carry a native RRULE — the guard is defensive.
    //
    // "Anchored at last completion" (binding spec) is encoded literally:
    // when canon has no explicit `start` of its own, an explicit DTSTART
    // matching `completed` is emitted alongside the derived RRULE so the
    // rule's RFC5545 anchor IS the completion timestamp. If canon already
    // carries an explicit `start`, that DTSTART is left untouched (must
    // not clobber a real scheduled start) and the derived RRULE rides on
    // it instead — a declared, narrow corner case (see the W4 return
    // receipt) since a completionAnchor-bearing org task ordinarily has
    // no DTSTART of its own.
    QByteArray derivedRecurrenceBytes;
    {
        const QJsonObject anchorObj = obj.value(QStringLiteral("completionAnchor")).toObject();
        if (recurrenceArr.isEmpty() && !anchorObj.isEmpty()) {
            const QString unit = anchorObj.value(QStringLiteral("unit")).toString();
            const int interval = anchorObj.value(QStringLiteral("interval")).toInt();
            const QString freq = completionAnchorFreqForUnit(unit);
            if (!freq.isEmpty() && interval > 0) {
                QString rrule = QStringLiteral("RRULE:FREQ=%1").arg(freq);
                if (interval != 1)
                    rrule += QStringLiteral(";INTERVAL=%1").arg(interval);
                derivedRecurrenceBytes = rrule.toUtf8();
                derivedRecurrenceBytes += '\n';

                if (!hadExplicitStart) {
                    const QString compStr = obj.value(QStringLiteral("completed")).toString();
                    if (!compStr.isEmpty()) {
                        const QDateTime dt = QDateTime::fromString(compStr, Qt::ISODate);
                        if (dt.isValid()) {
                            derivedRecurrenceBytes +=
                                (QStringLiteral("DTSTART:")
                                 + dt.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")))
                                    .toUtf8();
                            derivedRecurrenceBytes += '\n';
                        }
                    }
                }
            }
        }
    }

    // ---- recurrenceId / recurrenceRange ------------------------------------
    // Mirrors the event path (eventcanonfields.cpp): the canon object carries
    // the exception identity as {dateTime: <UTC ISO>}; KCalendarCore re-emits
    // RECURRENCE-ID at serialization.
    //
    // W3 (open decision 1) — RANGE=THISANDFUTURE is NEVER re-emitted on
    // write, unconditionally, regardless of what canon's `recurrenceRange`
    // carries. RANGE=THISANDFUTURE is write-hostile on real CalDAV servers;
    // the library's strategy is series-split (see
    // src/todo/todoseriessplitter.h and
    // docs/campaign/vtodo-parity/2026-08-27-w3-series-split-contract.md),
    // never this-and-future re-emission. `recurrenceRange` in canon is
    // therefore purely a READ-SIDE fact (an already-existing foreign
    // producer's write, captured losslessly by promote above) — this is a
    // hard safety backstop, independent of whether a split was ever
    // invoked. The bare exception identity (RECURRENCE-ID with no RANGE)
    // is unaffected and still fully Reversible.
    {
        const QJsonObject recIdObj = obj.value(QStringLiteral("recurrenceId")).toObject();
        if (!recIdObj.isEmpty()) {
            const QString dtStr = recIdObj.value(QStringLiteral("dateTime")).toString();
            if (!dtStr.isEmpty()) {
                const QDateTime dt = QDateTime::fromString(dtStr, Qt::ISODate);
                if (dt.isValid()) {
                    todo->setRecurrenceId(dt);
                    todo->setThisAndFuture(false);
                }
            }
        }
    }

    // ---- alarms (VALARM, W5 shape extension) --------------------------------
    {
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        for (const auto& av : alarms) {
            const QJsonObject a = av.toObject();
            KCalendarCore::Alarm::Ptr alarm(new KCalendarCore::Alarm(todo.data()));
            const int typeInt = a.value(QStringLiteral("type")).toInt();
            alarm->setType(static_cast<KCalendarCore::Alarm::Type>(typeInt));

            if (a.contains(QStringLiteral("at"))) {
                const QDateTime dt = QDateTime::fromString(
                    a.value(QStringLiteral("at")).toString(), Qt::ISODate);
                if (dt.isValid())
                    alarm->setTime(dt);
            } else {
                const int offsetSecs = a.value(QStringLiteral("offset")).toInt();
                if (a.value(QStringLiteral("related")).toString() == QStringLiteral("end"))
                    alarm->setEndOffset(KCalendarCore::Duration(offsetSecs));
                else
                    alarm->setStartOffset(KCalendarCore::Duration(offsetSecs));
            }

            const QString text = a.value(QStringLiteral("text")).toString();
            if (!text.isEmpty())
                alarm->setText(text);

            // REPEAT/DURATION: only ever synthesize the pair when BOTH canon
            // keys are present — an unpaired REPEAT or DURATION is itself
            // malformed per RFC5545 and must never be manufactured here.
            if (a.contains(QStringLiteral("repeatCount"))
                && a.contains(QStringLiteral("repeatIntervalSecs"))) {
                alarm->setRepeatCount(a.value(QStringLiteral("repeatCount")).toInt());
                alarm->setSnoozeTime(KCalendarCore::Duration(
                    a.value(QStringLiteral("repeatIntervalSecs")).toInt()));
            }

            todo->addAlarm(alarm);
        }
    }

    // ---- location / geo ----------------------------------------------------
    {
        const QString location = obj.value(QStringLiteral("location")).toString();
        if (!location.isEmpty())
            todo->setLocation(location);
    }
    {
        const QJsonObject geoObj = obj.value(QStringLiteral("geo")).toObject();
        if (!geoObj.isEmpty()) {
            const float lat = static_cast<float>(geoObj.value(QStringLiteral("lat")).toDouble());
            const float lon = static_cast<float>(geoObj.value(QStringLiteral("lon")).toDouble());
            todo->setGeoLatitude(lat);
            todo->setGeoLongitude(lon);
        }
    }

    // ---- relatedTo (RELATED-TO) --------------------------------------------
    {
        const QJsonArray rels = obj.value(QStringLiteral("relatedTo")).toArray();
        for (const auto& rv : rels) {
            const QJsonObject r = rv.toObject();
            const QString relUid    = r.value(QStringLiteral("uid")).toString();
            const QString reltype   = r.value(QStringLiteral("reltype")).toString();
            if (relUid.isEmpty())
                continue;
            // KCalendarCore only supports RelTypeParent; other reltypes are ignored.
            if (reltype.isEmpty() || reltype == QStringLiteral("PARENT"))
                todo->setRelatedTo(relUid, KCalendarCore::Incidence::RelTypeParent);
        }
    }

    // ---- parentUid → RELATED-TO;RELTYPE=PARENT (Reversible) ----------------
    // Only emit if not already covered by relatedTo above.
    {
        const QString parentUid = obj.value(QStringLiteral("parentUid")).toString();
        if (!parentUid.isEmpty()) {
            const QString existing = todo->relatedTo(KCalendarCore::Incidence::RelTypeParent);
            if (existing.isEmpty())
                todo->setRelatedTo(parentUid, KCalendarCore::Incidence::RelTypeParent);
        }
    }

    // ---- providerExtras["x-vtodo"] — re-emit custom/X- properties ----------
    {
        const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        const QJsonObject xvtodo = extras.value(QStringLiteral("x-vtodo")).toObject();
        for (auto it = xvtodo.constBegin(); it != xvtodo.constEnd(); ++it)
            todo->setNonKDECustomProperty(it.key().toLatin1(), it.value().toString());
    }

    // ---- linkedResources: Dropped (no VTODO representation) ----------------
    // (Nothing to do — Dropped means the data is intentionally not recoverable.)

    // ---- checklistItems / sortOrder: Reversible (stashed as X- carriers) ----
    // No native VTODO representation; emit verbatim so the forward stage round-
    // trips them into providerExtras["x-vtodo"] (invariant 4: Reversible).
    {
        const QJsonValue checklist = obj.value(QStringLiteral("checklistItems"));
        if (!checklist.isUndefined() && !checklist.isNull()) {
            const QByteArray j = (checklist.isArray()
                ? QJsonDocument(checklist.toArray())
                : QJsonDocument(checklist.toObject())).toJson(QJsonDocument::Compact);
            todo->setNonKDECustomProperty("X-CANON-CHECKLISTITEMS", QString::fromUtf8(j));
        }
        const QString sortOrder = obj.value(QStringLiteral("sortOrder")).toString();
        if (!sortOrder.isEmpty())
            todo->setNonKDECustomProperty("X-CANON-SORTORDER", sortOrder);
    }

    // ---- Serialize to iCal -------------------------------------------------
    QByteArray icalBytes = serializeTodo(todo);

    // ---- Strip KCalendarCore-injected created/lastModified defaults -------
    if (!hadCreated)
        icalBytes = Kalburator::Calendar::stripICalPropertyLine(icalBytes, QStringLiteral("CREATED"));
    if (!hadLastModified)
        icalBytes = Kalburator::Calendar::stripICalPropertyLine(icalBytes, QStringLiteral("LAST-MODIFIED"));

    // ---- Inject verbatim recurrence lines / derived completion-anchor RRULE ---
    // KCalendarCore's serialiser may not preserve recurrence lines verbatim.
    // Insert them into the VTODO block before END:VTODO. At most one of
    // recurrenceArr / derivedRecurrenceBytes is non-empty (guarded above).
    if ((!recurrenceArr.isEmpty() || !derivedRecurrenceBytes.isEmpty()) && !icalBytes.isEmpty()) {
        const QByteArray marker = "END:VTODO";
        const int pos = icalBytes.indexOf(marker);
        if (pos >= 0) {
            QByteArray recBytes;
            for (const auto& rv : recurrenceArr) {
                recBytes += rv.toString().toUtf8();
                recBytes += '\n';
            }
            recBytes += derivedRecurrenceBytes;
            icalBytes.insert(pos, recBytes);
        }
    }

    return icalBytes;
}

QList<Kalburator::Shape::PropertyId> vtodoCanonContributedIds()
{
    using Kalburator::Shape::PropertyId;
    // Order mirrors todoFieldsToCanon's own field-by-field body above.
    // Envelope keys (_canon/uid/providerExtras) are deliberately excluded.
    // NOTE: `sortOrder`, `parentUid`, `checklistItems`, `linkedResources`
    // are NOT here — todoFieldsToCanon never produces them at the top
    // level (they arrive from the Google Tasks / MS To-Do vendor stages
    // and are only ever *consumed*, not produced, by
    // canonObjectToVtodoBytes on demote). They stay vendor-only keys in
    // todocanonproperties.cpp — verified 2026-09-02, IP.3 receipt.
    return {
        PropertyId{QStringLiteral("created")},
        PropertyId{QStringLiteral("lastModified")},
        PropertyId{QStringLiteral("summary")},
        PropertyId{QStringLiteral("description")},
        PropertyId{QStringLiteral("descriptionHtml")},
        PropertyId{QStringLiteral("status")},
        PropertyId{QStringLiteral("percentComplete")},
        PropertyId{QStringLiteral("priority")},
        PropertyId{QStringLiteral("categories")},
        PropertyId{QStringLiteral("start")},
        PropertyId{QStringLiteral("due")},
        PropertyId{QStringLiteral("completed")},
        PropertyId{QStringLiteral("recurrence")},
        PropertyId{QStringLiteral("recurrenceId")},
        PropertyId{QStringLiteral("recurrenceRange")},
        PropertyId{QStringLiteral("seriesSplitOf")},
        PropertyId{QStringLiteral("completionAnchor")},
        PropertyId{QStringLiteral("alarms")},
        PropertyId{QStringLiteral("location")},
        PropertyId{QStringLiteral("geo")},
        PropertyId{QStringLiteral("relatedTo")},
        PropertyId{QStringLiteral("providerExtrasDigest")},
    };
}

}  // namespace Kalburator::Todo
