#include "googletaskcanonstages.h"

#include "canonenvelope.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTime>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::canonicalDigest;

QString strValue(const QJsonObject& o, const QString& key)
{
    const QJsonValue v = o.value(key);
    if (v.type() != QJsonValue::String)
        return {};
    return v.toString();
}

/// RFC3339 wire string → canon due Json. Date-only (midnight UTC) wires
/// take the {date, allDay} form; anything else keeps the wire string
/// verbatim so the round trip is byte-stable.
QJsonObject dueWireToJson(const QString& wire)
{
    QJsonObject obj;
    if (wire.isEmpty())
        return obj;
    const QDateTime dt =
        QDateTime::fromString(wire, Qt::ISODateWithMs);
    if (dt.isValid() && dt.time() == QTime(0, 0)
        && dt.timeSpec() == Qt::UTC) {
        obj.insert(QStringLiteral("date"),
                   dt.date().toString(Qt::ISODate));
        obj.insert(QStringLiteral("allDay"), true);
    } else {
        obj.insert(QStringLiteral("dateTime"), wire);
    }
    return obj;
}

QString dueJsonToWire(const QJsonObject& due)
{
    // Vendor discards time-of-day: any canon input degrades to its UTC
    // date part at midnight (declared Degraded).
    QString dateStr;
    if (due.contains(QStringLiteral("date"))) {
        dateStr = due.value(QStringLiteral("date")).toString();
    } else {
        const QDateTime dt = QDateTime::fromString(
            due.value(QStringLiteral("dateTime")).toString(),
            Qt::ISODateWithMs);
        dateStr = dt.isValid() ? dt.toUTC().date().toString(Qt::ISODate)
                               : QString();
    }
    const QDate d = QDate::fromString(dateStr, Qt::ISODate);
    if (d.isValid())
        return d.toString(Qt::ISODate) + QStringLiteral("T00:00:00Z");
    return {};
}

} // namespace

namespace Kalburator::Todo {

// ---------------------------------------------------------------------------
// Promote — Google Task JSON → canon JSON (lossless by construction)
// ---------------------------------------------------------------------------

QByteArray GoogleTaskToCanonStage::transform(const QByteArray& googleBytes) const
{
    if (googleBytes.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(googleBytes);
    if (!doc.isObject())
        return {};
    const QJsonObject task = doc.object();

    QJsonObject obj;
    QJsonObject extras;

    // ---- uid ← id -----------------------------------------------------------
    const QString id = strValue(task, QStringLiteral("id"));

    // ---- core text / status ---------------------------------------------------
    {
        const QString title = strValue(task, QStringLiteral("title"));
        if (!title.isEmpty())
            obj.insert(QStringLiteral("summary"), title);
        const QString notes = strValue(task, QStringLiteral("notes"));
        if (!notes.isEmpty())
            obj.insert(QStringLiteral("description"), notes);
        const QString status = strValue(task, QStringLiteral("status"));
        if (!status.isEmpty())
            obj.insert(QStringLiteral("status"), status);
    }

    // ---- times -----------------------------------------------------------------
    {
        const QJsonObject due = dueWireToJson(strValue(task, QStringLiteral("due")));
        if (!due.isEmpty())
            obj.insert(QStringLiteral("due"), due);
        const QString completed = strValue(task, QStringLiteral("completed"));
        if (!completed.isEmpty())
            obj.insert(QStringLiteral("completed"), completed);
        const QString updated = strValue(task, QStringLiteral("updated"));
        if (!updated.isEmpty())
            obj.insert(QStringLiteral("lastModified"), updated);
    }

    // ---- hierarchy: one-level parent + opaque sibling order ----------------------
    {
        const QString parent = strValue(task, QStringLiteral("parent"));
        if (!parent.isEmpty())
            obj.insert(QStringLiteral("parentUid"), parent);
        const QString position = strValue(task, QStringLiteral("position"));
        if (!position.isEmpty())
            obj.insert(QStringLiteral("sortOrder"), position);
    }

    // ---- everything unmapped → providerExtras["google"] verbatim ------------------
    // kind/etag/deleted/hidden/links/webViewLink/selfLink/assignmentInfo all
    // ride the stash (Google-only transport/extras per the declared profile).
    {
        static const QSet<QString> consumed = {
            QStringLiteral("id"), QStringLiteral("title"),
            QStringLiteral("notes"), QStringLiteral("status"),
            QStringLiteral("due"), QStringLiteral("completed"),
            QStringLiteral("updated"), QStringLiteral("parent"),
            QStringLiteral("position")
        };
        for (auto it = task.constBegin(); it != task.constEnd(); ++it)
            if (!consumed.contains(it.key()))
                extras.insert(it.key(), it.value());
    }

    if (!extras.isEmpty()) {
        QJsonObject wrap;
        wrap.insert(QStringLiteral("google"), extras);
        obj.insert(providerExtrasKey(), wrap);

        // ---- providerExtrasDigest (O74) — FILTERED, excludes `etag` --------
        // `etag` bumps on every server-side write regardless of whether the
        // edit touched anything otherwise uncatalogued — hashing it
        // unfiltered would make this digest spuriously "always dirty" on
        // every Google-side edit, defeating the whole point of the fix. The
        // other stashed fields (kind/deleted/hidden/links/webViewLink/
        // selfLink/assignmentInfo) are real content or stable transport
        // metadata, not per-write bookkeeping, so they stay hashed.
        QJsonObject filtered = extras;
        filtered.remove(QStringLiteral("etag"));
        if (!filtered.isEmpty())
            obj.insert(QStringLiteral("providerExtrasDigest"), canonicalDigest(filtered));
    }
    stampEnvelope(obj, QStringLiteral("todo"), id);
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// Demote — canon JSON → Google Task JSON (lossy per the declared profile)
// ---------------------------------------------------------------------------

QByteArray CanonToGoogleTaskStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};

    QJsonObject out;
    const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
    const QJsonObject extrasGoogle = extras.value(QStringLiteral("google")).toObject();

    static const QSet<QString> kRebuilt = {
        QStringLiteral("id"), QStringLiteral("title"),
        QStringLiteral("notes"), QStringLiteral("status"),
        QStringLiteral("due"), QStringLiteral("completed"),
        QStringLiteral("updated"), QStringLiteral("parent"),
        QStringLiteral("position")
    };
    for (auto it = extrasGoogle.constBegin(); it != extrasGoogle.constEnd(); ++it)
        if (!kRebuilt.contains(it.key()))
            out.insert(it.key(), it.value());

    // ---- uid → id ------------------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            out.insert(QStringLiteral("id"), uid);
    }

