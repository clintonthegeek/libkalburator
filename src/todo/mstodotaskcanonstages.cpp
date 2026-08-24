#include "mstodotaskcanonstages.h"

#include "canonenvelope.h"
#include "recurrencepatternconverter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Calendar::RecurrencePattern::patternedRecurrenceToRruleLines;
using Kalburator::Calendar::RecurrencePattern::rruleLinesToPatternedRecurrence;

constexpr auto kCarrierPrefix = "x-canon-";
constexpr auto kExtensionName = "kalburator.canon";
constexpr auto kRecurrenceCarrierKey = "x-canon-recurrence";

QString carrierKey(const QString& propId)
{
    QString kebab;
    for (const QChar c : propId) {
        if (c.isUpper()) {
            kebab += QLatin1Char('-');
            kebab += c.toLower();
        } else {
            kebab += c;
        }
    }
    return QLatin1String(kCarrierPrefix) + kebab;
}

QString propFromCarrierKey(const QString& key)
{
    if (!key.startsWith(QLatin1String(kCarrierPrefix)))
        return {};
    const QString suffix = key.mid(int(qstrlen(kCarrierPrefix)));
    QString camel;
    bool upper = false;
    for (const QChar c : suffix) {
        if (c == QLatin1Char('-')) {
            upper = true;
        } else if (upper) {
            camel += c.toUpper();
            upper = false;
        } else {
            camel += c;
        }
    }
    return camel;
}

QString valueToCarrierString(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (d == qint64(d))
            return QString::number(qint64(d));
        return QString::number(d);
    }
    case QJsonValue::String: return v.toString();
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    default:                 return {};
    }
}

QJsonValue carrierStringToValue(const QJsonValue& v)
{
    if (v.type() != QJsonValue::String)
        return v;
    const QString s = v.toString();
    if (s == QStringLiteral("true"))   return QJsonValue(true);
    if (s == QStringLiteral("false"))  return QJsonValue(false);
    bool ok = false;
    const qlonglong i = s.toLongLong(&ok);
    if (ok && QString::number(i) == s)
        return QJsonValue(double(i));
    const QChar first = s.isEmpty() ? QChar() : s.at(0);
    if (first == QLatin1Char('{') || first == QLatin1Char('[')) {
        const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
        if (doc.isObject())
            return QJsonValue(doc.object());
        if (doc.isArray())
            return QJsonValue(doc.array());
    }
    return QJsonValue(s);
}

QString strValue(const QJsonObject& o, const QString& key)
{
    const QJsonValue v = o.value(key);
    if (v.type() != QJsonValue::String)
        return {};
    return v.toString();
}

/// Graph dateTimeTimeZone → canon time Json ({dateTime, tz} verbatim).
QJsonObject msDateTimeToJson(const QJsonObject& ms)
{
    QJsonObject obj;
    const QString dt = strValue(ms, QStringLiteral("dateTime"));
    if (!dt.isEmpty())
        obj.insert(QStringLiteral("dateTime"), dt);
    const QString tz = strValue(ms, QStringLiteral("timeZone"));
    if (!tz.isEmpty())
        obj.insert(QStringLiteral("tz"), tz);
    return obj;
}

/// Canon time Json → Graph dateTimeTimeZone. Date-only/allDay canon input
/// degrades to midnight UTC; floating input pins UTC (Graph requires a
/// timeZone string).
QJsonObject jsonToMsDateTime(const QJsonObject& obj)
{
    QJsonObject out;
    if (obj.contains(QStringLiteral("date"))) {
        out.insert(QStringLiteral("dateTime"),
                   obj.value(QStringLiteral("date")).toString()
                       + QStringLiteral("T00:00:00"));
        out.insert(QStringLiteral("timeZone"), QStringLiteral("UTC"));
        return out;
    }
    const QString dt = strValue(obj, QStringLiteral("dateTime"));
    if (dt.isEmpty())
        return out;
    out.insert(QStringLiteral("dateTime"), dt);
    QString tz = strValue(obj, QStringLiteral("tz"));
    if (tz.isEmpty())
        tz = QStringLiteral("UTC");
    out.insert(QStringLiteral("timeZone"), tz);
    return out;
}

int importanceToPriority(const QString& importance)
{
    if (importance == QLatin1String("low"))   return 9;
    if (importance == QLatin1String("high"))  return 1;
    return 5;  // normal
}

QString priorityToImportance(int priority)
{
    if (priority >= 8) return QStringLiteral("low");
    if (priority >= 4) return QStringLiteral("normal");
    return QStringLiteral("high");
}

} // namespace

