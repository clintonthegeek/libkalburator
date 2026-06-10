#include "localbackend.h"
#include "calendarmetadatamanager.h"
#include "asyncfilewriter.h"
#include "backendcapabilities.h"
#include "logicalcalendar.h"
#include "discoveredcalendar.h"
#include <KCalendarCore/ICalFormat>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

namespace Kalburator::Sync {

// ============================================================================
// FingerprintStore — private inner store for per-backend local directory fingerprints
//
// Persists to a `local_fingerprints` table in the .kalburator-sync.db file.
// BackendId is fixed at construction time so callers only pass calendarId.
// ============================================================================

class FingerprintStore
{
public:
    explicit FingerprintStore(const QString &dbPath, const QString &backendId)
        : m_backendId(backendId)
        , m_connectionName(QStringLiteral("FingerprintStore_%1_%2")
                               .arg(backendId)
                               .arg(reinterpret_cast<quintptr>(this)))
    {
        if (dbPath.isEmpty()) {
            qWarning() << "FingerprintStore: empty dbPath for backend" << backendId;
            return;
        }
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            qWarning() << "FingerprintStore: failed to open" << dbPath
                       << ":" << db.lastError().text();
            QSqlDatabase::removeDatabase(m_connectionName);
            m_connectionName.clear();
            return;
        }
        ensureSchema();
    }

    ~FingerprintStore()
    {
        if (!m_connectionName.isEmpty()) {
            if (QSqlDatabase::contains(m_connectionName)) {
                QSqlDatabase::database(m_connectionName).close();
                QSqlDatabase::removeDatabase(m_connectionName);
            }
        }
    }

    bool isValid() const { return !m_connectionName.isEmpty(); }

    QString get(const QString &calendarId) const
    {
        if (!isValid()) return QString();
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT fingerprint FROM local_fingerprints "
            "WHERE backend_id = ? AND calendar_id = ?"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        if (q.exec() && q.next())
            return q.value(0).toString();
        return QString();
    }

    bool set(const QString &calendarId, const QString &fingerprint)
    {
        if (!isValid()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO local_fingerprints "
            "(backend_id, calendar_id, fingerprint) VALUES (?, ?, ?)"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        q.addBindValue(fingerprint);
        if (!q.exec()) {
            qWarning() << "FingerprintStore::set failed:" << q.lastError().text();
            return false;
        }
        return true;
    }

private:
    bool ensureSchema()
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        bool ok = q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS local_fingerprints ("
            "  backend_id   TEXT NOT NULL,"
            "  calendar_id  TEXT NOT NULL,"
            "  fingerprint  TEXT NOT NULL,"
            "  PRIMARY KEY (backend_id, calendar_id)"
            ")"));
        if (!ok)
            qWarning() << "FingerprintStore::ensureSchema failed:" << q.lastError().text();
        return ok;
    }

    QString m_backendId;
    QString m_connectionName;
};

// (FingerprintStore class ends above; LocalBackend methods continue below in the same namespace)

const QString LocalBackend::BackendTypeName = QStringLiteral("local");

QString LocalBackend::backendType() const { return BackendTypeName; }

QList<Kalburator::Shape::Shape> LocalBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}

LocalBackend::LocalBackend(const QString &calendarRootPath, QObject *parent)
    : SyncBackend(parent)
    , m_calendarRootPath(calendarRootPath)
{
}

LocalBackend::~LocalBackend() = default;

void LocalBackend::setDbPath(const QString &dbPath)
{
    if (!dbPath.isEmpty() && !m_fingerprints) {
        m_fingerprints = std::make_unique<FingerprintStore>(dbPath, backendType());
    }
}

QString LocalBackend::cachedFingerprint(const QString &calendarId) const
{
    if (m_fingerprints)
        return m_fingerprints->get(calendarId);
    return QString();
}

void LocalBackend::setCachedFingerprint(const QString &calendarId, const QString &fingerprint)
{
    if (m_fingerprints)
        m_fingerprints->set(calendarId, fingerprint);
}

BackendCapabilities LocalBackend::capabilities() const
{
    return BackendCapabilities::localDefaults();
}

