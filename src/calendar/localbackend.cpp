#include "localbackend.h"
#include "calendarmetadatamanager.h"
#include "icalcodec.h"
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
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>
#include <QPointer>

namespace Kalburator::Sync {

// Forward declaration: defined in the anonymous namespace below fetchItems(),
// which needs it to build the recordsFromLastFetch memo without a second
// disk read.
namespace { BackendRecord recordFromBytes(const QByteArray &bytes, const QString &uid,
                                          const QDateTime &fileMtime); }

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
        : m_dbPath(dbPath)
        , m_backendId(backendId)
        , m_connectionName(QStringLiteral("FingerprintStore_%1_%2")
                               .arg(backendId)
                               .arg(reinterpret_cast<quintptr>(this)))
    {
        if (dbPath.isEmpty()) {
            qWarning() << "FingerprintStore: empty dbPath for backend" << backendId;
        }
    }

    ~FingerprintStore()
    {
        if (QSqlDatabase::contains(m_connectionName)) {
            QSqlDatabase::database(m_connectionName).close();
            QSqlDatabase::removeDatabase(m_connectionName);
        }
    }

    QString get(const QString &calendarId)
    {
        if (!ensureOpen()) return QString();
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
        if (!ensureOpen()) return false;
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
    // Opens the SQLite connection on first use — deferred past construction
    // so the connection's thread affinity is whichever thread first calls
    // get/set (the backend's own thread, post-D1-relocation), not whichever
    // thread happened to call setDbPath() (D1 T1.3, mirrors CTagStore T1.2).
    bool ensureOpen()
    {
        if (m_openAttempted) return m_open;
        m_openAttempted = true;

        if (m_dbPath.isEmpty()) return false;

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            qWarning() << "FingerprintStore: failed to open" << m_dbPath
                       << ":" << db.lastError().text();
            QSqlDatabase::removeDatabase(m_connectionName);
            return false;
        }
        m_open = ensureSchema();
        return m_open;
    }

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

    QString m_dbPath;
    QString m_backendId;
    QString m_connectionName;
    bool m_openAttempted = false;
    bool m_open = false;
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

// ---- Sync::ChangeDetection ----------------------------------------------

QString LocalBackend::collectionRevision(const QString &collectionId)
{
    return calendarFingerprint(collectionId);
}

QString LocalBackend::cachedCollectionRevision(const QString &collectionId) const
{
    return cachedFingerprint(collectionId);
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

    const QDir calDir(filePathForCalendar(calId));
    if (!calDir.exists()) {
        qWarning() << "LocalBackend::removeItem: Calendar directory does not exist" << calDir.path();
        return;
    }

    QString fileName = icsPathFor(calId, itemUid);
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

    // Apply deletions synchronously (fast operation)
    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        removeItem(calendar->id(), it.key());
    }

    // Combine creations and updates for async writing
    QList<KCalendarCore::Incidence::Ptr> allWrites;
    allWrites.append(stagedCreations);
    allWrites.append(stagedUpdates);

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

QString LocalBackend::icsPathFor(const QString &calendarId, const QString &uid) const
{
    return QDir(filePathForCalendar(calendarId)).filePath(uid + QStringLiteral(".ics"));
}

std::optional<QString> LocalBackend::recordPathFor(const QString &recordId) const
{
    if (recordId.isEmpty() || m_calendarRootPath.isEmpty()) return std::nullopt;

    const QDir rootDir(m_calendarRootPath);
    if (!rootDir.exists()) return std::nullopt;

    const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &calId : subdirs) {
        QString filePath = icsPathFor(calId, recordId);
        if (QFile::exists(filePath)) {
            return filePath;
        }
    }
    return std::nullopt;
}

// E9.2 (sync-excellence campaign, O34): shared hashing core. QMap sorts by
// key (filename) regardless of insertion order, so calendarFingerprint()'s
// full-rescan path and applyRecords()'s incremental patch path always hash
// entries in the same order — the incremental value is guaranteed
// bit-identical to what a full rescan of the same on-disk state would
// produce, as long as both funnel through this helper.
QString LocalBackend::hashFingerprintEntries(
    const QMap<QString, QPair<qint64, qint64>> &entries)
{
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        hasher.addData(it.key().toUtf8());
        hasher.addData(QByteArrayView("|"));
        hasher.addData(QByteArray::number(it.value().first));
        hasher.addData(QByteArrayView("|"));
        hasher.addData(QByteArray::number(it.value().second));
        hasher.addData(QByteArrayView("\n"));
    }
    return QString::fromLatin1(hasher.result().toHex());
}