namespace Kalburator::Todo {

// ---------------------------------------------------------------------------
// Promote — todoTask JSON → canon JSON (lossless by construction)
// ---------------------------------------------------------------------------

QByteArray MsTodoTaskToCanonStage::transform(const QByteArray& msBytes) const
{
    if (msBytes.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(msBytes);
    if (!doc.isObject())
        return {};
    const QJsonObject task = doc.object();

    QJsonObject obj;
    QJsonObject extras;

    // ---- uid ← id ------------------------------------------------------------
    const QString id = strValue(task, QStringLiteral("id"));

    // ---- core text / status / importance -----------------------------------------
    {
        const QString title = strValue(task, QStringLiteral("title"));
        if (!title.isEmpty())
            obj.insert(QStringLiteral("summary"), title);

        const QJsonObject body = task.value(QStringLiteral("body")).toObject();
        if (!body.isEmpty()) {
            const QString content = strValue(body, QStringLiteral("content"));
            const QString contentType =
                strValue(body, QStringLiteral("contentType"));
            if (!content.isEmpty()) {
                if (contentType == QLatin1String("html"))
                    obj.insert(QStringLiteral("descriptionHtml"), content);
                else
                    obj.insert(QStringLiteral("description"), content);
            }
        }

        const QString status = strValue(task, QStringLiteral("status"));
        if (!status.isEmpty())
            obj.insert(QStringLiteral("status"), status);

        const QString importance = strValue(task, QStringLiteral("importance"));
        if (!importance.isEmpty())
            obj.insert(QStringLiteral("priority"),
                       importanceToPriority(importance));

        const QJsonArray cats = task.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty())
            obj.insert(QStringLiteral("categories"), cats);
    }

    // ---- times ----------------------------------------------------------------------
    {
        const QJsonObject due =
            msDateTimeToJson(task.value(QStringLiteral("dueDateTime")).toObject());
        if (!due.isEmpty())
            obj.insert(QStringLiteral("due"), due);
        const QJsonObject start =
            msDateTimeToJson(task.value(QStringLiteral("startDateTime")).toObject());
        if (!start.isEmpty())
            obj.insert(QStringLiteral("start"), start);

        const QJsonObject completedObj =
            task.value(QStringLiteral("completedDateTime")).toObject();
        if (!completedObj.isEmpty()) {
            // zone dropped to the plain string form (declared Simplified);
            // no verbatim stash — uid-anchored canon keeps C→MS→C symmetric
            const QString dt = strValue(completedObj, QStringLiteral("dateTime"));
            if (!dt.isEmpty())
                obj.insert(QStringLiteral("completed"), dt);
        }
    }

    // ---- single reminder → alarms[0] ----------------------------------------------------
    {
        const QJsonObject reminder =
            task.value(QStringLiteral("reminderDateTime")).toObject();
        if (!reminder.isEmpty()) {
            QJsonArray alarms;
            QJsonObject row;
            row.insert(QStringLiteral("reminder"), reminder);
            alarms.append(row);
            obj.insert(QStringLiteral("alarms"), alarms);
        }
    }

    // ---- recurrence: patternedRecurrence → RFC5545 lines ---------------------------------
    {
        const QJsonObject recurrence =
            task.value(QStringLiteral("recurrence")).toObject();
        if (!recurrence.isEmpty()) {
            const QStringList lines =
                patternedRecurrenceToRruleLines(recurrence);
            QJsonArray arr;
            for (const QString& l : lines)
                arr.append(l);
            obj.insert(QStringLiteral("recurrence"), arr);
        }
    }

    // ---- open-extension carriers → canon props ---------------------------------------------
    {
        QJsonArray remainder;
        for (const auto& ev :
             task.value(QStringLiteral("extensions")).toArray()) {
            const QJsonObject ext = ev.toObject();
            if (strValue(ext, QStringLiteral("extensionName"))
                    != QLatin1String(kExtensionName)) {
                remainder.append(ext);
                continue;
            }
            for (auto it = ext.constBegin(); it != ext.constEnd(); ++it) {
                if (it.key() == QLatin1String("extensionName")
                    || it.key().startsWith(QLatin1String("@odata")))
                    continue;
                const QString prop = propFromCarrierKey(it.key());
                if (prop.isEmpty()) {
                    remainder.append(ext);
                    break;
                }
                if (it.key() == QLatin1String(kRecurrenceCarrierKey)) {
                    // Carried lines hold the ORIGINAL rule set — they replace
                    // the lines re-derived from patternedRecurrence above.
                    obj.insert(QStringLiteral("recurrence"), it.value().toArray());
                    continue;
                }
                obj.insert(prop, carrierStringToValue(it.value()));
            }
        }
        if (!remainder.isEmpty())
            extras.insert(QStringLiteral("extensions"), remainder);
    }

    // ---- everything unmapped → providerExtras["msgraph"] verbatim ---------------------------
    {
        static const QSet<QString> consumed = {
            QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("body"),
            QStringLiteral("status"), QStringLiteral("importance"),
            QStringLiteral("categories"), QStringLiteral("dueDateTime"),
            QStringLiteral("startDateTime"), QStringLiteral("completedDateTime"),
            QStringLiteral("reminderDateTime"), QStringLiteral("recurrence"),
            QStringLiteral("extensions")
        };
        for (auto it = task.constBegin(); it != task.constEnd(); ++it)
            if (!consumed.contains(it.key()))
                extras.insert(it.key(), it.value());
    }

    if (!extras.isEmpty()) {
        QJsonObject wrap;
        wrap.insert(QStringLiteral("msgraph"), extras);
        obj.insert(providerExtrasKey(), wrap);
    }
    stampEnvelope(obj, QStringLiteral("todo"), id);
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// Demote — canon JSON → todoTask JSON (lossy per the declared profile)
// ---------------------------------------------------------------------------

QByteArray CanonToMsTodoTaskStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};

