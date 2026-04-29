#include "localblobbackend.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Kalburator::Sync {

namespace {

QString slugify(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
        } else if (c == QLatin1Char('-') || c == QLatin1Char('_')
                   || c == QLatin1Char(' ')) {
            out.append(QLatin1Char('-'));
        }
    }
    if (out.isEmpty()) {
        out = QStringLiteral("record");
    }
    return out;
}

QString shortHash(const QString &s)
{
    const QByteArray hash = QCryptographicHash::hash(s.toUtf8(),
                                                     QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex()).left(8);
}

QString sha256Hex(const QByteArray &data)
{
    const QByteArray hash = QCryptographicHash::hash(data,
                                                     QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

} // namespace

LocalBlobBackend::LocalBlobBackend(const QString &basePath, QObject *parent)
    : QObject(parent)
    , m_basePath(basePath)
{
}

LocalBlobBackend::~LocalBlobBackend() = default;

bool LocalBlobBackend::isAvailable() const
{
    QFileInfo fi(m_basePath);
    return fi.exists() && fi.isDir() && fi.isWritable();
}

QString LocalBlobBackend::extensionForType(const QString &type) const
{
    if (type == QLatin1String("memos"))    return QStringLiteral(".md");
    if (type == QLatin1String("contacts")) return QStringLiteral(".vcf");
    if (type == QLatin1String("calendar")) return QStringLiteral(".ics");
    if (type == QLatin1String("todos"))    return QStringLiteral(".ics");
    return QStringLiteral(".bin");
}

QString LocalBlobBackend::filenameFor(const BackendRecord &record,
                                      const QString &type) const
{
    const QString base = slugify(record.displayName.isEmpty()
                                 ? record.id : record.displayName);
    return QStringLiteral("%1-%2%3")
        .arg(base, shortHash(record.id), extensionForType(type));
}

QString LocalBlobBackend::pathFromRecordId(const QString &recordId) const
{
    // Record id *is* the absolute path in this backend.
    return recordId;
}

QList<CollectionInfo> LocalBlobBackend::availableCollections()
{
    QList<CollectionInfo> out;
    if (!isAvailable()) {
        return out;
    }
    QDir dir(m_basePath);
    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                              QDir::Name);
    for (const QString &name : subdirs) {
        CollectionInfo info;
        info.id = name;
        info.name = name;
        info.path = dir.absoluteFilePath(name);
        info.type = m_collectionTypes.value(name);
        out.append(info);
    }
    return out;
}

CollectionInfo LocalBlobBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    if (collectionId.isEmpty()) return info;
    QDir dir(m_basePath);
    if (!dir.exists(collectionId)) return info;
    info.id = collectionId;
    info.name = collectionId;
    info.path = dir.absoluteFilePath(collectionId);
    info.type = m_collectionTypes.value(collectionId);
    return info;
}

QString LocalBlobBackend::createCollection(const CollectionInfo &info)
{
    if (info.id.isEmpty()) {
        return {};
    }
    QDir base(m_basePath);
    if (!base.mkpath(info.id)) {
        Q_EMIT errorOccurred(QStringLiteral("createCollection: mkpath failed for %1").arg(info.id));
        return {};
    }
    m_collectionTypes.insert(info.id, info.type);
    return info.id;
}

BackendRecord LocalBlobBackend::readFile(const QString &absolutePath,
                                         const QString &recordId) const
{
    BackendRecord r;
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return r;
    }
    r.data = f.readAll();
    r.id = recordId.isEmpty() ? absolutePath : recordId;
    QFileInfo fi(absolutePath);
    r.displayName = fi.completeBaseName();
    r.lastModified = fi.lastModified().toUTC();
    r.contentHash = sha256Hex(r.data);
    return r;
}

bool LocalBlobBackend::writeAtomic(const QString &absolutePath,
                                   const QByteArray &data)
{
    QSaveFile f(absolutePath);
    if (!f.open(QIODevice::WriteOnly)) {
        Q_EMIT errorOccurred(QStringLiteral("open failed: %1").arg(absolutePath));
        return false;
    }
    if (f.write(data) != data.size()) {
        Q_EMIT errorOccurred(QStringLiteral("short write: %1").arg(absolutePath));
        return false;
    }
    if (!f.commit()) {
        Q_EMIT errorOccurred(QStringLiteral("commit failed: %1").arg(absolutePath));
        return false;
    }
    return true;
}

QList<BackendRecord> LocalBlobBackend::loadRecords(const QString &collectionId)
{
    QList<BackendRecord> out;
    if (!isAvailable()) {
        return out;
    }
    const QString collDir = QDir(m_basePath).absoluteFilePath(collectionId);
    QDir dir(collDir);
    if (!dir.exists()) {
        return out;
    }
    const QStringList files = dir.entryList(QDir::Files, QDir::Name);
    for (const QString &name : files) {
        const QString abs = dir.absoluteFilePath(name);
        out.append(readFile(abs, abs));
    }
    return out;
}

std::optional<BackendRecord> LocalBlobBackend::loadRecord(const QString &recordId)
{
    const QString path = pathFromRecordId(recordId);
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        return std::nullopt;
    }
    return readFile(path, path);
}

QString LocalBlobBackend::createRecord(const QString &collectionId,
                                       const BackendRecord &record)
{
    if (!isAvailable() || collectionId.isEmpty() || record.id.isEmpty()) {
        return {};
    }
    const QString type = m_collectionTypes.value(collectionId);
    const QString collDir = QDir(m_basePath).absoluteFilePath(collectionId);
    QDir().mkpath(collDir);
    const QString absPath = QDir(collDir).absoluteFilePath(filenameFor(record, type));
    if (!writeAtomic(absPath, record.data)) {
        return {};
    }
    Q_EMIT recordCreated(absPath);
    return absPath;
}

bool LocalBlobBackend::updateRecord(const BackendRecord &record)
{
    const QString path = pathFromRecordId(record.id);
    if (!QFileInfo::exists(path)) {
        return false;
    }
    if (!writeAtomic(path, record.data)) {
        return false;
    }
    Q_EMIT recordUpdated(path);
    return true;
}

bool LocalBlobBackend::deleteRecord(const QString &recordId)
{
    const QString path = pathFromRecordId(recordId);
    if (!QFile::remove(path)) {
        return false;
    }
    Q_EMIT recordDeleted(path);
    return true;
}

QList<BackendRecord> LocalBlobBackend::modifiedSince(const QString &collectionId,
                                                     const QDateTime &since)
{
    QList<BackendRecord> out;
    const QString collDir = QDir(m_basePath).absoluteFilePath(collectionId);
    QDir dir(collDir);
    if (!dir.exists()) {
        return out;
    }
    const QStringList files = dir.entryList(QDir::Files, QDir::Name);
    for (const QString &name : files) {
        const QString abs = dir.absoluteFilePath(name);
        const QFileInfo fi(abs);
        if (!since.isValid() || fi.lastModified().toUTC() >= since) {
            out.append(readFile(abs, abs));
        }
    }
    return out;
}

QStringList LocalBlobBackend::deletedSince(const QString &collectionId,
                                           const QDateTime &since)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(since);
    return {}; // file-based backend can't track deletions
}

} // namespace Kalburator::Sync