QString LocalBackend::calendarFingerprint(const QString &calendarId) const
{
    const QString calendarPath = filePathForCalendar(calendarId);
    QDir dir(calendarPath);
    if (!dir.exists()) return {};

    const QStringList entries = dir.entryList(QStringList() << QStringLiteral("*.ics"),
                                               QDir::Files, QDir::NoSort);
    QMap<QString, QPair<qint64, qint64>> snapshot;
    for (const QString &name : entries) {
        const QFileInfo fi(dir.filePath(name));
        snapshot.insert(name, qMakePair(fi.lastModified().toMSecsSinceEpoch(), fi.size()));
    }
    return hashFingerprintEntries(snapshot);
}

// ============================================================================
// VDirSyncer-compatible calendar metadata methods
// ============================================================================

std::optional<QString> LocalBackend::metadataDirFor(const QString &calendarId) const
{
    if (calendarId.isEmpty() || m_calendarRootPath.isEmpty()) {
        return std::nullopt;
    }
    return filePathForCalendar(calendarId);
}

QColor LocalBackend::calendarColor(const QString &calendarId) const
{
    const auto dir = metadataDirFor(calendarId);
    return dir ? CalendarMetadataManager(*dir).color() : QColor();
}

bool LocalBackend::setCalendarColor(const QString &calendarId, const QColor &color)
{
    const auto dir = metadataDirFor(calendarId);
    return dir && CalendarMetadataManager(*dir).setColor(color);
}

QString LocalBackend::calendarDisplayName(const QString &calendarId) const
{
    const auto dir = metadataDirFor(calendarId);
    return dir ? CalendarMetadataManager(*dir).displayName() : QString();
}

bool LocalBackend::setCalendarDisplayName(const QString &calendarId, const QString &name)
{
    const auto dir = metadataDirFor(calendarId);
    return dir && CalendarMetadataManager(*dir).setDisplayName(name);
}

QString LocalBackend::calendarDescription(const QString &calendarId) const
{
    const auto dir = metadataDirFor(calendarId);
    return dir ? CalendarMetadataManager(*dir).description() : QString();
}

bool LocalBackend::setCalendarDescription(const QString &calendarId, const QString &description)
{
    const auto dir = metadataDirFor(calendarId);
    return dir && CalendarMetadataManager(*dir).setDescription(description);
}

int LocalBackend::calendarOrder(const QString &calendarId) const
{
    const auto dir = metadataDirFor(calendarId);
    return dir ? CalendarMetadataManager(*dir).order() : 0;
}

bool LocalBackend::setCalendarOrder(const QString &calendarId, int order)
{
    const auto dir = metadataDirFor(calendarId);
    return dir && CalendarMetadataManager(*dir).setOrder(order);
}

// ============================================================================
// Operation-based push/delete API (PushOperation / DeleteOperation)
// ============================================================================

namespace {

/// Parallel-sync Task 4: how many records one batch processes before the
/// operation yields to the event loop. Small enough that a yield happens
/// promptly on any realistic collection; large enough that the per-turn
/// overhead stays negligible.
constexpr int kChunkSize = 64;

} // namespace

// Parallel-sync Task 4: drive @p total items in kChunkSize batches, calling
// @p processOne for each index and @p onDone after the last one, yielding to
// the event loop between batches. Aborts silently if @p op is destroyed or
// reaches a terminal state (e.g. cancelled) between batches — the caller's
// onDone is then never invoked, which is the correct cancellation shape.
void LocalBackend::runChunked(SyncOperation *op,
                              int total,
                              const std::function<void(int)> &processOne,
                              const std::function<void()> &onDone)
{
    auto step = std::make_shared<std::function<void(int)>>();
    QPointer<SyncOperation> guard(op);
    *step = [this, guard, total, processOne, onDone, step](int from) {
        if (guard.isNull() || guard->isFinished())
            return;
        const int to = qMin(from + kChunkSize, total);
        for (int i = from; i < to; ++i)
            processOne(i);
        if (to >= total) {
            onDone();
            return;
        }
        QTimer::singleShot(0, this, [step, to]() { (*step)(to); });
    };
    (*step)(0);
}