bool LocalBackend::discoveredWritable(const QString &calendarId) const
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return true;  // Default to writable if we can't check
    }

    QString calendarPath = filePathForCalendar(calendarId);
    QDir calendarDir(calendarPath);

    if (!calendarDir.exists()) {
        return true;  // Non-existent directory = we'd create it, so writable
    }

    // Check 1: Filesystem write permissions on the directory
    QFileInfo dirInfo(calendarPath);
    if (!dirInfo.isWritable()) {
        qDebug() << "LocalBackend: Calendar" << calendarId << "is read-only (no write permission)";
        return false;
    }

    // Check 2: Look for a "readonly" marker file (case-insensitive)
    const QStringList entries = calendarDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.compare(QStringLiteral("readonly"), Qt::CaseInsensitive) == 0) {
            qDebug() << "LocalBackend: Calendar" << calendarId << "is read-only (readonly marker file found)";
            return false;
        }
    }

    return true;
}

// ============================================================================
// Binding Metadata Support
// ============================================================================

QStringList LocalBackend::bindingMetadataKeys() const
{
    return {QStringLiteral("directory")};
}

void LocalBackend::populateBindingMetadata(
    const DiscoveredCalendar &discovered,
    CalendarBackendBinding &binding) const
{
    // For local backend, store the directory path in metadata
    QString dir = QDir(m_calendarRootPath).filePath(discovered.calendarId);
    binding.setMetadata(QStringLiteral("directory"), dir);
}

void LocalBackend::prepareCreationMetadata(
    const QString &calendarId,
    CalendarBackendBinding &binding) const
{
    // For local backend, store the directory path in metadata
    QString dir = QDir(m_calendarRootPath).filePath(calendarId);
    binding.setMetadata(QStringLiteral("directory"), dir);
}

void LocalBackend::setcalendarRootPath(const QString &path)
{
    m_calendarRootPath = path;
}

void LocalBackend::loadCalendars(const QString &collectionId)
{
    qDebug() << "LocalBackend::loadCalendars called with calendarRootPath:" << m_calendarRootPath;

    if (m_calendarRootPath.isEmpty()) {
        qWarning() << "LocalBackend: collection root path is empty";
        emit loadCalendarsFinished(collectionId, false, QStringLiteral("Collection root path is empty"));
        return;
    }

    QDir rootDir(m_calendarRootPath);
    if (!rootDir.exists()) {
        qWarning() << "LocalBackend: collection root directory does not exist:" << m_calendarRootPath;
        emit loadCalendarsFinished(collectionId, false, QStringLiteral("Directory does not exist: %1").arg(m_calendarRootPath));
        return;
    }

    const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &subdir : subdirs) {
        // Emit signal for any calendar directory, even if empty
        // This allows newly created calendars (which start with no .ics files) to be discovered
        emit calendarDiscovered(collectionId, subdir);
        qDebug() << "LocalBackend: calendarDiscovered emitted for" << subdir;
    }

    emit loadCalendarsFinished(collectionId, true);
}

void LocalBackend::storeCalendars(const QString &, const QList<KCalendarCore::MemoryCalendar*> &)
{
    // Deliberate no-op — no calendar-level save (mirrors RemoteCalendarBackend).
}

void LocalBackend::removeItem(const QString &calId, const QString &itemUid)
{
    if (calId.isEmpty()) {
        qWarning() << "LocalBackend::removeItem: Calendar ID is empty";
        return;
    }
    if (itemUid.isEmpty()) {
        qWarning() << "LocalBackend::removeItem: Incidence UID is empty";
        return;
    }
    if (m_calendarRootPath.isEmpty()) {
        qWarning() << "LocalBackend::removeItem: Collection root path is empty";
        return;
    }

    QDir calDir(m_calendarRootPath + "/" + calId);
    if (!calDir.exists()) {
        qWarning() << "LocalBackend::removeItem: Calendar directory does not exist" << calDir.path();
        return;
    }

    QString fileName = calDir.filePath(itemUid + ".ics");
    QFile file(fileName);

    if (!file.exists()) {
        qDebug() << "LocalBackend::removeItem: File does not exist, nothing to remove:" << fileName;
        return;
    }

    if (!file.remove()) {
        qWarning() << "LocalBackend::removeItem: Failed to remove file:" << fileName << file.errorString();
        return;
    }

    qDebug() << "LocalBackend::removeItem: Removed incidence uid" << itemUid << "from calendar" << calId;
}

bool LocalBackend::supportsCalendarCreation() const
{
    return true;
}

