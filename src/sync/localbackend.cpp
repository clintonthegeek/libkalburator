#include "localbackend.h"
#include "calendarmetadatamanager.h"
#include "asyncfilewriter.h"
#include "backendcapabilities.h"
#include "logicalcalendar.h"
#include "discoveredcalendar.h"
#include <KCalendarCore/ICalFormat>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QSaveFile>
#include <QTimer>
#include <QCoreApplication>

const QString LocalBackend::BackendTypeName = QStringLiteral("local");

QString LocalBackend::backendType() const { return BackendTypeName; }

LocalBackend::LocalBackend(const QString &calendarRootPath, QObject *parent)
    : SyncBackend(parent)
    , m_calendarRootPath(calendarRootPath)
{
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

void LocalBackend::loadItems(KCalendarCore::MemoryCalendar* cal, bool suppressSignals)
{
    if (!cal) {
        qWarning() << "LocalBackend::loadItems: Null calendar provided";
        if (!suppressSignals) emit calendarLoaded(cal);
        return;
    }

    const QString calId = cal->id();
    if (calId.isEmpty()) {
        qWarning() << "LocalBackend::loadItems: Calendar has empty ID";
        if (!suppressSignals) emit calendarLoaded(cal);
        emit fetchFinished(calId, false, QStringLiteral("Empty calendar ID"));
        return;
    }

    if (m_calendarRootPath.isEmpty()) {
        qWarning() << "LocalBackend::loadItems: collection root path is empty";
        if (!suppressSignals) emit calendarLoaded(cal);
        emit fetchFinished(calId, false, QStringLiteral("Empty root path"));
        return;
    }

    QDir calDir(m_calendarRootPath + "/" + calId);
    if (!calDir.exists()) {
        qWarning() << "LocalBackend::loadItems: Calendar directory does not exist:" << calDir.path();
        if (!suppressSignals) emit calendarLoaded(cal);
        emit fetchFinished(calId, false, QStringLiteral("Directory does not exist"));
        return;
    }

    // Clear existing incidences
    auto existingIncidences = cal->incidences();
    for (const auto &inc : existingIncidences) {
        cal->deleteIncidence(inc);
    }

    const QStringList files = calDir.entryList(QStringList() << "*.ics", QDir::Files | QDir::NoSymLinks);
    const int totalFiles = files.size();

    // Emit fetchStarted with the count of files we're about to load
    emit fetchStarted(calId, totalFiles);

    KCalendarCore::ICalFormat icalFormat;
    int loadedCount = 0;
    int currentFile = 0;

    for (const QString &fileName : files) {
        QString filePath = calDir.filePath(fileName);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "LocalBackend::loadItems: Failed to open" << filePath << ":" << file.errorString();
            currentFile++;
            emit fetchProgressChanged(calId, currentFile, totalFiles);
            continue;
        }
        QByteArray data = file.readAll();
        file.close();

        auto *tempCalRaw = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        QSharedPointer<KCalendarCore::Calendar> tempCalPtr(tempCalRaw, [](KCalendarCore::Calendar*) {});

        if (!icalFormat.fromRawString(tempCalPtr, data)) {
            qWarning() << "LocalBackend::loadItems: Failed to parse .ics file:" << filePath;
            delete tempCalRaw;
            currentFile++;
            emit fetchProgressChanged(calId, currentFile, totalFiles);
            continue;
        }

        for (const auto &inc : tempCalRaw->incidences()) {
            cal->addIncidence(inc);
            loadedCount++;
            if (!suppressSignals) {
                emit itemLoaded(cal, inc, QString());
                emit itemFetched(calId, inc);
            }
        }

        delete tempCalRaw;
        currentFile++;
        emit fetchProgressChanged(calId, currentFile, totalFiles);
        // Note: processEvents() removed - sync runs in worker thread
    }

    // Build hierarchy by setting parent-child links in the calendar model if needed
    buildHierarchy(cal);

    if (!suppressSignals) emit calendarLoaded(cal);
    emit fetchFinished(calId, true);
}

void LocalBackend::buildHierarchy(KCalendarCore::MemoryCalendar* cal)
{
    // This function can be used to process incidences and ensure parent-child links
    // are recognized in your model if you maintain explicit pointers.
    // KCalendarCore::MemoryCalendar and Incidence do not maintain explicit parent pointers,
    // but your model can use RELATED-TO to build the tree.
    Q_UNUSED(cal);
    // No action needed here if your model builds tree from RELATED-TO.
}

void LocalBackend::storeCalendars(const QString &someArg, const QList<KCalendarCore::MemoryCalendar*> &calendars) {
    // Your implementation here
}

