#include "rawfilesbackend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;

namespace Kalburator::Sinks {

static constexpr const char *kManifestName = "_shapes.json";

RawFilesBackend::RawFilesBackend(QString rootPath, QObject *parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_rootPath(std::move(rootPath))
{
    loadManifest();
}

bool RawFilesBackend::isAvailable() const
{
    return QDir(m_rootPath).exists();
}

// ---- Collection management ----

QList<CollectionInfo> RawFilesBackend::availableCollections()
{
    return m_collections.values();
}

CollectionInfo RawFilesBackend::collectionInfo(const QString &collectionId)
{
    return m_collections.value(collectionId);
}

QString RawFilesBackend::createCollection(const CollectionInfo &info)
{
    QDir dir(m_rootPath);
    if (!dir.exists() && !dir.mkpath(QLatin1String(".")))
        return {};
    if (!m_collections.contains(info.id)) {
        m_collections[info.id] = info;
        saveManifest();
    }
    return info.id;
}

void RawFilesBackend::deleteCollection(const QString &collectionId)
{
    clearCollection(collectionId);
    m_collections.remove(collectionId);
    saveManifest();
}

void RawFilesBackend::clearCollection(const QString &collectionId)
{
    const QString suffix = QLatin1Char('.') + suffixFor(collectionId);
    QDir dir(m_rootPath);
    const auto entries = dir.entryList(QDir::Files);
    for (const QString &name : entries) {
        if (name.endsWith(suffix))
            QFile::remove(dir.filePath(name));
    }
}

// ---- Record I/O ----

QList<BackendRecord> RawFilesBackend::loadRecords(const QString &collectionId)
{
    const QString suffix = QLatin1Char('.') + suffixFor(collectionId);
    QDir dir(m_rootPath);
    const auto entries = dir.entryList(QDir::Files);
    QList<BackendRecord> result;
    for (const QString &name : entries) {
        if (!name.endsWith(suffix) || name == QLatin1String(kManifestName))
            continue;
        result.append(readFile(dir.filePath(name)));
    }
    return result;
}

std::optional<BackendRecord> RawFilesBackend::loadRecord(const QString &recordId)
{
    if (!QFile::exists(recordId))
        return std::nullopt;
    return readFile(recordId);
}

QString RawFilesBackend::createRecord(const QString &collectionId,
                                      const BackendRecord &record)
{
    if (!m_collections.contains(collectionId))
        return {};
    QDir dir(m_rootPath);
    if (!dir.exists() && !dir.mkpath(QLatin1String(".")))
        return {};

    const QString base = record.id.isEmpty() ? record.displayName : record.id;
    const QString fileName = sanitize(base) + QLatin1Char('.') + suffixFor(collectionId);
    const QString absPath = dir.filePath(fileName);
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(record.data);
    f.close();
    return absPath;
}

bool RawFilesBackend::updateRecord(const BackendRecord &record)
{
    if (!QFile::exists(record.id))
        return false;
    QFile f(record.id);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(record.data);
    return true;
}

bool RawFilesBackend::deleteRecord(const QString &recordId)
{
    return QFile::remove(recordId);
}

// ---- Helpers ----

BackendRecord RawFilesBackend::readFile(const QString &absPath) const
{
    QFile f(absPath);
    BackendRecord r;
    r.id = absPath;
    if (!f.open(QIODevice::ReadOnly))
        return r;
    r.data = f.readAll();
    r.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(r.data, QCryptographicHash::Sha256).toHex());
    QFileInfo info(absPath);
    r.lastModified = info.lastModified().toUTC();
    r.type = QStringLiteral("raw");
    return r;
}

void RawFilesBackend::loadManifest()
{
    QFile f(QDir(m_rootPath).filePath(QLatin1String(kManifestName)));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject collections = doc.object().value(QStringLiteral("collections")).toObject();
    for (auto it = collections.begin(); it != collections.end(); ++it) {
        const QJsonObject obj = it.value().toObject();
        CollectionInfo ci;
        ci.id = it.key();
        ci.name = obj.value(QStringLiteral("name")).toString();
        ci.type = obj.value(QStringLiteral("type")).toString();
        m_collections[ci.id] = ci;
    }
}

void RawFilesBackend::saveManifest() const
{
    QJsonObject collectionsObj;
    for (auto it = m_collections.begin(); it != m_collections.end(); ++it) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = it.value().name;
        obj[QStringLiteral("type")] = it.value().type;
        collectionsObj[it.key()] = obj;
    }
    QJsonObject root;
    root[QStringLiteral("collections")] = collectionsObj;
    QFile f(QDir(m_rootPath).filePath(QLatin1String(kManifestName)));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// static
QString RawFilesBackend::sanitize(const QString &id)
{
    QString out = id;
    for (QChar &c : out) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') ||
            c == QLatin1Char(':') || c == QLatin1Char('*') ||
            c == QLatin1Char('?') || c == QLatin1Char('"') ||
            c == QLatin1Char('<') || c == QLatin1Char('>') ||
            c == QLatin1Char('|'))
            c = QLatin1Char('_');
    }
    if (out.isEmpty())
        out = QStringLiteral("record");
    return out;
}

// static: for collectionId of the form "domain+encoding", returns "encoding.domain"
// (following the design doc's <encoding>.<domain> file suffix convention)
QString RawFilesBackend::suffixFor(const QString &collectionId)
{
    // Collection IDs are Shape::toString() = "<domain>+<encoding>" or just the
    // raw collectionId if not shape-keyed. Split on '+'.
    const int plus = collectionId.indexOf(QLatin1Char('+'));
    if (plus < 0)
        return collectionId;
    const QString domain = collectionId.left(plus);
    const QString encoding = collectionId.mid(plus + 1);
    return encoding + QLatin1Char('.') + domain;
}

} // namespace Kalburator::Sinks