bool LocalBackend::createCalendar(const QString &collectionId, const QString &calendarId,
                                   const QString &name, CalendarType type)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(type);  // LocalBackend supports all types, no server-side restrictions

    if (calendarId.isEmpty()) {
        qWarning() << "LocalBackend::createCalendar: Calendar ID is empty";
        return false;
    }
    if (m_calendarRootPath.isEmpty()) {
        qWarning() << "LocalBackend::createCalendar: Calendar root path is empty";
        return false;
    }

    QString calendarPath = filePathForCalendar(calendarId);
    QDir calendarDir(calendarPath);

    if (calendarDir.exists()) {
        // Idempotent: return true if calendar already exists
        qDebug() << "LocalBackend::createCalendar: Calendar already exists:" << calendarPath;
        return true;
    }

    if (!calendarDir.mkpath(".")) {
        qWarning() << "LocalBackend::createCalendar: Failed to create calendar directory:" << calendarPath;
        return false;
    }

    // Write VDirSyncer-compatible displayname if provided
    if (!name.isEmpty()) {
        CalendarMetadataManager metadata(calendarPath);
        metadata.setDisplayName(name);
    }

    qDebug() << "LocalBackend::createCalendar: Successfully created calendar directory:" << calendarPath;
    return true;
}

bool LocalBackend::deleteCalendar(const QString &collectionId, const QString &calendarId)
{
    Q_UNUSED(collectionId);

    if (calendarId.isEmpty()) {
        qWarning() << "LocalBackend::deleteCalendar: Calendar ID is empty";
        return false;
    }
    if (m_calendarRootPath.isEmpty()) {
        qWarning() << "LocalBackend::deleteCalendar: Calendar root path is empty";
        return false;
    }

    QString calendarPath = filePathForCalendar(calendarId);
    QDir calendarDir(calendarPath);

    if (!calendarDir.exists()) {
        qDebug() << "LocalBackend::deleteCalendar: Calendar directory does not exist:" << calendarPath;
        return false;
    }

    if (!calendarDir.removeRecursively()) {
        qWarning() << "LocalBackend::deleteCalendar: Failed to remove calendar directory:" << calendarPath;
        return false;
    }

    qDebug() << "LocalBackend::deleteCalendar: Successfully deleted calendar directory:" << calendarPath;
    return true;
}

bool LocalBackend::updateCalendar(const QString &collectionId, const QString &calendarId,
                                   const QVariantMap &properties)
{
    Q_UNUSED(collectionId);

    if (calendarId.isEmpty()) {
        qWarning() << "LocalBackend::updateCalendar: Calendar ID is empty";
        return false;
    }

    bool success = true;

    // Update display name
    if (properties.contains(QStringLiteral("displayName"))) {
        QString name = properties.value(QStringLiteral("displayName")).toString();
        if (!setCalendarDisplayName(calendarId, name)) {
            qWarning() << "LocalBackend::updateCalendar: Failed to set display name";
            success = false;
        }
    }

    // Update color
    if (properties.contains(QStringLiteral("color"))) {
        QColor color = properties.value(QStringLiteral("color")).value<QColor>();
        if (!color.isValid()) {
            // Try to parse as string
            color = QColor(properties.value(QStringLiteral("color")).toString());
        }
        if (color.isValid()) {
            if (!setCalendarColor(calendarId, color)) {
                qWarning() << "LocalBackend::updateCalendar: Failed to set color";
                success = false;
            }
        }
    }

    // Update description
    if (properties.contains(QStringLiteral("description"))) {
        QString description = properties.value(QStringLiteral("description")).toString();
        if (!setCalendarDescription(calendarId, description)) {
            qWarning() << "LocalBackend::updateCalendar: Failed to set description";
            success = false;
        }
    }

    // Update display order
    if (properties.contains(QStringLiteral("displayOrder"))) {
        int order = properties.value(QStringLiteral("displayOrder")).toInt();
        if (!setCalendarOrder(calendarId, order)) {
            qWarning() << "LocalBackend::updateCalendar: Failed to set display order";
            success = false;
        }
    }

    if (success) {
        emit calendarUpdated(collectionId, calendarId);
    }

    return success;
}

