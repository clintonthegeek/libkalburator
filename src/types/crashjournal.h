#ifndef CRASHJOURNAL_H
#define CRASHJOURNAL_H

#include <QString>
#include <QJsonObject>
#include <QStringList>
#include <functional>

/**
 * @brief Generic append-only JSON-lines journal for crash recovery.
 *
 * Provides a shared engine for domain-specific journals (BlockJournal,
 * CalendarJournal). Each entity gets its own journal file. Entries are
 * appended as compact JSON, one per line. Files are removed on truncate.
 *
 * Not a QObject — pure value-type utility.
 */
class CrashJournal {
public:
    /**
     * @param directory  Directory where journal files are stored
     * @param suffix     File suffix (e.g. ".planstan.journal", ".calendar.journal")
     */
    explicit CrashJournal(const QString &directory,
                          const QString &suffix = QStringLiteral(".journal"));

    /** Append a JSON entry to the journal for @p entityId. */
    void append(const QString &entityId, const QJsonObject &entry);

    /** Delete the journal file for @p entityId. */
    void truncate(const QString &entityId);

    /** True if a non-empty journal exists for @p entityId. */
    bool hasJournal(const QString &entityId) const;

    /** Return entity IDs that have non-empty journal files. */
    QStringList entitiesWithJournals() const;

    /**
     * Replay journal entries for @p entityId, calling @p handler for each.
     * Returns the number of entries successfully handled.
     * Malformed lines are skipped with a warning.
     */
    int replay(const QString &entityId,
               const std::function<void(const QJsonObject &)> &handler) const;

private:
    QString journalPath(const QString &entityId) const;
    QString m_directory;
    QString m_suffix;
};

#endif // CRASHJOURNAL_H
