#include "decsynclib.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>
#include <QSaveFile>

namespace Kalburator::Sync {

// ============================================================================
// DecSyncEntry
// ============================================================================

QJsonArray DecSyncEntry::toJson() const
{
    QJsonArray arr;
    QJsonArray pathArr;
    for (const QString &p : path) {
        pathArr.append(p);
    }
    arr.append(pathArr);
    arr.append(datetime);
    arr.append(key);
    arr.append(value);
    return arr;
}

DecSyncEntry DecSyncEntry::fromLine(const QString &line)
{
    DecSyncEntry entry;
    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return entry;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "DecSyncEntry::fromLine: Failed to parse:" << trimmed
                    << parseError.errorString();
        return entry;
    }

    QJsonArray arr = doc.array();
    if (arr.size() != 4) {
        qWarning() << "DecSyncEntry::fromLine: Expected 4 elements, got" << arr.size();
        return entry;
    }

    // Element 0: path (array of strings)
    if (!arr[0].isArray()) {
        qWarning() << "DecSyncEntry::fromLine: path is not an array";
        return entry;
    }
    QJsonArray pathArr = arr[0].toArray();
    for (const QJsonValue &v : pathArr) {
        entry.path.append(v.toString());
    }

    // Element 1: datetime (string)
    entry.datetime = arr[1].toString();

    // Element 2: key (any JSON value)
    entry.key = arr[2];

    // Element 3: value (any JSON value)
    entry.value = arr[3];

    return entry;
}