void LocalBackend::startSync(const QString &collectionId,
                             KCalendarCore::MemoryCalendar* calendar,
                             const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                             const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                             const QMap<QString, QString> &stagedDeletions)
{
    if (!calendar) {
        qWarning() << "LocalBackend::startSync: Null calendar provided";
        emit syncCompleted(collectionId);
        return;
    }

    const QString calId = calendar->id();

    const QList<KCalendarCore::Incidence::Ptr> &finalCreations = stagedCreations;
    const QList<KCalendarCore::Incidence::Ptr> &finalUpdates = stagedUpdates;

    // Apply deletions synchronously (fast operation)
    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        removeItem(calendar->id(), it.key());
    }

    // Combine creations and updates for async writing
    QList<KCalendarCore::Incidence::Ptr> allWrites;
    allWrites.append(finalCreations);
    allWrites.append(finalUpdates);

    if (allWrites.isEmpty()) {
        emit syncCompleted(collectionId);
        return;
    }

    // Use async file writer for non-blocking writes
    ensureAsyncWriterReady();

    QDir calDir(m_calendarRootPath + "/" + calId);
    if (!calDir.exists()) {
        if (!calDir.mkpath(".")) {
            qWarning() << "LocalBackend::startSync: Failed to create calendar directory" << calDir.path();
            emit syncCompleted(collectionId);
            return;
        }
    }

    m_pendingSyncCollectionId = collectionId;

    emit writeStarted(calId, allWrites.size());

    for (const KCalendarCore::Incidence::Ptr &incidence : allWrites) {
        if (incidence.isNull()) {
            continue;
        }

        QString uid = incidence->uid();
        if (uid.isEmpty()) {
            qWarning() << "LocalBackend::startSync: Incidence with empty UID skipped";
            continue;
        }

        QString fileName = calDir.filePath(uid + ".ics");

        // Queue the incidence for serialization AND writing in worker thread
        // (Serialization now happens off the main thread too!)
        m_asyncWriter->queueIncidenceWrite(fileName, incidence, uid);
    }

    // Signal that no more writes will be queued
    m_asyncWriter->finishWrites();

    // Don't emit syncCompleted here - it will be emitted by onAsyncWritesFinished
}

void LocalBackend::ensureAsyncWriterReady()
{
    if (!m_asyncWriter) {
        m_asyncWriter = new AsyncFileWriter(this);
        connect(m_asyncWriter, &AsyncFileWriter::writeCompleted,
                this, &LocalBackend::onAsyncWriteCompleted);
        connect(m_asyncWriter, &AsyncFileWriter::allWritesCompleted,
                this, &LocalBackend::onAsyncWritesFinished);
        connect(m_asyncWriter, &AsyncFileWriter::progressChanged,
                this, &LocalBackend::onAsyncWriteProgress);
    }
    m_asyncWriter->start();
}

void LocalBackend::onAsyncWriteCompleted(const QString &filePath, const QString &identifier,
                                          bool success, const QString &errorMessage)
{
    Q_UNUSED(filePath)
    if (!success) {
        qWarning() << "LocalBackend: Async write failed for" << identifier << ":" << errorMessage;
    }
}

void LocalBackend::onAsyncWritesFinished(int successCount, int failCount)
{
    qDebug() << "LocalBackend::onAsyncWritesFinished: success=" << successCount << "fail=" << failCount;

    // Stop the worker thread (it will be restarted on next startSync)
    if (m_asyncWriter) {
        m_asyncWriter->stop();
    }

    emit syncCompleted(m_pendingSyncCollectionId);
    m_pendingSyncCollectionId.clear();
}

void LocalBackend::onAsyncWriteProgress(int completed, int total)
{
    emit writeProgressChanged(m_pendingSyncCollectionId, completed, total);
}

QString LocalBackend::filePathForCalendar(const QString &calendarId) const
{
    return QDir(m_calendarRootPath).filePath(calendarId);
}

QString LocalBackend::calendarFingerprint(const QString &calendarId) const
{
    const QString calendarPath = filePathForCalendar(calendarId);
    QDir dir(calendarPath);
    if (!dir.exists()) return {};

    const QStringList entries = dir.entryList(QStringList() << QStringLiteral("*.ics"),
                                               QDir::Files, QDir::Name);
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    for (const QString &name : entries) {
        const QFileInfo fi(dir.filePath(name));
        hasher.addData(name.toUtf8());
        hasher.addData(QByteArrayView("|"));
        hasher.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
        hasher.addData(QByteArrayView("|"));
        hasher.addData(QByteArray::number(fi.size()));
        hasher.addData(QByteArrayView("\n"));
    }
    return QString::fromLatin1(hasher.result().toHex());
}

// ============================================================================
// VDirSyncer-compatible calendar metadata methods
// ============================================================================

QColor LocalBackend::calendarColor(const QString &calendarId) const
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return QColor();
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.color();
}

bool LocalBackend::setCalendarColor(const QString &calendarId, const QColor &color)
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return false;
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.setColor(color);
}

QString LocalBackend::calendarDisplayName(const QString &calendarId) const
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return QString();
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.displayName();
}

bool LocalBackend::setCalendarDisplayName(const QString &calendarId, const QString &name)
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return false;
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.setDisplayName(name);
}

