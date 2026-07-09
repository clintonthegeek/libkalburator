#include "vtodocanonfields.h"

#include "canonenvelope.h"
#include "icalcomponentscan.h"
#include "icaltimestamp.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;

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

    // ---- start / due -------------------------------------------------------
    {
        const QDateTime start = todo->dtStart();
        if (start.isValid()) {
            const QJsonObject startObj = dateTimeToJson(start);
            if (!startObj.isEmpty())
                obj.insert(QStringLiteral("start"), startObj);
        }
    }
    {
        const QDateTime due = todo->dtDue();
        if (due.isValid()) {
            const QJsonObject dueObj = dateTimeToJson(due);
            if (!dueObj.isEmpty())
                obj.insert(QStringLiteral("due"), dueObj);
        }
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

    // ---- alarms (VALARM) ---------------------------------------------------
    {
        const auto alarms = todo->alarms();
        if (!alarms.isEmpty()) {
            QJsonArray arr;
            for (const auto& alarm : alarms) {
                QJsonObject a;
                a.insert(QStringLiteral("type"),   int(alarm->type()));
                a.insert(QStringLiteral("offset"), alarm->startOffset().asSeconds());
                if (!alarm->text().isEmpty())
                    a.insert(QStringLiteral("text"), alarm->text());
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

    // ---- start / due -------------------------------------------------------
    {
        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        if (!startObj.isEmpty()) {
            const QDateTime dt = jsonToDateTime(startObj);
            if (dt.isValid())
                todo->setDtStart(dt);
        }
    }
    {
        const QJsonObject dueObj = obj.value(QStringLiteral("due")).toObject();
        if (!dueObj.isEmpty()) {
            const QDateTime dt = jsonToDateTime(dueObj);
            if (dt.isValid())
                todo->setDtDue(dt);
        }
    }

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

    // ---- alarms (VALARM) ---------------------------------------------------
    {
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        for (const auto& av : alarms) {
            const QJsonObject a = av.toObject();
            KCalendarCore::Alarm::Ptr alarm(new KCalendarCore::Alarm(todo.data()));
            const int typeInt = a.value(QStringLiteral("type")).toInt();
            alarm->setType(static_cast<KCalendarCore::Alarm::Type>(typeInt));
            const int offsetSecs = a.value(QStringLiteral("offset")).toInt();
            alarm->setStartOffset(KCalendarCore::Duration(offsetSecs));
            const QString text = a.value(QStringLiteral("text")).toString();
            if (!text.isEmpty())
                alarm->setText(text);
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

    // ---- Inject verbatim recurrence lines ----------------------------------
    // KCalendarCore's serialiser may not preserve recurrence lines verbatim.
    // Insert them into the VTODO block before END:VTODO.
    if (!recurrenceArr.isEmpty() && !icalBytes.isEmpty()) {
        const QByteArray marker = "END:VTODO";
        const int pos = icalBytes.indexOf(marker);
        if (pos >= 0) {
            QByteArray recBytes;
            for (const auto& rv : recurrenceArr) {
                recBytes += rv.toString().toUtf8();
                recBytes += '\n';
            }
            icalBytes.insert(pos, recBytes);
        }
    }

    return icalBytes;
}

}  // namespace Kalburator::Todo