    QJsonObject out;
    QJsonArray extensionRows;

    const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
    const QJsonObject extrasMs = extras.value(QStringLiteral("msgraph")).toObject();

    static const QSet<QString> kRebuilt = {
        QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("body"),
        QStringLiteral("status"), QStringLiteral("importance"),
        QStringLiteral("categories"), QStringLiteral("dueDateTime"),
        QStringLiteral("startDateTime"), QStringLiteral("completedDateTime"),
        QStringLiteral("reminderDateTime"), QStringLiteral("recurrence"),
        QStringLiteral("extensions")
    };
    for (auto it = extrasMs.constBegin(); it != extrasMs.constEnd(); ++it)
        if (!kRebuilt.contains(it.key()))
            out.insert(it.key(), it.value());

    // ---- uid → id --------------------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            out.insert(QStringLiteral("id"), uid);
    }

    // ---- core text / status / importance ------------------------------------------
    {
        const QString summary = obj.value(QStringLiteral("summary")).toString();
        if (!summary.isEmpty())
            out.insert(QStringLiteral("title"), summary);

        const QString html =
            obj.value(QStringLiteral("descriptionHtml")).toString();
        const QString text =
            obj.value(QStringLiteral("description")).toString();
        if (!html.isEmpty()) {
            out.insert(QStringLiteral("body"),
                       QJsonObject{ { QStringLiteral("content"), html },
                                    { QStringLiteral("contentType"), QStringLiteral("html") } });
        } else if (!text.isEmpty()) {
            out.insert(QStringLiteral("body"),
                       QJsonObject{ { QStringLiteral("content"), text },
                                    { QStringLiteral("contentType"), QStringLiteral("text") } });
        }

        const QString status = obj.value(QStringLiteral("status")).toString();
        if (!status.isEmpty())
            out.insert(QStringLiteral("status"), status);

        const QJsonValue priority = obj.value(QStringLiteral("priority"));
        if (priority.isDouble())
            out.insert(QStringLiteral("importance"),
                       priorityToImportance(int(priority.toDouble())));

        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty())
            out.insert(QStringLiteral("categories"), cats);
    }

    // ---- times -------------------------------------------------------------------------
    {
        const QJsonObject due =
            jsonToMsDateTime(obj.value(QStringLiteral("due")).toObject());
        if (!due.isEmpty())
            out.insert(QStringLiteral("dueDateTime"), due);
        const QJsonObject start =
            jsonToMsDateTime(obj.value(QStringLiteral("start")).toObject());
        if (!start.isEmpty())
            out.insert(QStringLiteral("startDateTime"), start);

        const QString completed =
            obj.value(QStringLiteral("completed")).toString();
        if (!completed.isEmpty()) {
            const QJsonObject stash =
                extrasMs.value(QStringLiteral("completedDateTime")).toObject();
            if (strValue(stash, QStringLiteral("dateTime")) == completed) {
                // unchanged — re-emit the verbatim wire form
                out.insert(QStringLiteral("completedDateTime"), stash);
            } else {
                out.insert(QStringLiteral("completedDateTime"),
                           QJsonObject{ { QStringLiteral("dateTime"), completed },
                                        { QStringLiteral("timeZone"), QStringLiteral("UTC") } });
            }
        }
    }

    // ---- alarms[0] → single reminder --------------------------------------------------------
    {
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        if (!alarms.isEmpty()) {
            const QJsonObject reminder = alarms.at(0).toObject()
                                             .value(QStringLiteral("reminder"))
                                             .toObject();
            if (!reminder.isEmpty())
                out.insert(QStringLiteral("reminderDateTime"), reminder);
        }
    }

    // ---- recurrence: RFC5545 lines → pattern + carried rulings -------------------------------
    {
        QStringList lines;
        for (const auto& lv : obj.value(QStringLiteral("recurrence")).toArray())
            lines << lv.toString();

        // carrier wins over re-derivation when the carried set is present
        bool hasCarrierRecurrence = false;
        for (const auto& ev :
             extrasMs.value(QStringLiteral("extensions")).toArray()) {
            const QJsonObject ext = ev.toObject();
            if (strValue(ext, QStringLiteral("extensionName"))
                    != QLatin1String(kExtensionName))
                continue;
            const QJsonValue carried =
                ext.value(QLatin1String(kRecurrenceCarrierKey));
            if (carried.isArray()) {
                out.insert(QStringLiteral("recurrence"), carried.toArray());
                hasCarrierRecurrence = true;
            }
        }

        if (!lines.isEmpty() && !hasCarrierRecurrence) {
            const Kalburator::Calendar::RecurrencePattern::DemoteResult r =
                rruleLinesToPatternedRecurrence(lines);
            if (!r.patternedRecurrence.isEmpty())
                out.insert(QStringLiteral("recurrence"), r.patternedRecurrence);
            if (!r.carriedLines.isEmpty() || !r.exdates.isEmpty()) {
                // cannot-represent rulings ride the extension carrier
                QJsonArray carried;
                for (const QString& l : lines)
                    carried.append(l);
                QJsonObject carrierRow;
                carrierRow.insert(
                    QStringLiteral("@odata.type"),
                    QStringLiteral("microsoft.graph.openTypeExtension"));
                carrierRow.insert(QStringLiteral("extensionName"),
                                  QLatin1String(kExtensionName));
                carrierRow.insert(QLatin1String(kRecurrenceCarrierKey), carried);
                extensionRows.append(carrierRow);
            }
        }
    }

    // ---- unhandled canon props → open-extension carriers (never dropped) ----------------------
    {
        static const QSet<QString> handled = {
            QStringLiteral("uid"), QStringLiteral("summary"),
            QStringLiteral("description"), QStringLiteral("descriptionHtml"),
            QStringLiteral("status"), QStringLiteral("priority"),
            QStringLiteral("categories"), QStringLiteral("due"),
            QStringLiteral("start"), QStringLiteral("completed"),
            QStringLiteral("alarms"), QStringLiteral("recurrence")
            // percentComplete / relatedTo / parentUid / sortOrder / location /
            // geo are deliberately NOT handled — no todoTask home (declared
            // Dropped); checklistItems/linkedResources are separate-endpoint
            // nav collections (transport).
        };
        bool any = false;
        QJsonObject carrierRow;
        carrierRow.insert(QStringLiteral("@odata.type"),
                          QStringLiteral("microsoft.graph.openTypeExtension"));
        carrierRow.insert(QStringLiteral("extensionName"),
                          QLatin1String(kExtensionName));
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (handled.contains(it.key()))
                continue;
            if (it.key() == Kalburator::Shape::CanonEnvelope::canonKey()
                || it.key() == Kalburator::Shape::CanonEnvelope::uidKey()
                || it.key() == providerExtrasKey())
                continue;
            carrierRow.insert(carrierKey(it.key()),
                              valueToCarrierString(it.value()));
            any = true;
        }
        if (any)
            extensionRows.append(carrierRow);
    }

    // merge with stashed non-carrier extension rows
    {
        const QJsonArray stashed =
            extrasMs.value(QStringLiteral("extensions")).toArray();
        if (!extensionRows.isEmpty()) {
            QJsonArray merged = extensionRows;
            for (const auto& sv : stashed) {
                const QJsonObject so = sv.toObject();
                if (so.value(QStringLiteral("extensionName")).toString()
                    == QLatin1String(kExtensionName))
                    continue;  // rebuilt fresh above
                merged.append(so);
            }
            out.insert(QStringLiteral("extensions"), merged);
        } else if (!stashed.isEmpty()) {
            out.insert(QStringLiteral("extensions"), stashed);
        }
    }

    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------------------
// LossProfile — canon → ms-todotask demote
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToMsTodoTaskLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Degraded: many-to-one vocabulary mapping
    p.affected.insert(PropertyId{QStringLiteral("priority")}, LossKind::Degraded);

    // Simplified
    for (const char* prop : { "description", "descriptionHtml", "due",
                              "start", "completed", "alarms" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(prop)},
                          LossKind::Simplified);
    }
    // Reversible: recurrence cannot-represent rulings + unhandled canon
    // props ride the kalburator.canon open-extension carrier
    p.affected.insert(PropertyId{QStringLiteral("recurrence")},
                      LossKind::Reversible);
    for (const char* prop : { "percentComplete", "relatedTo", "parentUid",
                              "sortOrder", "location", "geo" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(prop)},
                          LossKind::Reversible);
    }
    // Dropped: no todoTask home AND out of edge scope (separate-endpoint nav)
    p.affected.insert(PropertyId{QStringLiteral("checklistItems")},
                      LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("linkedResources")},
                      LossKind::Dropped);
    return p;
}

}  // namespace Kalburator::Todo