QString LocalBackend::calendarDescription(const QString &calendarId) const
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return QString();
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.description();
}

bool LocalBackend::setCalendarDescription(const QString &calendarId, const QString &description)
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return false;
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.setDescription(description);
}

int LocalBackend::calendarOrder(const QString &calendarId) const
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return 0;
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.order();
}

bool LocalBackend::setCalendarOrder(const QString &calendarId, int order)
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return false;
    }

    CalendarMetadataManager metadata(filePathForCalendar(calendarId));
    return metadata.setOrder(order);
}

// ============================================================================
// Operation-based push/delete API (PushOperation / DeleteOperation)
// ============================================================================

FetchOperation* LocalBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    registerOperation(op);

    // Use deferred execution so signals can be connected before firing
    QTimer::singleShot(0, this, [this, op, calendarId]() {
        if (op->state() == SyncOperation::Cancelled) {
            return;
        }

        op->setState(SyncOperation::Running);

        if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
            op->fail(QStringLiteral("Invalid calendar ID or root path"));
            emit fetchFinished(calendarId, false, QStringLiteral("Invalid calendar ID or root path"));
            return;
        }

        QDir calDir(m_calendarRootPath + "/" + calendarId);
        if (!calDir.exists()) {
            QString errorMsg = QStringLiteral("Calendar directory does not exist: %1").arg(calDir.path());
            op->fail(errorMsg);
            emit fetchFinished(calendarId, false, errorMsg);
            return;
        }

        QList<KCalendarCore::Incidence::Ptr> items;
        const QStringList files = calDir.entryList(QStringList() << "*.ics", QDir::Files | QDir::NoSymLinks);
        const int totalFiles = files.size();

        // Emit fetchStarted with the count of files we're about to load
        emit fetchStarted(calendarId, totalFiles);

        if (totalFiles == 0) {
            op->setFetchedItems({});
            op->complete();
            emit fetchFinished(calendarId, true);
            return;
        }

        KCalendarCore::ICalFormat icalFormat;
        int currentFile = 0;

        for (const QString &fileName : files) {
            if (op->state() == SyncOperation::Cancelled) {
                emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                return;
            }

            QString filePath = calDir.filePath(fileName);
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "LocalBackend::fetchItems: Failed to open" << filePath;
                currentFile++;
                emit fetchProgressChanged(calendarId, currentFile, totalFiles);
                continue;
            }
            QByteArray data = file.readAll();
            file.close();

            auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
                new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
            );

            if (!icalFormat.fromRawString(tempCal, data)) {
                qWarning() << "LocalBackend::fetchItems: Failed to parse" << filePath;
                currentFile++;
                emit fetchProgressChanged(calendarId, currentFile, totalFiles);
                continue;
            }

            // Emit itemFetched for EACH incidence as we parse it
            for (const auto &inc : tempCal->incidences()) {
                items.append(inc);
                emit itemFetched(calendarId, inc);
            }

            currentFile++;
            emit fetchProgressChanged(calendarId, currentFile, totalFiles);
            // Note: processEvents() removed - sync runs in worker thread
        }

        qDebug() << "LocalBackend::fetchItems: Fetched" << items.size()
                 << "incidences for calendar" << calendarId;

        op->setFetchedItems(items);
        op->complete();
        emit fetchFinished(calendarId, true);
    });

    return op;
}

PushOperation* LocalBackend::pushItems(const QString &calendarId,
                                        const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);

    // Use deferred execution
    QTimer::singleShot(0, this, [this, op, calendarId, items]() {
        if (op->state() == SyncOperation::Cancelled) {
            return;
        }

        op->setState(SyncOperation::Running);

        if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
            op->fail(QStringLiteral("Invalid calendar ID or root path"));
            return;
        }

        QDir calDir(m_calendarRootPath + "/" + calendarId);
        if (!calDir.exists()) {
            if (!calDir.mkpath(".")) {
                op->fail(QStringLiteral("Failed to create calendar directory: %1").arg(calDir.path()));
                return;
            }
        }

        QStringList succeededUids;
        QStringList failedUids;
        KCalendarCore::ICalFormat icalFormat;

        for (const auto &item : items) {
            if (item.isNull()) {
                continue;
            }

            QString fileName = calDir.filePath(item->uid() + ".ics");
            QSaveFile file(fileName);

            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qWarning() << "LocalBackend::pushItems: Failed to open" << fileName;
                failedUids.append(item->uid());
                continue;
            }

            auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
                new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
            );
            tempCal->addIncidence(item);
            QString icalData = icalFormat.toString(tempCal);

            if (file.write(icalData.toUtf8()) == -1) {
                file.cancelWriting();
                failedUids.append(item->uid());
                continue;
            }

            if (!file.commit()) {
                failedUids.append(item->uid());
                continue;
            }

            succeededUids.append(item->uid());
        }

        op->setSucceededUids(succeededUids);
        op->setFailedUids(failedUids);

        if (!failedUids.isEmpty() && succeededUids.isEmpty()) {
            op->fail(QStringLiteral("All items failed to push"));
        } else {
            op->complete();
        }
    });

    return op;
}