FetchOperation* LocalBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);

    // E5.1: serialize per-collection via SyncBackendBase's FIFO queue
    // (enqueueOperation already defers this functor to the next event-loop
    // turn, same guarantee the old direct QTimer::singleShot(0, ...) gave).
    enqueueOperation(calendarId, op, [this, op, calendarId]() {
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

        const QStringList files = calDir.entryList(QStringList() << "*.ics", QDir::Files | QDir::NoSymLinks);
        const int totalFiles = files.size();

        // Emit fetchStarted with the count of files we're about to load
        emit fetchStarted(calendarId, totalFiles);

        if (totalFiles == 0) {
            op->setFetchedItems({});
            m_lastFetchRecords[calendarId] = {};
            // E9.2: an empty collection has an empty fingerprint snapshot —
            // still a valid (non-absent) base for the next applyRecords().
            m_lastFetchFingerprintSnapshot[calendarId] = {};
            op->complete();
            emit fetchFinished(calendarId, true);
            return;
        }

        // Parallel-sync Task 4: the whole pass used to run synchronously in
        // this one functor call; it now runs through runChunked() in
        // kChunkSize batches with an event-loop turn between them, so these
        // accumulators must be heap-owned — this stack frame is gone by the
        // time batch 2 runs. calDir/files are captured by value into the
        // per-batch closure for the same reason (QDir copies cheaply).
        auto items = std::make_shared<QList<KCalendarCore::Incidence::Ptr>>();
        auto records = std::make_shared<QList<BackendRecord>>();
        // E9.2 (sync-excellence campaign, O34): snapshot every *.ics file's
        // (mtimeMs, size) as we see it in this pass — the base that
        // applyRecords() will later patch in place with only the files it
        // itself wrote/deleted. Captured for every file regardless of
        // read/parse success, matching calendarFingerprint()'s full-rescan
        // semantics (which also stats every *.ics file unconditionally).
        auto fpSnapshot = std::make_shared<QMap<QString, QPair<qint64, qint64>>>();
        auto currentFile = std::make_shared<int>(0);

        runChunked(op, totalFiles,
            [this, calDir, files, calendarId, totalFiles, items, records, fpSnapshot, currentFile](int i) {
                // Body of the former per-file loop, verbatim, minus the
                // per-item cancellation check: runChunked's between-batch
                // guard now owns cancellation (a batch itself always runs
                // to completion, matching kChunkSize being deliberately
                // small enough that this is cheap).
                const QString &fileName = files.at(i);
                QString filePath = calDir.filePath(fileName);
                {
                    const QFileInfo fi(filePath);
                    fpSnapshot->insert(fileName, qMakePair(fi.lastModified().toMSecsSinceEpoch(),
                                                            fi.size()));
                }
                QFile file(filePath);
                // H5/O23: no QIODevice::Text here — it strips '\r' on read, which
                // would make the recordsFromLastFetch memo's bytes (and hence
                // contentHash) diverge from recordFromFile()'s raw read of the
                // same file (loadRecords()'s path), silently manufacturing a
                // spurious per-cycle "target changed" baseline mismatch.
                if (!file.open(QIODevice::ReadOnly)) {
                    qWarning() << "LocalBackend::fetchItems: Failed to open" << filePath;
                    (*currentFile)++;
                    emit fetchProgressChanged(calendarId, *currentFile, totalFiles);
                    return;
                }
                QByteArray data = file.readAll();
                file.close();

                const auto incidences = incidencesFromIcal(data);
                if (incidences.isEmpty()) {
                    qWarning() << "LocalBackend::fetchItems: Failed to parse" << filePath;
                    (*currentFile)++;
                    emit fetchProgressChanged(calendarId, *currentFile, totalFiles);
                    return;
                }

                for (const auto &inc : incidences) {
                    items->append(inc);
                }

                // recordsFromLastFetch memo (H5/O23): one BackendRecord per file,
                // built from the bytes already in hand (no second disk read).
                records->append(recordFromBytes(data, fileName.chopped(4),
                                                QFileInfo(filePath).lastModified()));

                (*currentFile)++;
                emit fetchProgressChanged(calendarId, *currentFile, totalFiles);
                // Note: processEvents() removed - this lambda runs on the
                // backend's own thread, not the sync worker thread.
            },
            [this, op, calendarId, items, records, fpSnapshot]() {
                qDebug() << "LocalBackend::fetchItems: Fetched" << items->size()
                         << "incidences for calendar" << calendarId;

                // E9.1 (sync-excellence campaign, O34): batch signal, once per
                // fetch pass, with the full item list (syncbackend.h).
                emit itemsFetched(calendarId, *items);

                op->setFetchedItems(*items);
                m_lastFetchRecords[calendarId] = *records;
                // E9.2: this pass's snapshot becomes the base for the next
                // applyRecords() incremental patch.
                m_lastFetchFingerprintSnapshot[calendarId] = *fpSnapshot;
                op->complete();
                emit fetchFinished(calendarId, true);
            });
    });

    return op;
}

