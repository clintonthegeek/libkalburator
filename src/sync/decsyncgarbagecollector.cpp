#include "decsyncgarbagecollector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QDebug>

// ============================================================================
// Construction
// ============================================================================

DecSyncGarbageCollector::DecSyncGarbageCollector(DecSyncCollection *collection,
                                                 DecSyncControllerStore *store,
                                                 const QString &ownAppId,
                                                 const QString &collectionId,
                                                 QObject *parent)
    : QObject(parent)
    , m_collection(collection)
    , m_store(store)
    , m_ownAppId(ownAppId)
    , m_collectionId(collectionId)
{
}

// ============================================================================
// selfCompact
// ============================================================================

void DecSyncGarbageCollector::selfCompact()
{
    const QString ownAppDir = m_collection->collectionDir()
                              + QStringLiteral("/v2/") + m_ownAppId;

    QDir dir(ownAppDir);
    if (!dir.exists()) {
        return;
    }

    const QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &fileName : files) {
        if (fileName == QStringLiteral("sequences")) {
            continue;
        }
        // selfCompact does NOT check baselines — only dedup within own file
        compactHashFile(ownAppDir, fileName, /*checkBaseline=*/false);
    }
}

// ============================================================================
// compactInactiveApps
// ============================================================================

void DecSyncGarbageCollector::compactInactiveApps(int inactiveDays)
{
    const QStringList inactive = m_store->inactiveApps(m_collectionId, inactiveDays);

    const QString v2Dir = m_collection->collectionDir() + QStringLiteral("/v2");

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    for (const QString &appId : inactive) {
        if (appId == m_ownAppId) {
            continue;
        }

        const QString appDir = v2Dir + QStringLiteral("/") + appId;
        QDir dir(appDir);
        if (!dir.exists()) {
            continue;
        }

        const QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);

        // Read current sequences so we can bump after compaction
        // sequences file: {"hashName": seqNum, ...}
        QMap<QString, int> sequences;
        {
            const QString seqPath = appDir + QStringLiteral("/sequences");
            QFile seqFile(seqPath);
            if (seqFile.exists() && seqFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QByteArray data = seqFile.readAll();
                seqFile.close();
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    for (auto it = obj.begin(); it != obj.end(); ++it) {
                        sequences[it.key()] = it.value().toInt();
                    }
                }
            }
        }

        bool anyModified = false;
        for (const QString &fileName : files) {
            if (fileName == QStringLiteral("sequences")) {
                continue;
            }
            const bool modified = compactHashFile(appDir, fileName, /*checkBaseline=*/true);
            if (modified) {
                sequences[fileName] = sequences.value(fileName, 0) + 1;
                anyModified = true;
            }
        }

        if (anyModified) {
            // Write updated sequences file
            const QString seqPath = appDir + QStringLiteral("/sequences");
            QJsonObject obj;
            for (auto it = sequences.constBegin(); it != sequences.constEnd(); ++it) {
                obj[it.key()] = it.value();
            }
            QSaveFile seqFile(seqPath);
            if (seqFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                seqFile.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
                seqFile.write("\n");
                if (!seqFile.commit()) {
                    qWarning() << "DecSyncGarbageCollector::compactInactiveApps: "
                                  "failed to write sequences for" << appId;
                }
            }

            // Record compaction timestamp
            m_store->recordAppCompaction(m_collectionId, appId, now);
        }
    }
}

// ============================================================================
// purgeDeadApps
// ============================================================================

void DecSyncGarbageCollector::purgeDeadApps(int deadDays)
{
    const QStringList dead = m_store->inactiveApps(m_collectionId, deadDays);

    const QString v2Dir = m_collection->collectionDir() + QStringLiteral("/v2");

    for (const QString &appId : dead) {
        if (appId == m_ownAppId) {
            continue;
        }

        const QString appDir = v2Dir + QStringLiteral("/") + appId;
        QDir dir(appDir);
        if (dir.exists()) {
            if (!dir.removeRecursively()) {
                qWarning() << "DecSyncGarbageCollector::purgeDeadApps: "
                              "failed to remove directory" << appDir;
                continue;
            }
        }

        m_store->removeAppActivity(m_collectionId, appId);
    }
}

// ============================================================================
// onboardNewApps
// ============================================================================