void LocalBackend::storeItems(KCalendarCore::MemoryCalendar* cal,
                              const QList<KCalendarCore::Incidence::Ptr> &items)
{
    if (!cal) {
        qWarning() << "LocalBackend::storeItems: Null calendar provided";
        return;
    }

    const QString calId = cal->id();
    if (calId.isEmpty()) {
        qWarning() << "LocalBackend::storeItems: Calendar has empty ID";
        return;
    }

    if (m_calendarRootPath.isEmpty()) {
        qWarning() << "LocalBackend::storeItems: Collection root path is empty";
        return;
    }

    QDir calDir(m_calendarRootPath + "/" + calId);

    if (!calDir.exists()) {
        if (!calDir.mkpath(".")) {
            qWarning() << "LocalBackend::storeItems: Failed to create calendar directory" << calDir.path();
            return;
        }
    }

    KCalendarCore::ICalFormat icalFormat;
    const int totalItems = items.size();
    int currentItem = 0;

    emit writeStarted(calId, totalItems);

    for (const KCalendarCore::Incidence::Ptr &incidence : items) {
        if (incidence.isNull()) {
            qWarning() << "LocalBackend::storeItems: Null incidence skipped";
            currentItem++;
            continue;
        }

        QString uid = incidence->uid();
        if (uid.isEmpty()) {
            qWarning() << "LocalBackend::storeItems: Incidence with empty UID skipped";
            currentItem++;
            continue;
        }

        QString fileName = calDir.filePath(uid + ".ics");

        qDebug() << "Writing incidence" << uid << "to file" << fileName;

        QSaveFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "LocalBackend::storeItems: Failed to open file for writing:" << fileName << file.errorString();
            currentItem++;
            continue;
        }

        // Serialize incidence with hierarchy info intact
        writeIncidenceWithHierarchy(cal, incidence);

        // Use ICalFormat to serialize incidence wrapped in a temporary calendar
        auto tempCalRaw = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        tempCalRaw->addIncidence(incidence);
        QSharedPointer<KCalendarCore::Calendar> tempCalPtr(tempCalRaw, [](KCalendarCore::Calendar*) {});

        QString icalData = icalFormat.toString(tempCalPtr);
        QByteArray icalBytes = icalData.toUtf8();

        if (file.write(icalBytes) == -1) {
            qWarning() << "LocalBackend::storeItems: Failed to write incidence to file:" << fileName;
            file.cancelWriting();
            delete tempCalRaw;
            currentItem++;
            continue;
        }

        if (!file.commit()) {
            qWarning() << "LocalBackend::storeItems: Failed to commit saved file:" << fileName << file.errorString();
            delete tempCalRaw;
            currentItem++;
            continue;
        }

        delete tempCalRaw;
        currentItem++;

        // Emit write progress
        emit writeProgressChanged(calId, currentItem, totalItems);
        // Note: processEvents() removed - sync runs in worker thread
    }

    emit writeFinished(calId, true);
    qDebug() << "LocalBackend::storeItems: Saved" << items.count() << "incidences into calendar" << calId;
}

void LocalBackend::writeIncidenceWithHierarchy(KCalendarCore::MemoryCalendar* cal, const KCalendarCore::Incidence::Ptr &incidence)
{
    // This is a placeholder for any processing needed before serialization,
    // e.g., ensuring RELATED-TO properties are set correctly for parent-child.

    Q_UNUSED(cal);
    Q_UNUSED(incidence);

    // Typically, the incidence already has RELATED-TO properties set by your commands or model.
    // If you maintain explicit parent pointers, update RELATED-TO here accordingly.
}

void LocalBackend::updateItem(KCalendarCore::MemoryCalendar* cal,
                              const KCalendarCore::Incidence::Ptr &item,
                              const QString &icalData)
{
    Q_UNUSED(icalData)

    if (!cal || item.isNull()) {
        qWarning() << "LocalBackend::updateItem: Invalid calendar or incidence";
        return;
    }

    const QString calId = cal->id();
    if (calId.isEmpty()) {
        qWarning() << "LocalBackend::updateItem: Empty calendar ID";
        return;
    }

    QDir calDir(m_calendarRootPath + "/" + calId);
    if (!calDir.exists()) {
        if (!calDir.mkpath(".")) {
            qWarning() << "LocalBackend::updateItem: Failed to create calendar directory" << calDir.path();
            return;
        }
    }

    QString fileName = calDir.filePath(item->uid() + ".ics");

    qDebug() << "Updating incidence" << item->uid() << "to file" << fileName;

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "LocalBackend::updateItem: Failed to open file for writing:" << fileName << file.errorString();
        return;
    }

    // Serialize incidence with hierarchy info intact
    writeIncidenceWithHierarchy(cal, item);

    KCalendarCore::ICalFormat icalFormat;
    auto tempCalRaw = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    tempCalRaw->addIncidence(item);
    QSharedPointer<KCalendarCore::Calendar> tempCalPtr(tempCalRaw, [](KCalendarCore::Calendar*) {});

    QString icalDataStr = icalFormat.toString(tempCalPtr);
    QByteArray dataBytes = icalDataStr.toUtf8();

    if (file.write(dataBytes) == -1) {
        qWarning() << "LocalBackend::updateItem: Failed to write to file:" << fileName;
        file.cancelWriting();
        delete tempCalRaw;
        return;
    }

    if (!file.commit()) {
        qWarning() << "LocalBackend::updateItem: Failed to commit file:" << fileName << file.errorString();
        delete tempCalRaw;
        return;
    }

    delete tempCalRaw;
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

    const QString calId = calendar->id();
    QDir calDir(m_calendarRootPath + "/" + calId);
    if (!calDir.exists()) {
        if (!calDir.mkpath(".")) {
            qWarning() << "LocalBackend::startSync: Failed to create calendar directory" << calDir.path();
            emit syncCompleted(collectionId);
            return;
        }
    }

    m_pendingSyncCollectionId = collectionId;
    m_pendingWriteCount = allWrites.size();

    emit writeStarted(calId, allWrites.size());

    for (const KCalendarCore::Incidence::Ptr &incidence : allWrites) {
        if (incidence.isNull()) {
            m_pendingWriteCount--;
            continue;
        }

        QString uid = incidence->uid();
        if (uid.isEmpty()) {
            qWarning() << "LocalBackend::startSync: Incidence with empty UID skipped";
            m_pendingWriteCount--;
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

    emit writeFinished(m_pendingSyncCollectionId, failCount == 0);
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
// Operation-Based API for SyncTransaction support
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