PushOperation* LocalBackend::pushItems(const QString &calendarId,
                                        const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);

    enqueueOperation(calendarId, op, [this, op, calendarId, items]() {
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

        // Parallel-sync Task 4: heap-owned so they survive past this
        // functor's stack frame — see fetchItems() above for the same
        // pattern and rationale.
        auto succeededUids = std::make_shared<QStringList>();
        auto failedUids = std::make_shared<QStringList>();
        const int totalItems = static_cast<int>(items.size());

        runChunked(op, totalItems,
            [calDir, items, succeededUids, failedUids](int i) {
                // Body of the former per-item loop, verbatim.
                const auto &item = items.at(i);
                if (item.isNull()) {
                    return;
                }

                QString fileName = calDir.filePath(item->uid() + ".ics");
                QSaveFile file(fileName);

                if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    qWarning() << "LocalBackend::pushItems: Failed to open" << fileName;
                    failedUids->append(item->uid());
                    return;
                }

                if (file.write(icalFromIncidence(item)) == -1) {
                    file.cancelWriting();
                    failedUids->append(item->uid());
                    return;
                }

                if (!file.commit()) {
                    failedUids->append(item->uid());
                    return;
                }

                succeededUids->append(item->uid());
            },
            [op, succeededUids, failedUids]() {
                op->setSucceededUids(*succeededUids);
                op->setFailedUids(*failedUids);

                if (!failedUids->isEmpty() && succeededUids->isEmpty()) {
                    op->fail(QStringLiteral("All items failed to push"));
                } else {
                    op->complete();
                }
            });
    });

    return op;
}