QString DecSyncEntry::toLine() const
{
    QJsonDocument doc(toJson());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

bool DecSyncEntry::isNewerThan(const DecSyncEntry &other) const
{
    return datetime > other.datetime;
}

bool DecSyncEntry::isValid() const
{
    return !path.isEmpty() && !datetime.isEmpty();
}

// ============================================================================
// DecSyncHash
// ============================================================================

QString DecSyncHash::pathToHash(const QStringList &path)
{
    if (path == QStringList{QStringLiteral("info")}) {
        return QStringLiteral("info");
    }

    QList<int> stringHashes;
    stringHashes.reserve(path.size());
    for (const QString &s : path) {
        stringHashes.append(stringToHash(s));
    }

    int hash = polyHash(199, stringHashes);
    return QStringLiteral("%1").arg(hash, 2, 16, QLatin1Char('0'));
}

int DecSyncHash::stringToHash(const QString &s)
{
    QByteArray utf8 = s.toUtf8();
    QList<int> bytes;
    bytes.reserve(utf8.size());
    for (int i = 0; i < utf8.size(); ++i) {
        // Kotlin's byte.toInt() produces signed values (-128 to 127).
        // We must match that behavior for hash compatibility.
        bytes.append(static_cast<signed char>(utf8[i]));
    }
    return polyHash(19, bytes);
}

int DecSyncHash::polyHash(int p, const QList<int> &xs)
{
    int hash = 0;
    for (int x : xs) {
        hash *= p;
        hash += x;
        hash %= HASH_BINS;
        // Ensure positive result (C++ modulo can be negative)
        if (hash < 0) {
            hash += HASH_BINS;
        }
    }
    return hash;
}

// ============================================================================
// DecSyncCollection
// ============================================================================

DecSyncCollection::DecSyncCollection(const QString &collectionDir, const QString &appId)
    : m_collectionDir(collectionDir)
    , m_appId(appId)
{
}

void DecSyncCollection::setEntry(const QStringList &path, const QJsonValue &key, const QJsonValue &value)
{
    DecSyncEntry entry;
    entry.path = path;
    entry.datetime = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    entry.key = key;
    entry.value = value;
    setEntries({entry});
}

void DecSyncCollection::setEntries(const QList<DecSyncEntry> &entries)
{
    if (entries.isEmpty()) {
        return;
    }

    ensureDirectoryStructure();

    QString appDir = ownAppDir();

    // Group entries by hash
    QMap<QString, QList<DecSyncEntry>> byHash;
    for (const DecSyncEntry &entry : entries) {
        QString hash = DecSyncHash::pathToHash(entry.path);
        byHash[hash].append(entry);
    }

    // Read current sequences
    QMap<QString, int> sequences = readSequences(appDir);

    // Write each hash group
    for (auto it = byHash.constBegin(); it != byHash.constEnd(); ++it) {
        const QString &hash = it.key();
        const QList<DecSyncEntry> &hashEntries = it.value();

        writeHashFile(appDir, hash, hashEntries);

        // Increment sequence number
        sequences[hash] = sequences.value(hash, 0) + 1;
    }

    // Write updated sequences
    writeSequences(appDir, sequences);

    // Update local info
    updateLocalInfo();
}

QMap<QString, DecSyncEntry> DecSyncCollection::readAllResources() const
{
    // Merge across all appId directories with last-write-wins
    QMap<QString, DecSyncEntry> result;

    QDir v2Dir(m_collectionDir + QStringLiteral("/v2"));
    if (!v2Dir.exists()) {
        return result;
    }

    const QStringList appDirs = v2Dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // We need to read all hash files (00-ff) from all apps and merge
    for (const QString &appId : appDirs) {
        QString appDir = v2Dir.filePath(appId);

        // Read all hash files (00-ff)
        QDir appDirObj(appDir);
        const QStringList hashFiles = appDirObj.entryList(QDir::Files | QDir::NoDotAndDotDot);

        for (const QString &hashFile : hashFiles) {
            if (hashFile == QStringLiteral("sequences")) {
                continue;
            }

            QList<DecSyncEntry> entries = readHashFile(appDir, hashFile);
            for (const DecSyncEntry &entry : entries) {
                // Only process resource entries
                if (entry.path.size() < 2 || entry.path[0] != QStringLiteral("resources")) {
                    continue;
                }

                QString uid = entry.path[1];

                // Last-write-wins: keep newer entry
                if (!result.contains(uid) || entry.isNewerThan(result[uid])) {
                    result[uid] = entry;
                }
            }
        }
    }

    // Remove entries with null value (deleted resources)
    QMutableMapIterator<QString, DecSyncEntry> it(result);
    while (it.hasNext()) {
        it.next();
        if (it.value().value.isNull()) {
            it.remove();
        }
    }

    return result;
}

QMap<QString, QMap<QString, DecSyncEntry>> DecSyncCollection::readPerAppResources() const
{
    QMap<QString, QMap<QString, DecSyncEntry>> result;

    QDir v2Dir(m_collectionDir + QStringLiteral("/v2"));
    if (!v2Dir.exists()) {
        return result;
    }

    const QStringList appDirs = v2Dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &appId : appDirs) {
        QString appDir = v2Dir.filePath(appId);

        QDir appDirObj(appDir);
        const QStringList hashFiles = appDirObj.entryList(QDir::Files | QDir::NoDotAndDotDot);

        QMap<QString, DecSyncEntry> appEntries;

        for (const QString &hashFile : hashFiles) {
            if (hashFile == QStringLiteral("sequences")) {
                continue;
            }

            QList<DecSyncEntry> entries = readHashFile(appDir, hashFile);
            for (const DecSyncEntry &entry : entries) {
                // Only process resource entries
                if (entry.path.size() < 2 || entry.path[0] != QStringLiteral("resources")) {
                    continue;
                }

                QString uid = entry.path[1];

                // Per-app dedup: keep newest per uid within same app
                if (!appEntries.contains(uid) || entry.isNewerThan(appEntries[uid])) {
                    appEntries[uid] = entry;
                }
            }
        }

        if (!appEntries.isEmpty()) {
            result[appId] = appEntries;
        }
    }

    // Note: null values (deletions) are intentionally NOT filtered out,
    // unlike readAllResources(). The controller needs to see deletions.
    return result;
}

