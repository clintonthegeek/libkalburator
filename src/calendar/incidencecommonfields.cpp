#include "incidencecommonfields.h"

#include "icaltimestamp.h"

#include <KCalendarCore/Attendee>

#include <QJsonArray>

namespace {

// ---- attendee enum <-> string helpers ------------------------------------
// Relocated verbatim from eventcanonfields.cpp (Plan 6 Task 3) — VTODO's
// promoted/demoted attendee row shape is identical, so these move here
// rather than being duplicated a second time.

QString roleToString(KCalendarCore::Attendee::Role r)
{
    switch (r) {
    case KCalendarCore::Attendee::Chair:           return QStringLiteral("chair");
    case KCalendarCore::Attendee::ReqParticipant:  return QStringLiteral("required");
    case KCalendarCore::Attendee::OptParticipant:  return QStringLiteral("optional");
    case KCalendarCore::Attendee::NonParticipant:  return QStringLiteral("nonParticipant");
    default:                                       return QStringLiteral("required");
    }
}

KCalendarCore::Attendee::Role roleFromString(const QString &s)
{
    if (s == QStringLiteral("chair"))          return KCalendarCore::Attendee::Chair;
    if (s == QStringLiteral("optional"))       return KCalendarCore::Attendee::OptParticipant;
    if (s == QStringLiteral("nonParticipant")) return KCalendarCore::Attendee::NonParticipant;
    return KCalendarCore::Attendee::ReqParticipant;
}

QString partStatToString(KCalendarCore::Attendee::PartStat ps)
{
    switch (ps) {
    case KCalendarCore::Attendee::Accepted:      return QStringLiteral("accepted");
    case KCalendarCore::Attendee::Declined:      return QStringLiteral("declined");
    case KCalendarCore::Attendee::Tentative:     return QStringLiteral("tentative");
    case KCalendarCore::Attendee::Delegated:     return QStringLiteral("delegated");
    case KCalendarCore::Attendee::NeedsAction:   return QStringLiteral("needsAction");
    default:                                     return QStringLiteral("needsAction");
    }
}

KCalendarCore::Attendee::PartStat partStatFromString(const QString &s)
{
    if (s == QStringLiteral("accepted"))    return KCalendarCore::Attendee::Accepted;
    if (s == QStringLiteral("declined"))    return KCalendarCore::Attendee::Declined;
    if (s == QStringLiteral("tentative"))   return KCalendarCore::Attendee::Tentative;
    if (s == QStringLiteral("delegated"))   return KCalendarCore::Attendee::Delegated;
    return KCalendarCore::Attendee::NeedsAction;
}

}  // namespace

