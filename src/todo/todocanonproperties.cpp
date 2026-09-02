#include "todocanonproperties.h"

#include "vtodocanonfields.h"

#include <QHash>
#include <QSet>

using namespace Kalburator::Shape;

namespace Kalburator::Todo {

namespace {

/// Kind + display-name metadata for every property id this catalogue may
/// carry. IP.3: the single declaration of what a property id *looks like*;
/// WHICH ids appear in the catalogue is decided by makeTodoCanonCatalogue()
/// below, from vtodoCanonContributedIds() plus todoVendorOnlyIds() — never
/// by hand-listing ids redundantly here.
struct PropertyMeta {
    PropertyKind kind;
    QString displayName;
};

const QHash<PropertyId, PropertyMeta>& todoPropertyMetadata()
{
    static const QHash<PropertyId, PropertyMeta> table = {
        // Timestamps
        { PropertyId{QStringLiteral("created")},          {PropertyKind::DateTime,   QStringLiteral("Created")} },
        { PropertyId{QStringLiteral("lastModified")},     {PropertyKind::DateTime,   QStringLiteral("Last Modified")} },
        // Core text fields
        { PropertyId{QStringLiteral("summary")},          {PropertyKind::String,     QStringLiteral("Summary")} },
        { PropertyId{QStringLiteral("description")},      {PropertyKind::String,     QStringLiteral("Description")} },
        { PropertyId{QStringLiteral("descriptionHtml")},  {PropertyKind::String,     QStringLiteral("Description (HTML)")} },
        // Status fields
        { PropertyId{QStringLiteral("status")},           {PropertyKind::String,     QStringLiteral("Status")} },
        { PropertyId{QStringLiteral("percentComplete")},  {PropertyKind::Integer,    QStringLiteral("Percent Complete")} },
        { PropertyId{QStringLiteral("priority")},         {PropertyKind::Integer,    QStringLiteral("Priority")} },
        // Classification
        { PropertyId{QStringLiteral("categories")},       {PropertyKind::StringList, QStringLiteral("Categories")} },
        // Time fields (Json to support tz + floating + precision)
        { PropertyId{QStringLiteral("start")},            {PropertyKind::Json,       QStringLiteral("Start")} },
        { PropertyId{QStringLiteral("due")},              {PropertyKind::Json,       QStringLiteral("Due")} },
        { PropertyId{QStringLiteral("completed")},        {PropertyKind::DateTime,   QStringLiteral("Completed")} },
        // Recurrence (verbatim RFC5545 lines — invariant 3)
        { PropertyId{QStringLiteral("recurrence")},       {PropertyKind::StringList, QStringLiteral("Recurrence")} },
        // Detached-exception identity (mirrors the event canon catalogue)
        { PropertyId{QStringLiteral("recurrenceId")},     {PropertyKind::Json,       QStringLiteral("Recurrence ID")} },
        { PropertyId{QStringLiteral("recurrenceRange")},  {PropertyKind::String,     QStringLiteral("Recurrence Range")} },
        // Series-split re-association (W3): links a series-split new
        // master back to its old master's uid (String = old master's uid).
        // Read/write on the vtodo/CalDAV leg via
        // X-CANON-SERIES-SPLIT-OF; auto-carries on MS To-Do (Reversible);
        // Dropped on Google Tasks (no extension point). See
        // docs/campaign/vtodo-parity/2026-08-27-w3-series-split-contract.md.
        { PropertyId{QStringLiteral("seriesSplitOf")},    {PropertyKind::String,     QStringLiteral("Series Split Of")} },
        // Completion-anchored recurrence (W4): derived standard form of an
        // org-mode completion-anchor repeater (".+1w" Restart / "++2d"
        // CatchUp) — {type, interval, unit}. Catalogued so the differ sees
        // an anchor advance as an ordinary field change (never a
        // conflict); the verbatim org string rides
        // providerExtras["x-vtodo"] via the generic custom-prop channel.
        { PropertyId{QStringLiteral("completionAnchor")}, {PropertyKind::Json,       QStringLiteral("Completion Anchor")} },
        // O74 — fingerprint of providerExtras content, computed fresh at
        // promote time on each leg (filtered to exclude known-volatile
        // vendor bookkeeping on MS/Google — see the respective promote
        // stages). Makes an X-prop/extras-only edit differ-visible:
        // CanonJsonDiffer only ever compares catalogued keys, and
        // providerExtras itself is deliberately never catalogued (see
        // FINDINGS.md O74).
        { PropertyId{QStringLiteral("providerExtrasDigest")}, {PropertyKind::String, QStringLiteral("Provider Extras Digest")} },
        // Alarms and extra data
        { PropertyId{QStringLiteral("alarms")},           {PropertyKind::Json,       QStringLiteral("Alarms")} },
        { PropertyId{QStringLiteral("location")},         {PropertyKind::String,     QStringLiteral("Location")} },
        { PropertyId{QStringLiteral("geo")},              {PropertyKind::Json,       QStringLiteral("Geo")} },
        // Hierarchy / relations (carry-verbatim, invariant P4):
        // relatedTo = VTODO RELATED-TO hierarchy (array of {uid, reltype})
        // parentUid = Google Tasks single-level parent (vendor-only)
        // checklistItems = Microsoft To-Do checklist (vendor-only)
        { PropertyId{QStringLiteral("sortOrder")},        {PropertyKind::String,     QStringLiteral("Sort Order")} },
        { PropertyId{QStringLiteral("relatedTo")},        {PropertyKind::Json,       QStringLiteral("Related To")} },
        { PropertyId{QStringLiteral("parentUid")},        {PropertyKind::String,     QStringLiteral("Parent UID")} },
        { PropertyId{QStringLiteral("checklistItems")},   {PropertyKind::Json,       QStringLiteral("Checklist Items")} },
        { PropertyId{QStringLiteral("linkedResources")},  {PropertyKind::Json,       QStringLiteral("Linked Resources")} },
    };
    return table;
}

/// Vendor-only keys (Google Tasks / MS To-Do) with no VTODO representation
/// via the shared emitter. Verified 2026-09-02 (IP.3 receipt): grep-
/// confirmed zero references in vtodocanonfields.cpp's promote path
/// (todoFieldsToCanon); each is written by googletaskcanonstages.cpp /
/// mstodotaskcanonstages.cpp instead, and only ever *consumed* (never
/// produced) by canonObjectToVtodoBytes on the vtodo/CalDAV demote leg.
QList<PropertyId> todoVendorOnlyIds()
{
    return {
        PropertyId{QStringLiteral("sortOrder")},
        PropertyId{QStringLiteral("parentUid")},
        PropertyId{QStringLiteral("checklistItems")},
        PropertyId{QStringLiteral("linkedResources")},
    };
}

}  // namespace

Kalburator::Shape::PropertyCatalogue makeTodoCanonCatalogue()
{
    PropertyCatalogue cat;

    // Required field — envelope-owned, not a contributed id.
    cat.addProperty({ PropertyId{QStringLiteral("uid")}, PropertyKind::String, QStringLiteral("UID"), false });

    // IP.3: vtodoCanonContributedIds() plus this domain's vendor-only
    // ids is the ONLY place the catalogue's id set is decided; adding a
    // key to the shared vtodo emitter's contributed-id list reaches this
    // catalogue automatically, no second edit here.
    QList<PropertyId> ids;
    QSet<PropertyId> seen;
    auto addAll = [&ids, &seen](const QList<PropertyId>& src) {
        for (const auto& id : src) {
            if (seen.contains(id))
                continue;
            seen.insert(id);
            ids.append(id);
        }
    };
    addAll(vtodoCanonContributedIds());
    addAll(todoVendorOnlyIds());

    const auto& meta = todoPropertyMetadata();
    for (const auto& id : ids) {
        const auto it = meta.constFind(id);
        if (it != meta.constEnd()) {
            cat.addProperty({ id, it->kind, it->displayName, true });
        } else {
            // A contributed id with no metadata entry yet: still catalogue
            // it — never silently drop it — with a safe generic default.
            cat.addProperty({ id, PropertyKind::Json, id.toString(), true });
        }
    }

    return cat;
}

QList<Kalburator::Shape::PropertyId> todoCanonPropertyIds()
{
    QList<PropertyId> ids;
    const PropertyCatalogue cat = makeTodoCanonCatalogue();
    for (const auto& d : cat.properties())
        ids.append(d.id);
    return ids;
}

}  // namespace Kalburator::Todo