    // ---- core text -------------------------------------------------------------
    {
        const QString summary = obj.value(QStringLiteral("summary")).toString();
        if (!summary.isEmpty())
            out.insert(QStringLiteral("title"), summary);
        const QString description =
            obj.value(QStringLiteral("description")).toString();
        if (!description.isEmpty())
            out.insert(QStringLiteral("notes"), description);
    }

    // ---- status collapse (needsAction/completed only) ------------------------------
    {
        const QString status = obj.value(QStringLiteral("status")).toString();
        const bool hasCompleted =
            !obj.value(QStringLiteral("completed")).toString().isEmpty();
        if (status == QLatin1String("completed") || hasCompleted)
            out.insert(QStringLiteral("status"), QStringLiteral("completed"));
        else
            out.insert(QStringLiteral("status"), QStringLiteral("needsAction"));
    }

    // ---- times ------------------------------------------------------------------------
    {
        const QJsonObject due = obj.value(QStringLiteral("due")).toObject();
        const QString dueWire = dueJsonToWire(due);
        if (!dueWire.isEmpty())
            out.insert(QStringLiteral("due"), dueWire);

        const QString completed =
            obj.value(QStringLiteral("completed")).toString();
        if (!completed.isEmpty())
            out.insert(QStringLiteral("completed"), completed);

        const QString lastModified =
            obj.value(QStringLiteral("lastModified")).toString();
        if (!lastModified.isEmpty())
            out.insert(QStringLiteral("updated"), lastModified);
    }

    // ---- hierarchy -----------------------------------------------------------------------
    {
        const QString parentUid = obj.value(QStringLiteral("parentUid")).toString();
        if (!parentUid.isEmpty())
            out.insert(QStringLiteral("parent"), parentUid);
        const QString sortOrder = obj.value(QStringLiteral("sortOrder")).toString();
        if (!sortOrder.isEmpty())
            out.insert(QStringLiteral("position"), sortOrder);
    }

    // Dropped rulings (no Task home, no carrier channel): percentComplete,
    // priority, categories, start, recurrence, alarms, location, geo,
    // checklistItems, relatedTo, descriptionHtml — deliberately not emitted.

    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------------------
// LossProfile — canon → google-task demote
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToGoogleTaskLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Degraded: vendor discards time-of-day on due
    p.affected.insert(PropertyId{QStringLiteral("due")}, LossKind::Degraded);

    // Simplified: status vocabulary collapses to needsAction/completed
    p.affected.insert(PropertyId{QStringLiteral("status")}, LossKind::Simplified);

    // Dropped: no Task home and NO carrier channel (declared profile §Carrier)
    // completionAnchor (W4) joins recurrence here: Google Tasks has no
    // recurrence field of any kind, so the derived standard form is
    // equally unrepresentable. seriesSplitOf (W3) joins the same list —
    // Google Tasks has no extension point of any kind (O66(c)).
    for (const char* prop : { "percentComplete", "priority", "categories",
                              "start", "recurrence", "completionAnchor",
                              "seriesSplitOf",
                              "alarms", "location",
                              "geo", "checklistItems", "relatedTo",
                              "descriptionHtml" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(prop)},
                          LossKind::Dropped);
    }
    // providerExtrasDigest (O74): purely derived/meta, no wire representation
    // by design on any leg — not a traditional information loss.
    p.affected.insert(PropertyId{QStringLiteral("providerExtrasDigest")},
                      LossKind::Dropped);
    return p;
}

}  // namespace Kalburator::Todo