DeleteOperation* LocalBackend::deleteItems(const QString &calendarId,
                                            const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);

    enqueueOperation(calendarId, op, [this, op, calendarId, uids]() {
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

        // Parallel-sync Task 4: heap-owned so they survive past this
        // functor's stack frame — see fetchItems() above for the same
        // pattern and rationale.
        auto succeededUids = std::make_shared<QStringList>();
        auto failedUids = std::make_shared<QStringList>();
        const int totalUids = static_cast<int>(uids.size());

        runChunked(op, totalUids,
            [calDir, uids, succeededUids, failedUids](int i) {
                // Body of the former per-uid loop, verbatim.
                const QString &uid = uids.at(i);
                QString fileName = calDir.filePath(uid + ".ics");
                QFile file(fileName);

                if (!file.exists()) {
                    // Consider non-existent as success (already deleted)
                    succeededUids->append(uid);
                    return;
                }

                if (file.remove()) {
                    succeededUids->append(uid);
                } else {
                    qWarning() << "LocalBackend::deleteItems: Failed to delete" << fileName;
                    failedUids->append(uid);
                }
            },
            [op, succeededUids, failedUids]() {
                op->setSucceededUids(*succeededUids);
                op->setFailedUids(*failedUids);

                if (!failedUids->isEmpty() && succeededUids->isEmpty()) {
                    op->fail(QStringLiteral("All items failed to delete"));
                } else {
                    op->complete();
                }
            });
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

    QString filePath = icsPathFor(calendarId, uid);
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

    QString filePath = icsPathFor(calendarId, uid);
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

// Build a BackendRecord from already-read file bytes, avoiding a second
// disk read when the caller (fetchItems) already has the bytes in hand.
BackendRecord recordFromBytes(
    const QByteArray &bytes,
    const QString &uid,
    const QDateTime &fileMtime)
{
    const QByteArray hashBytes = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);

    const QDateTime icalLastMod = extractICalLastModified(bytes);
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
    return recordFromBytes(bytes, uid, QFileInfo(filePath).lastModified());
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
    const auto path = recordPathFor(recordId);
    if (!path) return std::nullopt;
    return recordFromFile(*path, recordId);
}

bool LocalBackend::recordsFromLastFetch(const QString &collectionId,
                                        QList<BackendRecord> &records,
                                        QString &errorMessage)
{
    auto it = m_lastFetchRecords.find(collectionId);
    if (it == m_lastFetchRecords.end()) {
        return SyncBackendBase::recordsFromLastFetch(collectionId, records, errorMessage);
    }
    records = it.value();
    m_lastFetchRecords.erase(it); // single-shot
    errorMessage.clear();
    return true;
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
    // Find the calendar directory that owns this uid
    const auto path = recordPathFor(record.id);
    if (!path) {
        qWarning() << "LocalBackend: updateRecord: uid not found:" << record.id;
        return false;
    }

    QSaveFile f(*path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "LocalBackend: updateRecord: cannot open" << *path;
        return false;
    }
    if (f.write(record.data) == -1 || !f.commit()) {
        qWarning() << "LocalBackend: updateRecord: write failed for" << *path;
        return false;
    }
    return true;
}

bool LocalBackend::deleteRecord(const QString &recordId)
{
    const auto path = recordPathFor(recordId);
    return path && QFile::remove(*path);
}

// E9.2 (sync-excellence campaign, O34): layers an incremental expected-
// fingerprint computation on top of SyncBackendBase's default synchronous
// create/update/delete dispatch (reused unchanged — same order, same
// per-record success/failure semantics, same MockBackend-style failure-
// injection compatibility). Removes the accepted one-cycle re-diff lag
// after self-writes, the SOUND way (audit A2): only the files THIS call
// actually wrote/deleted are re-stat'd and patched into the fetch-time
// snapshot — no full re-scan, and any OTHER (foreign) file's change is
// simply absent from the patch, so it still differs from a full rescan
// next cycle and correctly defeats a skip.
WriteOperation* LocalBackend::applyRecords(const QString &collectionId,
                                           const WriterBatch &batch)
{
    WriteOperation *op = SyncBackendBase::applyRecords(collectionId, batch);
    if (!op) return op;

    // No prior fetch-time snapshot for this collection (e.g. a write
    // without a preceding fetchItems() in this backend instance's
    // lifetime) — never guess; leave resultRevision() empty (the default).
    auto snapIt = m_lastFetchFingerprintSnapshot.find(collectionId);
    if (snapIt == m_lastFetchFingerprintSnapshot.end())
        return op;

    QMap<QString, QPair<qint64, qint64>> snapshot = snapIt.value();
    const QSet<QString> succeeded(op->succeededUids().cbegin(), op->succeededUids().cend());

    // Stat exactly the files this call wrote or deleted — never re-list
    // the directory.
    auto patchWritten = [this, &collectionId, &snapshot](const QString &uid) {
        const QString fileName = uid + QStringLiteral(".ics");
        const QFileInfo fi(icsPathFor(collectionId, uid));
        if (fi.exists()) {
            snapshot.insert(fileName, qMakePair(fi.lastModified().toMSecsSinceEpoch(), fi.size()));
        } else {
            // Deleted (or a create/update that somehow left no file behind).
            snapshot.remove(fileName);
        }
    };

    for (const auto &r : batch.creates) if (succeeded.contains(r.id)) patchWritten(r.id);
    for (const auto &r : batch.updates) if (succeeded.contains(r.id)) patchWritten(r.id);
    for (const auto &id : batch.deletes) if (succeeded.contains(id)) patchWritten(id);

    m_lastFetchFingerprintSnapshot[collectionId] = snapshot;
    const QString revision = hashFingerprintEntries(snapshot);
    op->setResultRevision(revision);
    // Keep the persisted change-detection cache (modifiedSince()'s
    // short-circuit) consistent with the incremental value too.
    if (m_fingerprints) m_fingerprints->set(collectionId, revision);

    return op;
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
