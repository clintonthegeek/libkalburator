#include "calendarcanonproperties.h"

#include "eventcanonfields.h"
#include "journalcanonfields.h"
#include "vtodocanonfields.h"

#include <QHash>
#include <QSet>

using namespace Kalburator::Shape;

namespace Kalburator::Calendar {

namespace {

/// Kind + display-name metadata for every property id this catalogue may
/// carry. IP.3: this table is the catalogue's single declaration of what a
/// property id *looks like*; WHICH ids actually appear in the catalogue is
/// decided by makeCalendarCanonCatalogue() below, from the union of the
/// three emitter modules' contributed-id lists plus calendarVendorOnlyIds()
/// — never by hand-listing ids redundantly here. A key contributed by more
/// than one emitter (e.g. VTODO riding both the `calendar` and `todo`
/// domains) is declared exactly once here, so the union can never produce
/// two different PropertyKinds for the same id.
struct PropertyMeta {
    PropertyKind kind;
    QString displayName;
};

const QHash<PropertyId, PropertyMeta>& calendarPropertyMetadata()
{
    static const QHash<PropertyId, PropertyMeta> table = {
        // Sequencing
        { PropertyId{QStringLiteral("sequence")},          {PropertyKind::Integer,    QStringLiteral("Sequence")} },
        // Timestamps
        { PropertyId{QStringLiteral("created")},           {PropertyKind::DateTime,   QStringLiteral("Created")} },
        { PropertyId{QStringLiteral("lastModified")},      {PropertyKind::DateTime,   QStringLiteral("Last Modified")} },
        // Core text fields
        { PropertyId{QStringLiteral("summary")},           {PropertyKind::String,     QStringLiteral("Summary")} },
        { PropertyId{QStringLiteral("description")},       {PropertyKind::String,     QStringLiteral("Description")} },
        { PropertyId{QStringLiteral("descriptionHtml")},   {PropertyKind::String,     QStringLiteral("Description (HTML)")} },
        { PropertyId{QStringLiteral("location")},          {PropertyKind::String,     QStringLiteral("Location")} },
        // Structured location (Google/MS multi-location) — vendor-only
        { PropertyId{QStringLiteral("locations")},         {PropertyKind::Json,       QStringLiteral("Locations")} },
        // Status / classification
        { PropertyId{QStringLiteral("status")},            {PropertyKind::String,     QStringLiteral("Status")} },
        { PropertyId{QStringLiteral("classification")},    {PropertyKind::String,     QStringLiteral("Classification")} },
        { PropertyId{QStringLiteral("timeTransparency")},  {PropertyKind::String,     QStringLiteral("Time Transparency")} },
        { PropertyId{QStringLiteral("freeBusyStatus")},    {PropertyKind::String,     QStringLiteral("Free/Busy Status")} },
        // Time
        { PropertyId{QStringLiteral("start")},             {PropertyKind::Json,       QStringLiteral("Start")} },
        { PropertyId{QStringLiteral("end")},                {PropertyKind::Json,       QStringLiteral("End")} },
        { PropertyId{QStringLiteral("allDay")},             {PropertyKind::Boolean,    QStringLiteral("All Day")} },
        // Recurrence (verbatim RFC5545 lines — invariant 3)
        { PropertyId{QStringLiteral("recurrence")},         {PropertyKind::StringList, QStringLiteral("Recurrence")} },
        { PropertyId{QStringLiteral("recurrenceId")},       {PropertyKind::Json,       QStringLiteral("Recurrence ID")} },
        { PropertyId{QStringLiteral("recurrenceRange")},    {PropertyKind::String,     QStringLiteral("Recurrence Range")} },
        // Appearance
        { PropertyId{QStringLiteral("color")},              {PropertyKind::String,     QStringLiteral("Color")} },
        { PropertyId{QStringLiteral("categories")},         {PropertyKind::StringList, QStringLiteral("Categories")} },
        { PropertyId{QStringLiteral("url")},                {PropertyKind::String,     QStringLiteral("URL")} },
        // Participants
        { PropertyId{QStringLiteral("organizer")},          {PropertyKind::Json,       QStringLiteral("Organizer")} },
        { PropertyId{QStringLiteral("attendees")},          {PropertyKind::Json,       QStringLiteral("Attendees")} },
        // Scheduling — vendor-only
        { PropertyId{QStringLiteral("responseRequested")},  {PropertyKind::Boolean,    QStringLiteral("Response Requested")} },
        { PropertyId{QStringLiteral("priority")},           {PropertyKind::Integer,    QStringLiteral("Priority")} },
        // Rich content
        { PropertyId{QStringLiteral("alarms")},             {PropertyKind::Json,       QStringLiteral("Alarms")} },
        { PropertyId{QStringLiteral("onlineMeeting")},      {PropertyKind::Json,       QStringLiteral("Online Meeting")} },
        { PropertyId{QStringLiteral("attachments")},        {PropertyKind::Json,       QStringLiteral("Attachments")} },
        // Event type and typed properties (Google/MS) — vendor-only
        { PropertyId{QStringLiteral("eventType")},          {PropertyKind::String,     QStringLiteral("Event Type")} },
        { PropertyId{QStringLiteral("typedProperties")},    {PropertyKind::Json,       QStringLiteral("Typed Properties")} },
        // Guest permissions (Google) — vendor-only
        { PropertyId{QStringLiteral("guestsCanModify")},          {PropertyKind::Boolean, QStringLiteral("Guests Can Modify")} },
        { PropertyId{QStringLiteral("guestsCanInviteOthers")},    {PropertyKind::Boolean, QStringLiteral("Guests Can Invite Others")} },
        { PropertyId{QStringLiteral("guestsCanSeeOtherGuests")},  {PropertyKind::Boolean, QStringLiteral("Guests Can See Other Guests")} },
        // MS Graph flags — vendor-only
        { PropertyId{QStringLiteral("allowNewTimeProposals")}, {PropertyKind::Boolean, QStringLiteral("Allow New Time Proposals")} },
        { PropertyId{QStringLiteral("hideAttendees")},         {PropertyKind::Boolean, QStringLiteral("Hide Attendees")} },
        { PropertyId{QStringLiteral("locked")},                {PropertyKind::Boolean, QStringLiteral("Locked")} },
        { PropertyId{QStringLiteral("privateCopy")},           {PropertyKind::Boolean, QStringLiteral("Private Copy")} },
        // Union across iCalendar component kinds (VTODO / VJOURNAL) — the
        // {calendar,canon} shape carries any of VEVENT/VTODO/VJOURNAL
        // (kind-tagged in the envelope). These fields are absent on events
        // but must be catalogued so CanonJsonDiffer detects changes to
        // todo/journal records.
        { PropertyId{QStringLiteral("due")},              {PropertyKind::Json,     QStringLiteral("Due")} },
        { PropertyId{QStringLiteral("completed")},        {PropertyKind::DateTime, QStringLiteral("Completed")} },
        { PropertyId{QStringLiteral("percentComplete")},  {PropertyKind::Integer,  QStringLiteral("Percent Complete")} },
        { PropertyId{QStringLiteral("relatedTo")},        {PropertyKind::Json,     QStringLiteral("Related To")} },
        { PropertyId{QStringLiteral("geo")},              {PropertyKind::Json,     QStringLiteral("Geo")} },
        // IP.2 / O78 — the {calendar,canon} VTODO leg runs the SAME
        // emitter as {todo,canon} (icalcanonstages.cpp calls
        // Todo::todoFieldsToCanon), so these vtodo-parity keys arrive here
        // too. Declarations must match todocanonproperties.cpp exactly
        // (kind + display name).
        { PropertyId{QStringLiteral("seriesSplitOf")},        {PropertyKind::String, QStringLiteral("Series Split Of")} },
        { PropertyId{QStringLiteral("completionAnchor")},     {PropertyKind::Json,   QStringLiteral("Completion Anchor")} },
        { PropertyId{QStringLiteral("providerExtrasDigest")}, {PropertyKind::String, QStringLiteral("Provider Extras Digest")} },
        // IP.6 commit 2 / O91 — RFC 5545 COMMENT/CONTACT/RESOURCES, newly
        // contributed by eventCanonContributedIds() (VEVENT+VJOURNAL for
        // comments/contacts; VEVENT only for resources — RFC 5545 §3.6.3's
        // jourprop grammar excludes RESOURCES on VJOURNAL) and
        // vtodoCanonContributedIds() (all three). Declarations must match
        // todocanonproperties.cpp exactly (kind + display name).
        // `requestStatus` is deliberately NOT catalogued here — no emitter
        // will ever produce it (KCalendarCore has no accessor at all), so
        // catalogueing it would misrepresent it as emitter-producible; it
        // stays an uncatalogued PropertyId in the loss profiles that
        // declare it Dropped, matching IP.9's precedent.
        { PropertyId{QStringLiteral("comments")},  {PropertyKind::StringList, QStringLiteral("Comments")} },
        { PropertyId{QStringLiteral("contacts")},  {PropertyKind::StringList, QStringLiteral("Contacts")} },
        { PropertyId{QStringLiteral("resources")}, {PropertyKind::StringList, QStringLiteral("Resources")} },
    };
    return table;
}

/// Event-only vendor keys — Google Calendar / MS Graph event fields that no
/// {event,todo,journal} emitter produces. Verified 2026-09-02 by grepping
/// every top-level `obj.insert(...)` in eventcanonfields.cpp,
/// journalcanonfields.cpp and vtodocanonfields.cpp for each of these ids
/// (see the IP.3 return receipt for the full sweep). NOT
/// `descriptionHtml`/`freeBusyStatus`: an earlier draft of the plan listed
/// both here, but eventcanonfields.cpp (and, for descriptionHtml,
/// vtodocanonfields.cpp too) emit them directly from X-ALT-DESC /
/// X-MICROSOFT-CDO-BUSYSTATUS custom properties — real emitter output, not
/// vendor-JSON-only — so they belong in the contributed-id lists instead
/// and are picked up from there.
QList<PropertyId> calendarVendorOnlyIds()
{
    return {
        PropertyId{QStringLiteral("locations")},
        PropertyId{QStringLiteral("onlineMeeting")},
        PropertyId{QStringLiteral("eventType")},
        PropertyId{QStringLiteral("typedProperties")},
        PropertyId{QStringLiteral("guestsCanModify")},
        PropertyId{QStringLiteral("guestsCanInviteOthers")},
        PropertyId{QStringLiteral("guestsCanSeeOtherGuests")},
        PropertyId{QStringLiteral("allowNewTimeProposals")},
        PropertyId{QStringLiteral("hideAttendees")},
        PropertyId{QStringLiteral("locked")},
        PropertyId{QStringLiteral("privateCopy")},
        PropertyId{QStringLiteral("responseRequested")},
    };
}

}  // namespace

Kalburator::Shape::PropertyCatalogue makeCalendarCanonCatalogue()
{
    PropertyCatalogue cat;

    // Required identity field — envelope-owned, not a contributed id.
    cat.addProperty({ PropertyId{QStringLiteral("uid")}, PropertyKind::String, QStringLiteral("UID"), false });

    // {calendar,canon} carries VEVENT, VTODO (shared emitter —
    // icalcanonstages.cpp calls Todo::todoFieldsToCanon directly) and
    // VJOURNAL, kind-tagged in the envelope — union all three contributors,
    // then the event-only vendor keys no emitter produces. IP.3: this
    // union is the ONLY place the catalogue's id set is decided; adding a
    // key to any of the three emitters' contributed-id lists reaches this
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
    addAll(eventCanonContributedIds());
    addAll(Kalburator::Todo::vtodoCanonContributedIds());
    addAll(journalCanonContributedIds());
    addAll(calendarVendorOnlyIds());

    const auto& meta = calendarPropertyMetadata();
    for (const auto& id : ids) {
        const auto it = meta.constFind(id);
        if (it != meta.constEnd()) {
            cat.addProperty({ id, it->kind, it->displayName, true });
        } else {
            // A contributed id with no metadata entry yet: still catalogue
            // it — never silently drop it, that is exactly the O78/O84
            // class this item exists to close — with a safe generic
            // default. Add a proper entry to calendarPropertyMetadata()
            // above the next time this id needs a non-default kind or
            // display name.
            cat.addProperty({ id, PropertyKind::Json, id.toString(), true });
        }
    }

    return cat;
}

QList<Kalburator::Shape::PropertyId> calendarCanonPropertyIds()
{
    QList<PropertyId> ids;
    const PropertyCatalogue cat = makeCalendarCanonCatalogue();
    for (const auto& d : cat.properties())
        ids.append(d.id);
    return ids;
}

}  // namespace Kalburator::Calendar