void DecSyncGarbageCollector::onboardNewApps()
{
    const QStringList currentApps = m_collection->listAppIds();
    const QStringList newAppIds = m_store->newApps(m_collectionId, currentApps);

    if (newAppIds.isEmpty()) {
        return;
    }

    // Build the authoritative set of entries to write:
    //   - all baselines (as resource entries with current timestamp)
    //   - all active deletions (as null-value resource entries)

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QList<DecSyncEntry> entries;

    // Baselines
    const QMap<QString, BaselineRecord> baselines = m_store->allBaselines(m_collectionId);
    for (auto it = baselines.constBegin(); it != baselines.constEnd(); ++it) {
        DecSyncEntry e;
        e.path     = {QStringLiteral("resources"), it.key()};
        e.datetime = now;
        e.key      = QJsonValue(QJsonValue::Null);
        e.value    = QJsonValue(it.value().icalData);
        entries.append(e);
    }

    // Active deletions
    const QList<DeletionRecord> deletions = m_store->activeDeletions(m_collectionId);
    for (const DeletionRecord &rec : deletions) {
        DecSyncEntry e;
        e.path     = {QStringLiteral("resources"), rec.uid};
        e.datetime = now;
        e.key      = QJsonValue(QJsonValue::Null);
        e.value    = QJsonValue(QJsonValue::Null);  // null value = deletion marker
        entries.append(e);
    }

    if (!entries.isEmpty()) {
        m_collection->setEntries(entries);
    }

    // Record all new apps as known in the store
    for (const QString &appId : newAppIds) {
        m_store->recordAppActivity(m_collectionId, appId, now);
    }
}

// ============================================================================
// runAll
// ============================================================================

void DecSyncGarbageCollector::runAll(int inactiveDays, int deadDays)
{
    selfCompact();
    onboardNewApps();
    compactInactiveApps(inactiveDays);
    purgeDeadApps(deadDays);

    emit gcCompleted(0, 0, 0);
}

// ============================================================================
// Private helpers
// ============================================================================

QString DecSyncGarbageCollector::dedupKey(const DecSyncEntry &entry)
{
    // Resource entries: "resources/uid"
    // Info entries: "info|keyString"
    // General: path joined with "/" for resource, or path + "|" + key for info
    if (entry.path.size() >= 2 && entry.path[0] == QStringLiteral("resources")) {
        return entry.path.join(QStringLiteral("/"));
    }
    // Info or other path types: path + "|" + key
    return entry.path.join(QStringLiteral("/"))
           + QStringLiteral("|")
           + entry.key.toString();
}

bool DecSyncGarbageCollector::compactHashFile(const QString &appDir,
                                               const QString &hashName,
                                               bool checkBaseline)
{
    const QString filePath = appDir + QStringLiteral("/") + hashName;

    QFile file(filePath);
    if (!file.exists()) {
        return false;
    }

    // Read all entries from the file
    QList<DecSyncEntry> allEntries;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "DecSyncGarbageCollector::compactHashFile: cannot open" << filePath;
        return false;
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine());
        if (line.trimmed().isEmpty()) {
            continue;
        }
        DecSyncEntry entry = DecSyncEntry::fromLine(line);
        if (entry.isValid()) {
            allEntries.append(entry);
        }
    }
    file.close();

    const int originalCount = allEntries.size();
    if (originalCount == 0) {
        return false;
    }

    // Dedup: for each key, keep only the newest entry
    QMap<QString, DecSyncEntry> newest;
    for (const DecSyncEntry &entry : allEntries) {
        const QString key = dedupKey(entry);
        if (!newest.contains(key) || entry.isNewerThan(newest[key])) {
            newest[key] = entry;
        }
    }

    // If checkBaseline: drop entries where the controller baseline has a
    // newer written_at. "Newer" means baseline.writtenAt > entry.datetime.
    QList<DecSyncEntry> compacted;
    for (auto it = newest.constBegin(); it != newest.constEnd(); ++it) {
        const DecSyncEntry &entry = it.value();

        if (checkBaseline
            && entry.path.size() == 2
            && entry.path[0] == QStringLiteral("resources"))
        {
            const QString uid = entry.path[1];
            auto bl = m_store->baseline(m_collectionId, uid);
            if (bl.has_value() && bl->writtenAt > entry.datetime) {
                // Baseline is newer — this entry is superseded; drop it
                continue;
            }
        }

        compacted.append(entry);
    }

    // Only write back if the file actually shrank
    if (compacted.size() >= originalCount) {
        return false;
    }

    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DecSyncGarbageCollector::compactHashFile: cannot write" << filePath;
        return false;
    }

    for (const DecSyncEntry &entry : compacted) {
        saveFile.write(entry.toLine().toUtf8());
        saveFile.write("\n");
    }

    if (!saveFile.commit()) {
        qWarning() << "DecSyncGarbageCollector::compactHashFile: commit failed for" << filePath;
        return false;
    }

    return true;
}
