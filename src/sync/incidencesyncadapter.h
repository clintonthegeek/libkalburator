#ifndef INCIDENCESYNCADAPTER_H
#define INCIDENCESYNCADAPTER_H

#include "isyncrecord.h"
#include <KCalendarCore/Incidence>
#include <KCalendarCore/ICalFormat>
#include <QCryptographicHash>

/**
 * @brief Adapter wrapping KCalendarCore::Incidence as an ISyncRecord.
 *
 * This allows incidences to be used with the generic sync infrastructure
 * that will eventually be shared via qsynccore.
 */
class IncidenceSyncAdapter : public ISyncRecord {
public:
    explicit IncidenceSyncAdapter(const KCalendarCore::Incidence::Ptr &incidence,
                                   const QString &calendarId = QString(),
                                   bool dirty = false,
                                   bool deleted = false)
        : m_incidence(incidence)
        , m_calendarId(calendarId)
        , m_dirty(dirty)
        , m_deleted(deleted)
    {
    }

    QString id() const override {
        return m_incidence ? m_incidence->uid() : QString();
    }

    QString contentHash() const override {
        if (!m_incidence) {
            return QString();
        }
        // Serialize to iCal and hash
        KCalendarCore::ICalFormat format;
        QString ical = format.toICalString(m_incidence);
        QByteArray hash = QCryptographicHash::hash(ical.toUtf8(), QCryptographicHash::Sha256);
        return QString::fromLatin1(hash.toHex());
    }

    QString description() const override {
        if (!m_incidence) {
            return QStringLiteral("<null>");
        }
        return m_incidence->summary();
    }

    QDateTime lastModified() const override {
        return m_incidence ? m_incidence->lastModified() : QDateTime();
    }

    bool isDirty() const override {
        return m_dirty;
    }

    bool isDeleted() const override {
        return m_deleted;
    }

    QByteArray serialize() const override {
        if (!m_incidence) {
            return QByteArray();
        }
        KCalendarCore::ICalFormat format;
        return format.toICalString(m_incidence).toUtf8();
    }

    // Accessors for the underlying incidence
    KCalendarCore::Incidence::Ptr incidence() const { return m_incidence; }
    QString calendarId() const { return m_calendarId; }

    void setDirty(bool dirty) { m_dirty = dirty; }
    void setDeleted(bool deleted) { m_deleted = deleted; }

private:
    KCalendarCore::Incidence::Ptr m_incidence;
    QString m_calendarId;
    bool m_dirty;
    bool m_deleted;
};

#endif // INCIDENCESYNCADAPTER_H