namespace Kalburator::Calendar {

// ---------------------------------------------------------------------
// created / lastModified
// ---------------------------------------------------------------------

void promoteTimestamps(QJsonObject& obj, const QByteArray& originalBytes)
{
    const QDateTime created = extractICalPropertyLiteral(originalBytes, QStringLiteral("CREATED"));
    const QDateTime lastMod = extractICalPropertyLiteral(originalBytes, QStringLiteral("LAST-MODIFIED"));
    if (created.isValid())
        obj.insert(QStringLiteral("created"), created.toUTC().toString(Qt::ISODate));
    if (lastMod.isValid())
        obj.insert(QStringLiteral("lastModified"), lastMod.toUTC().toString(Qt::ISODate));
}

TimestampPresence demoteTimestamps(const QJsonObject& obj,
                                    const KCalendarCore::Incidence::Ptr& inc)
{
    TimestampPresence presence;
    {
        const QString created = obj.value(QStringLiteral("created")).toString();
        if (!created.isEmpty()) {
            const QDateTime dt = QDateTime::fromString(created, Qt::ISODate);
            if (dt.isValid()) {
                inc->setCreated(dt);
                presence.hadCreated = true;
            }
        }
    }
    {
        const QString lastMod = obj.value(QStringLiteral("lastModified")).toString();
        if (!lastMod.isEmpty()) {
            const QDateTime dt = QDateTime::fromString(lastMod, Qt::ISODate);
            if (dt.isValid()) {
                inc->setLastModified(dt);
                presence.hadLastModified = true;
            }
        }
    }
    return presence;
}

QByteArray stripInjectedTimestamps(QByteArray icalBytes, const TimestampPresence& presence)
{
    if (!presence.hadCreated)
        icalBytes = stripICalPropertyLine(icalBytes, QStringLiteral("CREATED"));
    if (!presence.hadLastModified)
        icalBytes = stripICalPropertyLine(icalBytes, QStringLiteral("LAST-MODIFIED"));
    return icalBytes;
}

// ---------------------------------------------------------------------
// summary / description
// ---------------------------------------------------------------------

void promoteSummaryDescription(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QString summary = inc->summary();
    const QString description = inc->description();
    if (!summary.isEmpty())
        obj.insert(QStringLiteral("summary"), summary);
    if (!description.isEmpty())
        obj.insert(QStringLiteral("description"), description);
}

void demoteSummaryDescription(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QString summary = obj.value(QStringLiteral("summary")).toString();
    if (!summary.isEmpty())
        inc->setSummary(summary);
    const QString description = obj.value(QStringLiteral("description")).toString();
    if (!description.isEmpty())
        inc->setDescription(description);
}

// ---------------------------------------------------------------------
// categories
// ---------------------------------------------------------------------

void promoteCategories(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QStringList cats = inc->categories();
    if (!cats.isEmpty()) {
        QJsonArray arr;
        for (const auto& c : cats)
            arr.append(c);
        obj.insert(QStringLiteral("categories"), arr);
    }
}

void demoteCategories(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
    if (!cats.isEmpty()) {
        QStringList catList;
        for (const auto& c : cats)
            catList << c.toString();
        inc->setCategories(catList);
    }
}

// ---------------------------------------------------------------------
// Generic X- custom-property passthrough
// ---------------------------------------------------------------------

QJsonObject promoteCustomPropertyPassthrough(const KCalendarCore::Incidence::Ptr& inc,
                                              const QSet<QByteArray>& skipKeys)
{
    QJsonObject sub;
    const auto customProps = inc->customProperties();
    for (auto it = customProps.constBegin(); it != customProps.constEnd(); ++it) {
        if (skipKeys.contains(it.key()))
            continue;
        sub.insert(QString::fromLatin1(it.key()), it.value());
    }
    return sub;
}

void demoteCustomPropertyPassthrough(const QJsonObject& subObj,
                                      const KCalendarCore::Incidence::Ptr& inc)
{
    for (auto it = subObj.constBegin(); it != subObj.constEnd(); ++it)
        inc->setNonKDECustomProperty(it.key().toLatin1(), it.value().toString());
}

// ---------------------------------------------------------------------
// sequence
// ---------------------------------------------------------------------

void promoteSequence(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const int seq = inc->revision();
    if (seq > 0)
        obj.insert(QStringLiteral("sequence"), seq);
}

void demoteSequence(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonValue seq = obj.value(QStringLiteral("sequence"));
    if (!seq.isUndefined())
        inc->setRevision(seq.toInt());
}

// ---------------------------------------------------------------------
// classification
// ---------------------------------------------------------------------

void promoteClassification(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const auto cls = inc->secrecy();
    QString clsStr;
    switch (cls) {
    case KCalendarCore::Incidence::SecrecyPublic:       clsStr = QStringLiteral("public");       break;
    case KCalendarCore::Incidence::SecrecyPrivate:      clsStr = QStringLiteral("private");      break;
    case KCalendarCore::Incidence::SecrecyConfidential: clsStr = QStringLiteral("confidential"); break;
    default: break;
    }
    if (!clsStr.isEmpty())
        obj.insert(QStringLiteral("classification"), clsStr);
}

void demoteClassification(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QString cls = obj.value(QStringLiteral("classification")).toString();
    KCalendarCore::Incidence::Secrecy secrecy = KCalendarCore::Incidence::SecrecyPublic;
    if (cls == QStringLiteral("private")) {
        secrecy = KCalendarCore::Incidence::SecrecyPrivate;
    } else if (cls == QStringLiteral("confidential")) {
        secrecy = KCalendarCore::Incidence::SecrecyConfidential;
    } else if (cls == QStringLiteral("personal")) {
        // Degraded: MS "personal" has no iCal CLASS; map to PRIVATE but keep
        // the original verbatim (invariant 4) so it is recoverable — emit as
        // an X-property the forward stage round-trips into providerExtras.
        secrecy = KCalendarCore::Incidence::SecrecyPrivate;
        inc->setNonKDECustomProperty("X-CANON-CLASSIFICATION", cls);
    }
    inc->setSecrecy(secrecy);
}

// ---------------------------------------------------------------------
// color / url
// ---------------------------------------------------------------------

void promoteColor(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QString color = inc->color();
    if (!color.isEmpty())
        obj.insert(QStringLiteral("color"), color);
}

void demoteColor(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QString color = obj.value(QStringLiteral("color")).toString();
    if (!color.isEmpty())
        inc->setColor(color);
}

void promoteUrl(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QUrl url = inc->url();
    if (url.isValid())
        obj.insert(QStringLiteral("url"), url.toString());
}

void demoteUrl(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QString url = obj.value(QStringLiteral("url")).toString();
    if (!url.isEmpty())
        inc->setUrl(QUrl(url));
}

// ---------------------------------------------------------------------
// organizer / attendees / attachments
// ---------------------------------------------------------------------

void promoteOrganizer(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const auto org = inc->organizer();
    if (!org.email().isEmpty() || !org.name().isEmpty()) {
        QJsonObject orgObj;
        if (!org.email().isEmpty()) orgObj.insert(QStringLiteral("email"), org.email());
        if (!org.name().isEmpty())  orgObj.insert(QStringLiteral("name"),  org.name());
        obj.insert(QStringLiteral("organizer"), orgObj);
    }
}

void demoteOrganizer(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonObject orgObj = obj.value(QStringLiteral("organizer")).toObject();
    if (!orgObj.isEmpty()) {
        const QString email = orgObj.value(QStringLiteral("email")).toString();
        const QString name  = orgObj.value(QStringLiteral("name")).toString();
        if (!email.isEmpty())
            inc->setOrganizer(KCalendarCore::Person(name, email));
    }
}

void promoteAttendees(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const auto attendees = inc->attendees();
    if (!attendees.isEmpty()) {
        QJsonArray arr;
        for (const auto& a : attendees) {
            if (a.email().isEmpty())
                continue;
            QJsonObject entry;
            entry.insert(QStringLiteral("email"), a.email());
            if (!a.name().isEmpty())
                entry.insert(QStringLiteral("name"), a.name());
            entry.insert(QStringLiteral("role"), roleToString(a.role()));
            entry.insert(QStringLiteral("partstat"), partStatToString(a.status()));
            entry.insert(QStringLiteral("rsvp"), a.RSVP());
            arr.append(entry);
        }
        if (!arr.isEmpty())
            obj.insert(QStringLiteral("attendees"), arr);
    }
}

void demoteAttendees(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonArray attendees = obj.value(QStringLiteral("attendees")).toArray();
    for (const auto& av : attendees) {
        const QJsonObject a = av.toObject();
        const QString email = a.value(QStringLiteral("email")).toString();
        if (email.isEmpty())
            continue;
        const QString name  = a.value(QStringLiteral("name")).toString();
        const auto role     = roleFromString(a.value(QStringLiteral("role")).toString());
        const auto partstat = partStatFromString(a.value(QStringLiteral("partstat")).toString());
        const bool rsvp      = a.value(QStringLiteral("rsvp")).toBool();
        KCalendarCore::Attendee att(name, email, rsvp, partstat, role);
        inc->addAttendee(att);
    }
}

void promoteAttachments(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const auto attachments = inc->attachments();
    if (!attachments.isEmpty()) {
        QJsonArray arr;
        for (const auto& att : attachments) {
            QJsonObject entry;
            if (att.isUri())
                entry.insert(QStringLiteral("url"), att.uri());
            if (!att.mimeType().isEmpty())
                entry.insert(QStringLiteral("mimeType"), att.mimeType());
            if (!entry.isEmpty())
                arr.append(entry);
        }
        if (!arr.isEmpty())
            obj.insert(QStringLiteral("attachments"), arr);
    }
}

void demoteAttachments(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonArray attachments = obj.value(QStringLiteral("attachments")).toArray();
    for (const auto& av : attachments) {
        const QJsonObject a = av.toObject();
        const QString url = a.value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            KCalendarCore::Attachment att;
            att.setUri(url);
            const QString mime = a.value(QStringLiteral("mimeType")).toString();
            if (!mime.isEmpty())
                att.setMimeType(mime);
            inc->addAttachment(att);
        }
    }
}