QMap<QString, DecSyncEntry> DecSyncCollection::readInfoEntries() const
{
    QMap<QString, DecSyncEntry> result;

    QDir v2Dir(m_collectionDir + QStringLiteral("/v2"));
    if (!v2Dir.exists()) {
        return result;
    }

    const QStringList appDirs = v2Dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &appId : appDirs) {
        QString appDir = v2Dir.filePath(appId);

        // Info entries are in the "info" hash file
        QList<DecSyncEntry> entries = readHashFile(appDir, QStringLiteral("info"));
        for (const DecSyncEntry &entry : entries) {
            if (entry.path != QStringList{QStringLiteral("info")}) {
                continue;
            }

            // Key must be a string for info entries
            QString keyStr = entry.key.toString();
            if (keyStr.isEmpty()) {
                continue;
            }

            // Last-write-wins
            if (!result.contains(keyStr) || entry.isNewerThan(result[keyStr])) {
                result[keyStr] = entry;
            }
        }
    }

    return result;
}

bool DecSyncCollection::hasNewEntries() const
{
    QDir v2Dir(m_collectionDir + QStringLiteral("/v2"));
    if (!v2Dir.exists()) {
        return false;
    }

    QMap<QString, QMap<QString, int>> localSeqs = readLocalSequences();

    const QStringList appDirs = v2Dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &otherAppId : appDirs) {
        if (otherAppId == m_appId) {
            continue;
        }

        QString otherAppDir = v2Dir.filePath(otherAppId);
        QMap<QString, int> otherSeqs = readSequences(otherAppDir);
        QMap<QString, int> ourTracked = localSeqs.value(otherAppId);

        for (auto it = otherSeqs.constBegin(); it != otherSeqs.constEnd(); ++it) {
            if (it.value() > ourTracked.value(it.key(), 0)) {
                return true;
            }
        }
    }

    return false;
}

QList<DecSyncEntry> DecSyncCollection::getNewEntries()
{
    QList<DecSyncEntry> result;

    QDir v2Dir(m_collectionDir + QStringLiteral("/v2"));
    if (!v2Dir.exists()) {
        return result;
    }

    QMap<QString, QMap<QString, int>> localSeqs = readLocalSequences();

    const QStringList appDirs = v2Dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &otherAppId : appDirs) {
        if (otherAppId == m_appId) {
            continue;
        }

        QString otherAppDir = v2Dir.filePath(otherAppId);
        QMap<QString, int> otherSeqs = readSequences(otherAppDir);
        QMap<QString, int> &ourTracked = localSeqs[otherAppId];

        for (auto it = otherSeqs.constBegin(); it != otherSeqs.constEnd(); ++it) {
            const QString &hash = it.key();
            int otherSeq = it.value();
            int ourSeq = ourTracked.value(hash, 0);

            if (otherSeq > ourSeq) {
                // New entries exist in this hash file
                QList<DecSyncEntry> entries = readHashFile(otherAppDir, hash);
                result.append(entries);

                // Update our tracked sequence
                ourTracked[hash] = otherSeq;
            }
        }
    }

    // Write updated local sequences
    if (!result.isEmpty()) {
        writeLocalSequences(localSeqs);
    }

    return result;
}

QStringList DecSyncCollection::listAppIds() const
{
    QDir v2Dir(m_collectionDir + QStringLiteral("/v2"));
    if (!v2Dir.exists()) {
        return {};
    }
    return v2Dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

void DecSyncCollection::ensureDirectoryStructure()
{
    QDir().mkpath(ownAppDir());
    QDir().mkpath(localAppDir());
}

// ---------- Private helpers ----------

QList<DecSyncEntry> DecSyncCollection::readHashFile(const QString &appDir, const QString &hashName) const
{
    QList<DecSyncEntry> result;
    QString filePath = appDir + QStringLiteral("/") + hashName;

    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        if (line.trimmed().isEmpty()) {
            continue;
        }
        DecSyncEntry entry = DecSyncEntry::fromLine(line);
        if (entry.isValid()) {
            result.append(entry);
        }
    }

    file.close();
    return result;
}