DeleteOperation* LocalBackend::deleteItems(const QString &calendarId,
                                            const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);
    registerOperation(op);

    // Use deferred execution
    QTimer::singleShot(0, this, [this, op, calendarId, uids]() {
        if (op->state() == SyncOperation::Cancelled) {
            return;
        }

        op->setState(SyncOperation::Running);

        if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
            op->fail(QStringLiteral("Invalid calendar ID or root path"));
            return;
        }

        QDir calDir(m_calendarRootPath + "/" + calendarId);
        if (!calDir.exists()) {
            op->fail(QStringLiteral("Calendar directory does not exist: %1").arg(calDir.path()));
            return;
        }

        QStringList succeededUids;
        QStringList failedUids;

        for (const QString &uid : uids) {
            QString fileName = calDir.filePath(uid + ".ics");
            QFile file(fileName);

            if (!file.exists()) {
                // Consider non-existent as success (already deleted)
                succeededUids.append(uid);
                continue;
            }

            if (file.remove()) {
                succeededUids.append(uid);
            } else {
                qWarning() << "LocalBackend::deleteItems: Failed to delete" << fileName;
                failedUids.append(uid);
            }
        }

        op->setSucceededUids(succeededUids);
        op->setFailedUids(failedUids);

        if (!failedUids.isEmpty() && succeededUids.isEmpty()) {
            op->fail(QStringLiteral("All items failed to delete"));
        } else {
            op->complete();
        }
    });

    return op;
}

// ============================================================================
// Debug/Raw ICS Access
// ============================================================================

QString LocalBackend::getRawIcs(const QString &calendarId, const QString &uid) const
{
    if (calendarId.isEmpty() || uid.isEmpty() || m_calendarRootPath.isEmpty()) {
        return QString();
    }

    QString filePath = m_calendarRootPath + "/" + calendarId + "/" + uid + ".ics";
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "LocalBackend::getRawIcs: Failed to open" << filePath;
        return QString();
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    return content;
}

bool LocalBackend::setRawIcs(const QString &calendarId, const QString &uid,
                              const QString &icsContent)
{
    if (calendarId.isEmpty() || uid.isEmpty() || m_calendarRootPath.isEmpty()) {
        return false;
    }

    QString filePath = m_calendarRootPath + "/" + calendarId + "/" + uid + ".ics";
    QSaveFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "LocalBackend::setRawIcs: Failed to open" << filePath;
        return false;
    }

    if (file.write(icsContent.toUtf8()) == -1) {
        file.cancelWriting();
        return false;
    }

    return file.commit();
}

// ============================================================================
// IBlobBackend implementation (Phase D Task 12)
//
// recordId  == uid == filename without ".ics"
// collectionId == calendarId (directory name under m_calendarRootPath)
// ============================================================================