// ---------------------------------------------------------------------
// relatedTo
// ---------------------------------------------------------------------

void promoteRelatedTo(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    // KCalendarCore only exposes RelTypeParent (single RELATED-TO per type).
    const QString parentUid = inc->relatedTo(KCalendarCore::Incidence::RelTypeParent);
    if (!parentUid.isEmpty()) {
        QJsonArray arr;
        QJsonObject rel;
        rel.insert(QStringLiteral("uid"), parentUid);
        rel.insert(QStringLiteral("reltype"), QStringLiteral("PARENT"));
        arr.append(rel);
        obj.insert(QStringLiteral("relatedTo"), arr);
    }
}

void demoteRelatedTo(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonArray rels = obj.value(QStringLiteral("relatedTo")).toArray();
    for (const auto& rv : rels) {
        const QJsonObject r = rv.toObject();
        const QString relUid  = r.value(QStringLiteral("uid")).toString();
        const QString reltype = r.value(QStringLiteral("reltype")).toString();
        if (relUid.isEmpty())
            continue;
        // KCalendarCore only supports RelTypeParent; other reltypes ignored.
        if (reltype.isEmpty() || reltype == QStringLiteral("PARENT"))
            inc->setRelatedTo(relUid, KCalendarCore::Incidence::RelTypeParent);
    }
}

