#ifndef DECSYNCLIB_H
#define DECSYNCLIB_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QJsonValue>
#include <QJsonArray>

/**
 * @brief A single DecSync entry (one line in an entry file).
 *
 * Format: [path, datetime, key, value]
 * Example: [["resources","uid123"],"2024-01-15T10:30:00",null,"BEGIN:VCALENDAR\r\n..."]
 */
struct DecSyncEntry
{
    QStringList path;       ///< e.g., ["resources", "uid123"] or ["info"]
    QString datetime;       ///< ISO 8601: "2024-01-15T10:30:00"
    QJsonValue key;         ///< JSON key (null for resources)
    QJsonValue value;       ///< JSON value (iCal string, or null for deletion)

    QJsonArray toJson() const;
    static DecSyncEntry fromLine(const QString &line);
    QString toLine() const;
    bool isNewerThan(const DecSyncEntry &other) const;
    bool isValid() const;
};

/**
 * @brief DecSync v2 path hashing.
 *
 * Ported from libdecsync Hash.kt. Uses polynomial hash mod 256
 * with two levels: string-level (p=19) and path-level (p=199).
 *
 * Special case: ["info"] always hashes to literal "info".
 * All other paths produce a 2-digit lowercase hex string ("00"-"ff").
 */
class DecSyncHash
{
public:
    static QString pathToHash(const QStringList &path);

private:
    static int stringToHash(const QString &s);
    static int polyHash(int p, const QList<int> &xs);

    static constexpr int HASH_BINS = 256;
};

/**
 * @brief Manages one DecSync collection directory.
 *
 * A collection is e.g. calendars/myCalendar/ or tasks/work/.
 * Handles reading/writing entries across app directories, sequence
 * tracking, and last-write-wins merge.
 */
class DecSyncCollection
{
public:
    DecSyncCollection(const QString &collectionDir, const QString &appId);

    /// Write a single entry to own appId directory
    void setEntry(const QStringList &path, const QJsonValue &key, const QJsonValue &value);

    /// Write multiple entries to own appId directory
    void setEntries(const QList<DecSyncEntry> &entries);

    /// Read merged resource state across all appId directories (last-write-wins).
    /// Returns uid -> entry for non-null values only (deletions excluded).
    QMap<QString, DecSyncEntry> readAllResources() const;

    /// Read resources per-app without merging. Returns appId -> uid -> entry.
    /// Unlike readAllResources(), does NOT apply last-write-wins across apps.
    /// Each app's entries are returned separately, with per-app dedup (latest per uid within same app).
    /// Deleted resources (null value) are included.
    QMap<QString, QMap<QString, DecSyncEntry>> readPerAppResources() const;

    /// Read merged info entries across all appId directories (last-write-wins).
    /// Returns key-string -> entry.
    QMap<QString, DecSyncEntry> readInfoEntries() const;

    /// Check if other apps have written new entries since last read
    bool hasNewEntries() const;

    /// Get new entries from other apps and update local sequences
    QList<DecSyncEntry> getNewEntries();

    /// List all appId directories in v2/
    QStringList listAppIds() const;

    /// Create v2/<appId>/ and local/<appId>/ directories
    void ensureDirectoryStructure();

    /// Get the collection directory path
    QString collectionDir() const { return m_collectionDir; }

    /// Get the appId
    QString appId() const { return m_appId; }

private:
    /// Read all entries from a specific hash file in an appId directory
    QList<DecSyncEntry> readHashFile(const QString &appDir, const QString &hashName) const;

    /// Write entries to a hash file, merging with existing entries (same path+key replaced)
    void writeHashFile(const QString &appDir, const QString &hashName,
                       const QList<DecSyncEntry> &newEntries);

    /// Read the sequences file from an appId directory: hash -> seqNum
    QMap<QString, int> readSequences(const QString &appDir) const;

    /// Write sequences file to an appId directory
    void writeSequences(const QString &appDir, const QMap<QString, int> &sequences);

    /// Read local sequences: appId -> (hash -> seqNum)
    QMap<QString, QMap<QString, int>> readLocalSequences() const;

    /// Write local sequences
    void writeLocalSequences(const QMap<QString, QMap<QString, int>> &localSeqs);

    /// Update local app info (version, last-active)
    void updateLocalInfo();

    /// Path to v2/<appId>/
    QString ownAppDir() const;

    /// Path to local/<appId>/
    QString localAppDir() const;

    /// Generate an entry key string for deduplication (path + JSON key combined)
    static QString entryKey(const DecSyncEntry &entry);

    QString m_collectionDir;
    QString m_appId;
};

/**
 * @brief Top-level DecSync directory manager.
 *
 * Manages the DecSync/ root directory, including .decsync-info
 * and collection enumeration.
 */
class DecSyncDir
{
public:
    explicit DecSyncDir(const QString &decsyncDir);

    /// Check/create .decsync-info with {"version": 2}
    bool checkOrCreateInfo();

    /// List collection IDs for a sync type (e.g., "calendars", "tasks")
    QStringList listCollections(const QString &syncType) const;

    /// Get static info entries for a collection (name, color, deleted status)
    QMap<QString, QJsonValue> getStaticInfo(const QString &syncType,
                                            const QString &collection) const;

    /// Open a collection for reading/writing
    DecSyncCollection* openCollection(const QString &syncType,
                                      const QString &collection,
                                      const QString &appId);

    /// Get the root DecSync directory path
    QString decsyncDir() const { return m_decsyncDir; }

private:
    QString m_decsyncDir;
};

#endif // DECSYNCLIB_H