namespace {

// Extract iCal LAST-MODIFIED datetime from raw iCal bytes.
// Returns an invalid QDateTime if the property is absent or unparseable.
// Prefers iCal LAST-MODIFIED over file mtime for LWW comparison so that
// tests (and real-world scenarios) where iCal metadata is authoritative
// behave correctly regardless of file system timestamp granularity.
static QDateTime extractICalLastModified(const QByteArray &data)
{
    static const QRegularExpression re(
        QStringLiteral("LAST-MODIFIED:(\\d{8}T\\d{6}Z)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(QString::fromUtf8(data));
    if (!match.hasMatch()) return {};
    QDateTime dt = QDateTime::fromString(match.captured(1),
                                         QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    if (dt.isValid())
        dt.setTimeZone(QTimeZone::utc());
    return dt;
}

// Build a BackendRecord from a file on disk.
// Returns a null-opt if the file cannot be opened.
static std::optional<Kalburator::Sync::BackendRecord> recordFromFile(
    const QString &filePath,
    const QString &uid)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "LocalBackend (blob): cannot open" << filePath;
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const QByteArray hashBytes = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);

    const QDateTime icalLastMod = extractICalLastModified(bytes);
    const QDateTime fileMtime   = QFileInfo(filePath).lastModified();
    // Pick the record's lastModified from the explicit iCal LAST-MODIFIED stamp
    // and the file mtime, preserving BOTH intents (PlanStan bug doc
    // sync-conflicts-lastwritewins-tie-bias.md, fix A2):
    // - An explicit iCal LAST-MODIFIED is authoritative: a calendar app's edit
    //   time must not be overridden just because the file was later re-saved /
    //   format-normalized / imported (which bumps mtime to "now"). The old
    //   max(ical, mtime) silently let mtime win for any past-dated stamp.
    // - iCal LAST-MODIFIED is only whole-second precise; when the file mtime is
    //   in the SAME second as the stamp, fold mtime's sub-second part in so
    //   rapid back-to-back writes within that second still order correctly.
    // - With no explicit stamp, the file mtime is all we have.
    QDateTime bestMod;
    if (!icalLastMod.isValid()) {
        bestMod = fileMtime;
    } else {
        bestMod = icalLastMod;
        if (fileMtime.isValid()
            && fileMtime.toSecsSinceEpoch() == icalLastMod.toSecsSinceEpoch()) {
            bestMod = icalLastMod.addMSecs(fileMtime.time().msec());
        }
    }

    Kalburator::Sync::BackendRecord rec;
    rec.id          = uid;
    rec.type        = QStringLiteral("calendar");
    rec.data        = bytes;
    rec.contentHash = QString::fromLatin1(hashBytes.toHex());
    rec.lastModified = bestMod;
    rec.isDeleted   = false;
    return rec;
}

} // anonymous namespace

// --- Identity ---------------------------------------------------------------

QString LocalBackend::backendId() const
{
    // Stable id: type + root path digest so different roots get different ids
    const QByteArray h = QCryptographicHash::hash(
        (BackendTypeName + QLatin1Char(':') + m_calendarRootPath).toUtf8(),
        QCryptographicHash::Sha256);
    return BackendTypeName + QLatin1Char(':') + QString::fromLatin1(h.toHex().left(16));
}

QString LocalBackend::displayName() const
{
    return QStringLiteral("LocalBackend(%1)").arg(m_calendarRootPath);
}

bool LocalBackend::isAvailable() const
{
    return !m_calendarRootPath.isEmpty() && QDir(m_calendarRootPath).exists();
}

// --- Collections ------------------------------------------------------------

QList<CollectionInfo> LocalBackend::availableCollections()
{
    QList<CollectionInfo> result;
    if (m_calendarRootPath.isEmpty()) return result;

    const QDir rootDir(m_calendarRootPath);
    if (!rootDir.exists()) return result;

    const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &calId : subdirs) {
        CollectionInfo info;
        info.id   = calId;
        info.name = calId;
        info.path = filePathForCalendar(calId);
        info.type = QStringLiteral("calendar");
        result.append(info);
    }
    return result;
}

CollectionInfo LocalBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.name = collectionId;
    info.path = filePathForCalendar(collectionId);
    info.type = QStringLiteral("calendar");
    return info;
}

QString LocalBackend::createCollection(const CollectionInfo &info)
{
    const QString path = filePathForCalendar(info.id);
    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            qWarning() << "LocalBackend: createCollection failed to create" << path;
            return {};
        }
    }
    return info.id;
}

// --- Records ----------------------------------------------------------------

QList<BackendRecord> LocalBackend::loadRecords(const QString &collectionId)
{
    QList<BackendRecord> result;
    if (collectionId.isEmpty() || m_calendarRootPath.isEmpty()) return result;

    const QDir calDir(filePathForCalendar(collectionId));
    if (!calDir.exists()) return result;

    const QStringList files = calDir.entryList(
        QStringList() << QStringLiteral("*.ics"),
        QDir::Files | QDir::NoSymLinks);

    for (const QString &fileName : files) {
        const QString uid = fileName.chopped(4); // remove ".ics"
        auto rec = recordFromFile(calDir.filePath(fileName), uid);
        if (rec.has_value()) {
            result.append(rec.value());
        }
    }
    return result;
}

std::optional<BackendRecord> LocalBackend::loadRecord(const QString &recordId)
{
    // recordId == uid; search all calendar sub-directories
    if (recordId.isEmpty() || m_calendarRootPath.isEmpty()) return std::nullopt;

    const QDir rootDir(m_calendarRootPath);
    if (!rootDir.exists()) return std::nullopt;

    const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &calId : subdirs) {
        const QString filePath = filePathForCalendar(calId) + QLatin1Char('/') + recordId + QStringLiteral(".ics");
        if (QFile::exists(filePath)) {
            return recordFromFile(filePath, recordId);
        }
    }
    return std::nullopt;
}

