#include "journalcanonfields.h"

#include "canonenvelope.h"

#include <KCalendarCore/ICalFormat>

#include <QJsonArray>
#include <QTimeZone>

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;

namespace {

QString journalStatusToString(KCalendarCore::Incidence::Status s)
{
    switch (s) {
    case KCalendarCore::Incidence::StatusDraft:      return QStringLiteral("draft");
    case KCalendarCore::Incidence::StatusFinal:      return QStringLiteral("final");
    case KCalendarCore::Incidence::StatusCanceled:   return QStringLiteral("cancelled");
    default:                                         return {};
    }
}

KCalendarCore::Incidence::Status journalStatusFromString(const QString &s)
{
    if (s == QStringLiteral("draft"))     return KCalendarCore::Incidence::StatusDraft;
    if (s == QStringLiteral("final"))     return KCalendarCore::Incidence::StatusFinal;
    if (s == QStringLiteral("cancelled")) return KCalendarCore::Incidence::StatusCanceled;
    return KCalendarCore::Incidence::StatusNone;
}

QString classToString(KCalendarCore::Incidence::Secrecy cls)
{
    switch (cls) {
    case KCalendarCore::Incidence::SecrecyPrivate:      return QStringLiteral("private");
    case KCalendarCore::Incidence::SecrecyConfidential: return QStringLiteral("confidential");
    default:                                            return QStringLiteral("public");
    }
}

} // namespace

namespace Kalburator::Calendar {

QJsonObject journalFieldsToCanon(const KCalendarCore::Journal::Ptr& journal,
                                 const QByteArray& /*originalBytes*/)
{
    QJsonObject obj;
    if (journal->created().isValid())
        obj.insert(QStringLiteral("created"),
                   journal->created().toUTC().toString(Qt::ISODate));
    if (journal->lastModified().isValid())
        obj.insert(QStringLiteral("lastModified"),
                   journal->lastModified().toUTC().toString(Qt::ISODate));
    if (journal->revision() > 0)
        obj.insert(QStringLiteral("sequence"), journal->revision());
    if (!journal->summary().isEmpty())
        obj.insert(QStringLiteral("summary"), journal->summary());
    if (!journal->description().isEmpty())
        obj.insert(QStringLiteral("description"), journal->description());
    if (journal->dtStart().isValid()) {
        QJsonObject start;
        const bool allDay = journal->allDay();
        if (allDay) {
            start.insert(QStringLiteral("date"),
                         journal->dtStart().date().toString(Qt::ISODate));
            start.insert(QStringLiteral("allDay"), true);
            obj.insert(QStringLiteral("allDay"), true);
        } else {
            start.insert(QStringLiteral("dateTime"),
                         journal->dtStart().toUTC().toString(Qt::ISODate));
            start.insert(QStringLiteral("floating"),
                         journal->dtStart().timeSpec() == Qt::LocalTime);
        }
        obj.insert(QStringLiteral("start"), start);
    }
    {
        const QString st = journalStatusToString(journal->status());
        if (!st.isEmpty())
            obj.insert(QStringLiteral("status"), st);
    }
    obj.insert(QStringLiteral("classification"), classToString(journal->secrecy()));
    if (!journal->color().isEmpty())
        obj.insert(QStringLiteral("color"), journal->color());
    if (journal->url().isValid())
        obj.insert(QStringLiteral("url"), journal->url().toString());
    if (!journal->categories().isEmpty()) {
        QJsonArray arr;
        for (const auto& c : journal->categories())
            arr.append(c);
        obj.insert(QStringLiteral("categories"), arr);
    }
    // providerExtras["x-ical"] — unmapped X- custom properties.
    const auto customProps = journal->customProperties();
    if (!customProps.isEmpty()) {
        QJsonObject xical;
        for (auto it = customProps.constBegin(); it != customProps.constEnd(); ++it)
            xical.insert(QString::fromLatin1(it.key()), it.value());
        if (!xical.isEmpty()) {
            QJsonObject extras;
            extras.insert(QStringLiteral("x-ical"), xical);
            obj.insert(providerExtrasKey(), extras);
        }
    }
    return obj;
}

QByteArray canonObjectToJournalBytes(const QJsonObject& obj)
{
    if (obj.isEmpty())
        return {};
    KCalendarCore::Journal::Ptr journal(new KCalendarCore::Journal);
    const QString uid = obj.value(QStringLiteral("uid")).toString();
    if (!uid.isEmpty())
        journal->setUid(uid);
    {
        const QString created = obj.value(QStringLiteral("created")).toString();
        if (!created.isEmpty())
            journal->setCreated(QDateTime::fromString(created, Qt::ISODate));
        const QString lastMod = obj.value(QStringLiteral("lastModified")).toString();
        if (!lastMod.isEmpty())
            journal->setLastModified(QDateTime::fromString(lastMod, Qt::ISODate));
    }
    if (const QJsonValue seq = obj.value(QStringLiteral("sequence")); !seq.isUndefined())
        journal->setRevision(seq.toInt());
    if (const QString s = obj.value(QStringLiteral("summary")).toString(); !s.isEmpty())
        journal->setSummary(s);
    if (const QString d = obj.value(QStringLiteral("description")).toString(); !d.isEmpty())
        journal->setDescription(d);
    {
        const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
        const bool allDay = obj.value(QStringLiteral("allDay")).toBool();
        if (!start.isEmpty()) {
            if (start.contains(QStringLiteral("date"))) {
                const QDate dd = QDate::fromString(
                    start.value(QStringLiteral("date")).toString(), Qt::ISODate);
                if (dd.isValid()) {
                    journal->setDtStart(QDateTime(dd, QTime(0,0,0), QTimeZone::utc()));
                    journal->setAllDay(true);
                }
            } else {
                const QDateTime dt = QDateTime::fromString(
                    start.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
                if (dt.isValid()) {
                    journal->setDtStart(dt);
                    journal->setAllDay(allDay);
                }
            }
        }
    }
    if (const QString st = obj.value(QStringLiteral("status")).toString(); !st.isEmpty()) {
        const auto status = journalStatusFromString(st);
        if (status != KCalendarCore::Incidence::StatusNone)
            journal->setStatus(status);
    }
    {
        const QString cls = obj.value(QStringLiteral("classification")).toString();
        if (cls == QStringLiteral("private"))
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
        else if (cls == QStringLiteral("confidential"))
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyConfidential);
        else
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyPublic);
    }
    if (const QString c = obj.value(QStringLiteral("color")).toString(); !c.isEmpty())
        journal->setColor(c);
    if (const QString u = obj.value(QStringLiteral("url")).toString(); !u.isEmpty())
        journal->setUrl(QUrl(u));
    {
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty()) {
            QStringList catList;
            for (const auto& c : cats)
                catList << c.toString();
            journal->setCategories(catList);
        }
    }
    {
        const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        const QJsonObject xical  = extras.value(QStringLiteral("x-ical")).toObject();
        for (auto it = xical.constBegin(); it != xical.constEnd(); ++it)
            journal->setNonKDECustomProperty(it.key().toLatin1(), it.value().toString());
    }
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(journal).toUtf8();
}

Kalburator::Shape::LossProfile canonToVjournalLoss()
{
    // VJOURNAL maps its full field-set; no non-reversible loss to declare.
    return Kalburator::Shape::LossProfile{};
}

}  // namespace Kalburator::Calendar
