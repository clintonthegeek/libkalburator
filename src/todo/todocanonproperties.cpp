#include "todocanonproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Todo {

Kalburator::Shape::PropertyCatalogue makeTodoCanonCatalogue()
{
    PropertyCatalogue cat;

    // Required field
    cat.addProperty({ PropertyId{"uid"},              PropertyKind::String,     QStringLiteral("UID"),              false });

    // Timestamps
    cat.addProperty({ PropertyId{"created"},          PropertyKind::DateTime,   QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastModified"},     PropertyKind::DateTime,   QStringLiteral("Last Modified") });

    // Core text fields
    cat.addProperty({ PropertyId{"summary"},          PropertyKind::String,     QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"description"},      PropertyKind::String,     QStringLiteral("Description") });
    cat.addProperty({ PropertyId{"descriptionHtml"},  PropertyKind::String,     QStringLiteral("Description (HTML)") });

    // Status fields
    cat.addProperty({ PropertyId{"status"},           PropertyKind::String,     QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"percentComplete"},  PropertyKind::Integer,    QStringLiteral("Percent Complete") });
    cat.addProperty({ PropertyId{"priority"},         PropertyKind::Integer,    QStringLiteral("Priority") });

    // Classification
    cat.addProperty({ PropertyId{"categories"},       PropertyKind::StringList, QStringLiteral("Categories") });

    // Time fields (Json to support tz + floating + precision)
    cat.addProperty({ PropertyId{"start"},            PropertyKind::Json,       QStringLiteral("Start") });
    cat.addProperty({ PropertyId{"due"},              PropertyKind::Json,       QStringLiteral("Due") });
    cat.addProperty({ PropertyId{"completed"},        PropertyKind::DateTime,   QStringLiteral("Completed") });

    // Recurrence (verbatim RFC5545 lines — invariant 3)
    cat.addProperty({ PropertyId{"recurrence"},       PropertyKind::StringList, QStringLiteral("Recurrence") });
    // Detached-exception identity (mirrors the event canon catalogue)
    cat.addProperty({ PropertyId{"recurrenceId"},     PropertyKind::Json,       QStringLiteral("Recurrence ID") });
    cat.addProperty({ PropertyId{"recurrenceRange"},  PropertyKind::String,     QStringLiteral("Recurrence Range") });
    // Series-split re-association (W3): links a series-split new master
    // back to its old master's uid (String = old master's uid). Read/write
    // on the vtodo/CalDAV leg via X-CANON-SERIES-SPLIT-OF; auto-carries on
    // MS To-Do (Reversible); Dropped on Google Tasks (no extension point).
    // See docs/campaign/vtodo-parity/2026-08-27-w3-series-split-contract.md.
    cat.addProperty({ PropertyId{"seriesSplitOf"},    PropertyKind::String,     QStringLiteral("Series Split Of") });
    // Completion-anchored recurrence (W4): derived standard form of an
    // org-mode completion-anchor repeater (".+1w" Restart / "++2d"
    // CatchUp) — {type: "catchUp"|"restart", interval, unit}. Catalogued
    // so the differ sees an anchor advance as an ordinary field change
    // (never a conflict); the verbatim org string rides
    // providerExtras["x-vtodo"] via the generic custom-prop channel.
    cat.addProperty({ PropertyId{"completionAnchor"}, PropertyKind::Json,       QStringLiteral("Completion Anchor") });

    // Alarms and extra data
    cat.addProperty({ PropertyId{"alarms"},           PropertyKind::Json,       QStringLiteral("Alarms") });
    cat.addProperty({ PropertyId{"location"},         PropertyKind::String,     QStringLiteral("Location") });
    cat.addProperty({ PropertyId{"geo"},              PropertyKind::Json,       QStringLiteral("Geo") });

    // Hierarchy / relations (carry-verbatim, invariant P4):
    // relatedTo = VTODO RELATED-TO hierarchy (array of {uid, reltype})
    // parentUid = Google Tasks single-level parent
    // checklistItems = Microsoft To-Do checklist (Json array)
    cat.addProperty({ PropertyId{"sortOrder"},        PropertyKind::String,     QStringLiteral("Sort Order") });
    cat.addProperty({ PropertyId{"relatedTo"},        PropertyKind::Json,       QStringLiteral("Related To") });
    cat.addProperty({ PropertyId{"parentUid"},        PropertyKind::String,     QStringLiteral("Parent UID") });
    cat.addProperty({ PropertyId{"checklistItems"},   PropertyKind::Json,       QStringLiteral("Checklist Items") });
    cat.addProperty({ PropertyId{"linkedResources"},  PropertyKind::Json,       QStringLiteral("Linked Resources") });

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