void DecSyncCollection::writeHashFile(const QString &appDir, const QString &hashName,
                                       const QList<DecSyncEntry> &newEntries)
{
    QString filePath = appDir + QStringLiteral("/") + hashName;

    // Read existing entries
    QList<DecSyncEntry> existing = readHashFile(appDir, hashName);

    // Build set of new entry keys for dedup
    QSet<QString> newKeys;
    for (const DecSyncEntry &e : newEntries) {
        newKeys.insert(entryKey(e));
    }

    // Keep existing entries that don't conflict with new ones
    QList<DecSyncEntry> merged;
    for (const DecSyncEntry &e : existing) {
        if (!newKeys.contains(entryKey(e))) {
            merged.append(e);
        }
    }

    // Append new entries
    merged.append(newEntries);

    // Write all entries
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DecSyncCollection::writeHashFile: Failed to open" << filePath
                    << file.errorString();
        return;
    }

    for (const DecSyncEntry &e : merged) {
        file.write(e.toLine().toUtf8());
        file.write("\n");
    }

    if (!file.commit()) {
        qWarning() << "DecSyncCollection::writeHashFile: Failed to commit" << filePath
                    << file.errorString();
    }
}

QMap<QString, int> DecSyncCollection::readSequences(const QString &appDir) const
{
    QMap<QString, int> result;
    QString filePath = appDir + QStringLiteral("/sequences");

    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }

    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        result[it.key()] = it.value().toInt();
    }

    return result;
}

void DecSyncCollection::writeSequences(const QString &appDir, const QMap<QString, int> &sequences)
{
    QString filePath = appDir + QStringLiteral("/sequences");

    QJsonObject obj;
    for (auto it = sequences.constBegin(); it != sequences.constEnd(); ++it) {
        obj[it.key()] = it.value();
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DecSyncCollection::writeSequences: Failed to open" << filePath;
        return;
    }

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.write("\n");

    if (!file.commit()) {
        qWarning() << "DecSyncCollection::writeSequences: Failed to commit" << filePath;
    }
}

QMap<QString, QMap<QString, int>> DecSyncCollection::readLocalSequences() const
{
    QMap<QString, QMap<QString, int>> result;
    QString filePath = localAppDir() + QStringLiteral("/sequences");

    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }

    QJsonObject outerObj = doc.object();
    for (auto it = outerObj.begin(); it != outerObj.end(); ++it) {
        QMap<QString, int> inner;
        QJsonObject innerObj = it.value().toObject();
        for (auto jt = innerObj.begin(); jt != innerObj.end(); ++jt) {
            inner[jt.key()] = jt.value().toInt();
        }
        result[it.key()] = inner;
    }

    return result;
}

void DecSyncCollection::writeLocalSequences(const QMap<QString, QMap<QString, int>> &localSeqs)
{
    ensureDirectoryStructure();
    QString filePath = localAppDir() + QStringLiteral("/sequences");

    QJsonObject outerObj;
    for (auto it = localSeqs.constBegin(); it != localSeqs.constEnd(); ++it) {
        QJsonObject innerObj;
        const QMap<QString, int> &inner = it.value();
        for (auto jt = inner.constBegin(); jt != inner.constEnd(); ++jt) {
            innerObj[jt.key()] = jt.value();
        }
        outerObj[it.key()] = innerObj;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DecSyncCollection::writeLocalSequences: Failed to open" << filePath;
        return;
    }

    file.write(QJsonDocument(outerObj).toJson(QJsonDocument::Compact));
    file.write("\n");

    if (!file.commit()) {
        qWarning() << "DecSyncCollection::writeLocalSequences: Failed to commit" << filePath;
    }
}