QString LocalBackend::createRecord(const QString &collectionId,
                                   const BackendRecord &record)
{
    if (collectionId.isEmpty() || record.id.isEmpty()) return {};

    const QString calPath = filePathForCalendar(collectionId);
    QDir calDir(calPath);
    if (!calDir.exists()) {
        if (!calDir.mkpath(QStringLiteral("."))) {
            qWarning() << "LocalBackend: createRecord: cannot create dir" << calPath;
            return {};
        }
    }

    const QString filePath = calDir.filePath(record.id + QStringLiteral(".ics"));
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "LocalBackend: createRecord: cannot open" << filePath;
        return {};
    }
    if (file.write(record.data) == -1 || !file.commit()) {
        qWarning() << "LocalBackend: createRecord: write failed for" << filePath;
        return {};
    }
    return record.id;
}

bool LocalBackend::updateRecord(const BackendRecord &record)
{
    if (record.id.isEmpty() || m_calendarRootPath.isEmpty()) return false;

    // Find the calendar directory that owns this uid
    const QDir rootDir(m_calendarRootPath);
    const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &calId : subdirs) {
        const QString filePath = filePathForCalendar(calId) + QLatin1Char('/')
                                 + record.id + QStringLiteral(".ics");
        if (QFile::exists(filePath)) {
            QSaveFile f(filePath);
            if (!f.open(QIODevice::WriteOnly)) {
                qWarning() << "LocalBackend: updateRecord: cannot open" << filePath;
                return false;
            }
            if (f.write(record.data) == -1 || !f.commit()) {
                qWarning() << "LocalBackend: updateRecord: write failed for" << filePath;
                return false;
            }
            return true;
        }
    }
    qWarning() << "LocalBackend: updateRecord: uid not found:" << record.id;
    return false;
}

bool LocalBackend::deleteRecord(const QString &recordId)
{
    if (recordId.isEmpty() || m_calendarRootPath.isEmpty()) return false;

    const QDir rootDir(m_calendarRootPath);
    const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &calId : subdirs) {
        const QString filePath = filePathForCalendar(calId) + QLatin1Char('/')
                                 + recordId + QStringLiteral(".ics");
        if (QFile::exists(filePath)) {
            return QFile::remove(filePath);
        }
    }
    return false;
}

// --- Change detection -------------------------------------------------------

QList<BackendRecord> LocalBackend::modifiedSince(const QString &collectionId,
                                                   const QDateTime &since)
{
    QList<BackendRecord> result;
    if (collectionId.isEmpty() || m_calendarRootPath.isEmpty()) return result;

    const QString calPath = filePathForCalendar(collectionId);
    const QDir calDir(calPath);
    if (!calDir.exists()) return result;

    // Short-circuit: compare current directory fingerprint against cached value.
    // If they match, nothing has changed — return empty.
    if (m_fingerprints) {
        const QString currentFp = calendarFingerprint(collectionId);
        const QString cachedFp  = m_fingerprints->get(collectionId);
        if (!currentFp.isEmpty() && currentFp == cachedFp) {
            return {};  // nothing changed
        }
    }

    // Walk the directory; collect files with mtime > since
    const QStringList files = calDir.entryList(
        QStringList() << QStringLiteral("*.ics"),
        QDir::Files | QDir::NoSymLinks);

    for (const QString &fileName : files) {
        const QString filePath = calDir.filePath(fileName);
        const QFileInfo fi(filePath);
        if (fi.lastModified() > since) {
            const QString uid = fileName.chopped(4);
            auto rec = recordFromFile(filePath, uid);
            if (rec.has_value()) {
                result.append(rec.value());
            }
        }
    }

    // Update fingerprint so next identical-state call short-circuits
    if (m_fingerprints) {
        const QString newFp = calendarFingerprint(collectionId);
        if (!newFp.isEmpty()) {
            m_fingerprints->set(collectionId, newFp);
        }
    }

    return result;
}

QStringList LocalBackend::deletedSince(const QString &collectionId,
                                        const QDateTime &since)
{
    // LocalBackend has no deletion log — file-based storage with no tombstones.
    Q_UNUSED(collectionId)
    Q_UNUSED(since)
    return {};
}


} // namespace Kalburator::Sync
