#ifndef ISYNCRECORD_H
#define ISYNCRECORD_H

#include <QString>
#include <QDateTime>
#include <QByteArray>

namespace Kalburator::Sync {

/**
 * @file isyncrecord.h
 * @brief Abstract interface for records that can be synchronized.
 *
 * This interface abstracts the differences between various record types
 * (KCalendarCore::Incidence, Palm records, etc.) for the sync engine.
 * Designed for future extraction to qsynccore shared library.
 */

class ISyncRecord {
public:
    virtual ~ISyncRecord() = default;

    /**
     * @brief Unique identifier for this record.
     */
    virtual QString id() const = 0;

    /**
     * @brief Content hash for change detection.
     *
     * Should return a consistent hash of the record's content,
     * used by BaselineStore for detecting modifications.
     */
    virtual QString contentHash() const = 0;

    /**
     * @brief Human-readable description for logging and UI.
     */
    virtual QString description() const = 0;

    /**
     * @brief Last modification timestamp.
     */
    virtual QDateTime lastModified() const = 0;

    /**
     * @brief Whether the record has been modified since last sync.
     */
    virtual bool isDirty() const = 0;

    /**
     * @brief Whether the record has been marked for deletion.
     */
    virtual bool isDeleted() const = 0;

    /**
     * @brief Serialize the record to bytes for storage/comparison.
     */
    virtual QByteArray serialize() const = 0;
};

} // namespace Kalburator::Sync

#endif // ISYNCRECORD_H