void DecSyncCollection::updateLocalInfo()
{
    ensureDirectoryStructure();
    QString filePath = localAppDir() + QStringLiteral("/info");

    QJsonObject obj;
    obj[QStringLiteral("version")] = 2;
    obj[QStringLiteral("last-active")] = QDate::currentDate().toString(Qt::ISODate);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DecSyncCollection::updateLocalInfo: Failed to open" << filePath;
        return;
    }

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.write("\n");

    if (!file.commit()) {
        qWarning() << "DecSyncCollection::updateLocalInfo: Failed to commit" << filePath;
    }
}

QString DecSyncCollection::ownAppDir() const
{
    return m_collectionDir + QStringLiteral("/v2/") + m_appId;
}

QString DecSyncCollection::localAppDir() const
{
    return m_collectionDir + QStringLiteral("/local/") + m_appId;
}

QString DecSyncCollection::entryKey(const DecSyncEntry &entry)
{
    // Combine path + key into a unique string for deduplication
    QJsonArray pathArr;
    for (const QString &p : entry.path) {
        pathArr.append(p);
    }
    QJsonArray combined;
    combined.append(pathArr);
    combined.append(entry.key);
    return QString::fromUtf8(QJsonDocument(combined).toJson(QJsonDocument::Compact));
}

// ============================================================================
// DecSyncDir
// ============================================================================

DecSyncDir::DecSyncDir(const QString &decsyncDir)
    : m_decsyncDir(decsyncDir)
{
}

bool DecSyncDir::checkOrCreateInfo()
{
    QDir dir(m_decsyncDir);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            qWarning() << "DecSyncDir::checkOrCreateInfo: Failed to create" << m_decsyncDir;
            return false;
        }
    }

    QString infoPath = m_decsyncDir + QStringLiteral("/.decsync-info");
    QFile file(infoPath);

    if (file.exists()) {
        // Verify existing file
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "DecSyncDir::checkOrCreateInfo: Failed to read" << infoPath;
            return false;
        }
        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            qWarning() << "DecSyncDir::checkOrCreateInfo: Invalid JSON in" << infoPath;
            return false;
        }

        int version = doc.object().value(QStringLiteral("version")).toInt();
        if (version < 2) {
            qWarning() << "DecSyncDir::checkOrCreateInfo: Unsupported version" << version;
            return false;
        }
        return true;
    }

    // Create new .decsync-info
    QJsonObject obj;
    obj[QStringLiteral("version")] = 2;

    QSaveFile saveFile(infoPath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "DecSyncDir::checkOrCreateInfo: Failed to create" << infoPath;
        return false;
    }

    saveFile.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    saveFile.write("\n");

    if (!saveFile.commit()) {
        qWarning() << "DecSyncDir::checkOrCreateInfo: Failed to commit" << infoPath;
        return false;
    }

    return true;
}

QStringList DecSyncDir::listCollections(const QString &syncType) const
{
    QDir typeDir(m_decsyncDir + QStringLiteral("/") + syncType);
    if (!typeDir.exists()) {
        return {};
    }
    return typeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

QMap<QString, QJsonValue> DecSyncDir::getStaticInfo(const QString &syncType,
                                                     const QString &collection) const
{
    QMap<QString, QJsonValue> result;

    QString collectionDir = m_decsyncDir + QStringLiteral("/") + syncType
                            + QStringLiteral("/") + collection;

    DecSyncCollection coll(collectionDir, QStringLiteral("_reader"));
    QMap<QString, DecSyncEntry> infoEntries = coll.readInfoEntries();

    for (auto it = infoEntries.constBegin(); it != infoEntries.constEnd(); ++it) {
        result[it.key()] = it.value().value;
    }

    return result;
}

DecSyncCollection* DecSyncDir::openCollection(const QString &syncType,
                                               const QString &collection,
                                               const QString &appId)
{
    QString collectionDir = m_decsyncDir + QStringLiteral("/") + syncType
                            + QStringLiteral("/") + collection;
    return new DecSyncCollection(collectionDir, appId);
}


} // namespace Kalburator::Sync