// ---------------------------------------------------------------------
// comments / contacts (O91)
// ---------------------------------------------------------------------

void promoteComments(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QStringList comments = inc->comments();
    if (!comments.isEmpty()) {
        QJsonArray arr;
        for (const auto& c : comments)
            arr.append(c);
        obj.insert(QStringLiteral("comments"), arr);
    }
}

void demoteComments(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonArray arr = obj.value(QStringLiteral("comments")).toArray();
    for (const auto& v : arr) {
        const QString c = v.toString();
        if (!c.isEmpty())
            inc->addComment(c);
    }
}

void promoteContacts(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QStringList contacts = inc->contacts();
    if (!contacts.isEmpty()) {
        QJsonArray arr;
        for (const auto& c : contacts)
            arr.append(c);
        obj.insert(QStringLiteral("contacts"), arr);
    }
}

void demoteContacts(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonArray arr = obj.value(QStringLiteral("contacts")).toArray();
    for (const auto& v : arr) {
        const QString c = v.toString();
        if (!c.isEmpty())
            inc->addContact(c);
    }
}

// ---------------------------------------------------------------------
// resources (O91) — VEVENT + VTODO only
// ---------------------------------------------------------------------

void promoteResources(QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QStringList resources = inc->resources();
    if (!resources.isEmpty()) {
        QJsonArray arr;
        for (const auto& r : resources)
            arr.append(r);
        obj.insert(QStringLiteral("resources"), arr);
    }
}

void demoteResources(const QJsonObject& obj, const KCalendarCore::Incidence::Ptr& inc)
{
    const QJsonArray arr = obj.value(QStringLiteral("resources")).toArray();
    if (!arr.isEmpty()) {
        QStringList resList;
        for (const auto& v : arr)
            resList << v.toString();
        inc->setResources(resList);
    }
}

}  // namespace Kalburator::Calendar
