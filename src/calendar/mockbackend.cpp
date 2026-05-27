#include "mockbackend.h"
#include "syncoperation.h"

#include <QThread>
#include <QTimer>
#include <QCryptographicHash>
#include <KCalendarCore/ICalFormat>

namespace Kalburator::Sync {

const QString MockBackend::BackendTypeName = QStringLiteral("mock");

QString MockBackend::backendType() const { return BackendTypeName; }

QList<Kalburator::Shape::Shape> MockBackend::nativeShapes() const
{
    return { m_shape };
}

MockBackend::MockBackend(QObject *parent)
    : MockBackend(BackendTypeName, parent)
{
}

MockBackend::MockBackend(const QString &backendId, QObject *parent)
    : SyncBackend(parent)
    , m_backendId(backendId)
{
}

void MockBackend::loadCalendars(const QString &collectionId)
{
    logOperation(QStringLiteral("LOAD_CALENDARS"), collectionId);

    if (shouldFail(FailurePoint::OnLoadCalendars)) {
        QString errorMsg = m_failureMessage.isEmpty()
            ? QStringLiteral("Mock failure on loadCalendars")
            : m_failureMessage;
        emit calendarError(collectionId, QString(), errorMsg);
        emit loadCalendarsFinished(collectionId, false, errorMsg);
        return;
    }

    applyDelay();

    // Emit signal for each known calendar
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        emit calendarDiscovered(collectionId, it.key());
    }

    emit loadCalendarsFinished(collectionId, true);
}

void MockBackend::storeCalendars(const QString &collectionId,
                                  const QList<KCalendarCore::MemoryCalendar*> &calendars)
{
    logOperation(QStringLiteral("STORE_CALENDARS"), collectionId);
    applyDelay();

    for (auto *cal : calendars) {
        if (cal) {
            QString calId = cal->name();
            if (!m_calendars.contains(calId)) {
                m_calendars[calId] = QHash<QString, KCalendarCore::Incidence::Ptr>();
            }
        }
    }
}

void MockBackend::startSync(const QString &collectionId,
                             KCalendarCore::MemoryCalendar* calendar,
                             const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                             const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                             const QMap<QString, QString> &stagedDeletions)
{
    if (!calendar) {
        emit syncCompleted(collectionId);
        return;
    }

    // Use calendar->id() consistent with all other MockBackend methods.
    // (Previously used calendar->name() which was inconsistent with
    //  loadItems/storeItems/updateItem which all use cal->id().)
    const QString calendarId = calendar->id();
    logOperation(QStringLiteral("START_SYNC"), calendarId);

    if (shouldFail(FailurePoint::OnStartSync)) {
        emit calendarError(collectionId, calendarId,
            m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on startSync")
                : m_failureMessage);
        return;
    }

    applyDelay();

    const QList<KCalendarCore::Incidence::Ptr> &finalCreations = stagedCreations;
    const QList<KCalendarCore::Incidence::Ptr> &finalUpdates = stagedUpdates;

    auto &calData = m_calendars[calendarId];
    KCalendarCore::ICalFormat format;

    // Process creations — check OnPush and OnStoreItems per-item for partial failure injection
    for (const auto &item : finalCreations) {
        if (shouldFail(FailurePoint::OnPush) || shouldFail(FailurePoint::OnStoreItems)) {
            QString msg = m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on push/store (creation) in startSync")
                : m_failureMessage;
            emit calendarError(collectionId, calendarId, msg);
            return;
        }
        logOperation(QStringLiteral("SYNC_CREATE"), calendarId, item->uid());
        QString ical = format.toICalString(item);
        auto clone = format.fromString(ical);
        if (clone) {
            calData[item->uid()] = clone;
        }
    }

    // Process updates — check OnPush and OnStoreItems per-item for partial failure injection
    for (const auto &item : finalUpdates) {
        if (shouldFail(FailurePoint::OnPush) || shouldFail(FailurePoint::OnStoreItems)) {
            QString msg = m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on push/store (update) in startSync")
                : m_failureMessage;
            emit calendarError(collectionId, calendarId, msg);
            return;
        }
        logOperation(QStringLiteral("SYNC_UPDATE"), calendarId, item->uid());
        QString ical = format.toICalString(item);
        auto clone = format.fromString(ical);
        if (clone) {
            calData[item->uid()] = clone;
        }
    }

    // Process deletions — check OnDelete per-item for partial failure injection
    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        if (shouldFail(FailurePoint::OnDelete)) {
            QString msg = m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on delete in startSync")
                : m_failureMessage;
            emit calendarError(collectionId, calendarId, msg);
            return;
        }
        logOperation(QStringLiteral("SYNC_DELETE"), calendarId, it.key());
        calData.remove(it.key());
    }

    emit syncCompleted(collectionId);
}

void MockBackend::removeItem(const QString &calId, const QString &itemUid)
{
    logOperation(QStringLiteral("REMOVE_ITEM"), calId, itemUid);

    applyDelay();

    if (m_calendars.contains(calId)) {
        m_calendars[calId].remove(itemUid);
        m_deletionLog[calId].append({itemUid, QDateTime::currentDateTimeUtc()});
        emit itemRemoved(calId, itemUid);
    }
}

FetchOperation* MockBackend::fetchItems(const QString &calendarId)
{
    logOperation(QStringLiteral("FETCH"), calendarId);

    auto *op = new FetchOperation(calendarId, this);
    registerOperation(op);
    op->setState(SyncOperation::Running);  // Transition from Pending -> Running

    if (shouldFail(FailurePoint::OnFetch)) {
        QTimer::singleShot(m_operationDelayMs, this, [op, calendarId, this]() {
            emit fetchStarted(calendarId, 0);
            emit fetchFinished(calendarId, false, m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on fetch")
                : m_failureMessage);
            op->fail(m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on fetch")
                : m_failureMessage);
        });
    } else if (m_fetchBlocking) {
        // F2 Task 22: run the fetch on a worker thread that blocks
        // on m_fetchBlocker until the test releases it. This lets
        // cancellation tests (C2, C3) deterministically cancel a
        // fetch mid-flight rather than racing the test thread
        // against a fast-completing op. Default off; only opted
        // into by tests that have called setFetchBlocking(true).
        auto *thread = QThread::create([this, op, calendarId]() {
            m_fetchBlocker.acquire();
            // Observe cancellation AFTER acquiring the semaphore so
            // the test has a window to call op->cancel() before we
            // complete. SyncOperation::cancel() transitions the
            // op to Cancelled atomically, so checking state() here
            // is the public-API observation point.
            if (op->state() == SyncOperation::Cancelled) {
                return;
            }
            QList<KCalendarCore::Incidence::Ptr> items =
                m_calendars.value(calendarId).values();
            op->setFetchedItems(items);
            op->complete();
        });
        QObject::connect(thread, &QThread::finished,
                         thread, &QThread::deleteLater);
        thread->start();
    } else {
        QTimer::singleShot(m_operationDelayMs, this, [op, calendarId, this]() {
            QList<KCalendarCore::Incidence::Ptr> items = m_calendars.value(calendarId).values();
            int total = items.size();

            // Emit streaming signals for real-time UI updates
            emit fetchStarted(calendarId, total);

            int current = 0;
            for (const auto &item : items) {
                current++;
                emit itemFetched(calendarId, item);
                emit fetchProgressChanged(calendarId, current, total);
            }

            emit fetchFinished(calendarId, true);
            op->setFetchedItems(items);
            op->complete();
        });
    }

    return op;
}

PushOperation* MockBackend::pushItems(const QString &calendarId,
                                       const QList<KCalendarCore::Incidence::Ptr> &items)
{
    for (const auto &item : items) {
        logOperation(QStringLiteral("PUSH"), calendarId, item->uid());
    }

    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);
    op->setState(SyncOperation::Running);  // Transition from Pending -> Running

    // Unified failure injection: OnPush || OnStoreItems both drive
    // the operation to Failed symmetrically. Per FINDINGS
    // ("MockBackend missing failure injection on updateItem and OnPush
    //  in storeItems"), the sync and async paths once disagreed about
    //  which point to honour; F2 Task 6 collapses them onto this path.
    if (shouldFail(FailurePoint::OnPush) || shouldFail(FailurePoint::OnStoreItems)) {
        QTimer::singleShot(m_operationDelayMs, this, [op, this]() {
            op->fail(m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on push")
                : m_failureMessage);
        });
    } else if (m_pushBlocking) {
        // F2 Task 22: blockable push, mirrors the fetch path. Used
        // by C3 (cancel during apply) which needs a PushOperation
        // that hangs until the test releases it.
        auto *thread = QThread::create(
            [this, op, calendarId, items]() {
            m_pushBlocker.acquire();
            // See fetchItems(): cancel() already transitions the op
            // to Cancelled, so a state() check is the public-API
            // observation point.
            if (op->state() == SyncOperation::Cancelled) {
                return;
            }
            auto &calendar = m_calendars[calendarId];
            KCalendarCore::ICalFormat format;
            QStringList succeededUids;
            for (const auto &item : items) {
                QString ical = format.toICalString(item);
                auto clone = format.fromString(ical);
                if (clone) {
                    calendar[item->uid()] = clone;
                    succeededUids.append(item->uid());
                }
            }
            op->setSucceededUids(succeededUids);
            op->complete();
        });
        QObject::connect(thread, &QThread::finished,
                         thread, &QThread::deleteLater);
        thread->start();
    } else {
        QTimer::singleShot(m_operationDelayMs, this,
                           [op, calendarId, items, this]() {
            // Store items
            auto &calendar = m_calendars[calendarId];
            KCalendarCore::ICalFormat format;
            QStringList succeededUids;
            for (const auto &item : items) {
                QString ical = format.toICalString(item);
                auto clone = format.fromString(ical);
                if (clone) {
                    calendar[item->uid()] = clone;
                    succeededUids.append(item->uid());
                }
            }
            op->setSucceededUids(succeededUids);
            op->complete();
        });
    }

    return op;
}


DeleteOperation* MockBackend::deleteItems(const QString &calendarId,
                                           const QStringList &uids)
{
    for (const auto &uid : uids) {
        logOperation(QStringLiteral("DELETE"), calendarId, uid);
    }

    auto *op = new DeleteOperation(calendarId, uids, this);
    registerOperation(op);
    op->setState(SyncOperation::Running);  // Transition from Pending -> Running

    if (shouldFail(FailurePoint::OnDelete)) {
        QTimer::singleShot(m_operationDelayMs, this, [op, this]() {
            op->fail(m_failureMessage.isEmpty()
                ? QStringLiteral("Mock failure on delete")
                : m_failureMessage);
        });
    } else {
        QTimer::singleShot(m_operationDelayMs, this, [op, calendarId, uids, this]() {
            QStringList succeededUids;
            if (m_calendars.contains(calendarId)) {
                auto &calendar = m_calendars[calendarId];
                for (const QString &uid : uids) {
                    calendar.remove(uid);
                    succeededUids.append(uid);
                }
            }
            op->setSucceededUids(succeededUids);
            op->complete();
        });
    }

    return op;
}

bool MockBackend::createCalendar(const QString &collectionId,
                                  const QString &calendarId,
                                  const QString &name,
                                  CalendarType type)
{
    Q_UNUSED(collectionId)
    Q_UNUSED(type)  // MockBackend supports all types in-memory

    logOperation(QStringLiteral("CREATE_CAL"), calendarId);

    if (shouldFail(FailurePoint::OnCreateCalendar)) {
        return false;
    }

    applyDelay();

    if (!m_calendars.contains(calendarId)) {
        m_calendars[calendarId] = QHash<QString, KCalendarCore::Incidence::Ptr>();
        m_calendarNames[calendarId] = name;
        emit calendarCreated(collectionId, calendarId);
        return true;
    }
    return false;  // Already exists
}

bool MockBackend::deleteCalendar(const QString &collectionId, const QString &calendarId)
{
    logOperation(QStringLiteral("DELETE_CAL"), calendarId);

    if (shouldFail(FailurePoint::OnDeleteCalendar)) {
        return false;
    }

    applyDelay();

    if (m_calendars.contains(calendarId)) {
        m_calendars.remove(calendarId);
        m_calendarNames.remove(calendarId);
        emit calendarDeleted(collectionId, calendarId);
        return true;
    }
    return false;  // Doesn't exist
}

void MockBackend::setFailurePoint(FailurePoint point,
                                  int afterNOperations,
                                  const QString &errorMessage)
{
    m_failurePoint = point;
    m_failAfterN = afterNOperations;
    m_operationCountForFailure = 0;
    m_failureMessage = errorMessage;
}

void MockBackend::clearFailurePoint()
{
    m_failurePoint = FailurePoint::None;
    m_failAfterN = 0;
    m_operationCountForFailure = 0;
    m_failureMessage.clear();
}

QStringList MockBackend::calendarIds() const
{
    return m_calendars.keys();
}

QStringList MockBackend::allUids(const QString &calendarId) const
{
    if (!m_calendars.contains(calendarId)) {
        return {};
    }
    return m_calendars.value(calendarId).keys();
}

KCalendarCore::Incidence::Ptr MockBackend::incidence(const QString &calendarId,
                                                      const QString &uid) const
{
    if (!m_calendars.contains(calendarId)) {
        return nullptr;
    }
    return m_calendars.value(calendarId).value(uid);
}

QString MockBackend::incidenceHash(const QString &calendarId, const QString &uid) const
{
    auto inc = incidence(calendarId, uid);
    if (!inc) {
        return QString();
    }
    return computeHash(inc);
}

void MockBackend::setCalendarData(const QString &calendarId,
                                  const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto &calendar = m_calendars[calendarId];
    calendar.clear();

    KCalendarCore::ICalFormat format;
    for (const auto &item : items) {
        // Clone to avoid sharing pointers
        QString ical = format.toICalString(item);
        auto clone = format.fromString(ical);
        if (clone) {
            calendar[item->uid()] = clone;
        }
    }
}

void MockBackend::addIncidence(const QString &calendarId,
                                const KCalendarCore::Incidence::Ptr &incidence)
{
    if (!incidence) return;

    KCalendarCore::ICalFormat format;
    QString ical = format.toICalString(incidence);
    auto clone = format.fromString(ical);
    if (clone) {
        m_calendars[calendarId][incidence->uid()] = clone;
    }
}

void MockBackend::clearAllData()
{
    m_calendars.clear();
    m_calendarNames.clear();
    m_operationLog.clear();
    m_deletionLog.clear();
}

void MockBackend::logOperation(const QString &operation,
                               const QString &calendarId,
                               const QString &uid)
{
    QString entry = operation;
    if (!calendarId.isEmpty()) {
        entry += QStringLiteral(":") + calendarId;
        if (!uid.isEmpty()) {
            entry += QStringLiteral(":") + uid;
        }
    }
    m_operationLog.append(entry);
}

bool MockBackend::shouldFail(FailurePoint point)
{
    if (m_failurePoint != point) {
        return false;
    }

    m_operationCountForFailure++;
    if (m_operationCountForFailure > m_failAfterN) {
        return true;
    }
    return false;
}

void MockBackend::applyDelay()
{
    if (m_operationDelayMs > 0) {
        QThread::msleep(m_operationDelayMs);
    }
}

QString MockBackend::computeHash(const KCalendarCore::Incidence::Ptr &incidence) const
{
    KCalendarCore::ICalFormat format;
    QString ical = format.toICalString(incidence);
    QByteArray hash = QCryptographicHash::hash(ical.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex().left(16));
}

// ============================================================================
// IBlobBackend helpers
// ============================================================================

static BackendRecord toBackendRecord(const KCalendarCore::Incidence::Ptr &incidence)
{
    KCalendarCore::ICalFormat format;
    const QString ical = format.toICalString(incidence);
    const QByteArray data = ical.toUtf8();
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);

    BackendRecord r;
    r.id           = incidence->uid();
    r.type         = (incidence->type() == KCalendarCore::IncidenceBase::TypeTodo)
                       ? QStringLiteral("VTODO")
                       : (incidence->type() == KCalendarCore::IncidenceBase::TypeJournal)
                           ? QStringLiteral("VJOURNAL")
                           : QStringLiteral("VEVENT");
    r.displayName  = incidence->summary();
    r.data         = data;
    r.contentHash  = QString::fromLatin1(hash.toHex());
    r.lastModified = incidence->lastModified();
    r.isDeleted    = false;
    return r;
}

static KCalendarCore::Incidence::Ptr fromBackendRecord(const BackendRecord &record)
{
    KCalendarCore::ICalFormat format;
    return format.fromString(QString::fromUtf8(record.data));
}

// ============================================================================
// IBlobBackend — Collections
// ============================================================================

QList<CollectionInfo> MockBackend::availableCollections()
{
    QList<CollectionInfo> result;
    for (const QString &calId : m_calendars.keys()) {
        CollectionInfo info;
        info.id   = calId;
        info.name = m_calendarNames.value(calId, calId);
        info.type = QStringLiteral("calendar");
        result.append(info);
    }
    return result;
}

CollectionInfo MockBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.name = m_calendarNames.value(collectionId, collectionId);
    info.type = QStringLiteral("calendar");
    return info;
}

QString MockBackend::createCollection(const CollectionInfo &info)
{
    if (!m_calendars.contains(info.id)) {
        m_calendars[info.id] = QHash<QString, KCalendarCore::Incidence::Ptr>();
        m_calendarNames[info.id] = info.name;
    }
    return info.id;
}

// ============================================================================
// IBlobBackend — Records
// ============================================================================

QList<BackendRecord> MockBackend::loadRecords(const QString &collectionId)
{
    logOperation(QStringLiteral("LOAD_RECORDS"), collectionId);
    if (shouldFail(FailurePoint::OnFetch)) {
        return {};   // silent — simulates a backend that fails without reporting error
    }
    QList<BackendRecord> result;
    if (!m_calendars.contains(collectionId)) {
        return result;
    }
    const auto &cal = m_calendars.value(collectionId);
    for (auto it = cal.constBegin(); it != cal.constEnd(); ++it) {
        if (it.value()) {
            result.append(toBackendRecord(it.value()));
        }
    }
    return result;
}

bool MockBackend::loadRecordsOrError(const QString &collectionId,
                                      QList<BackendRecord> &records,
                                      QString &error)
{
    if (shouldFail(FailurePoint::OnFetch)) {
        error = m_failureMessage.isEmpty()
            ? QStringLiteral("Mock failure on fetch")
            : m_failureMessage;
        return false;
    }
    records = loadRecords(collectionId);
    error.clear();
    return true;
}

std::optional<BackendRecord> MockBackend::loadRecord(const QString &recordId)
{
    // recordId == uid; search across all calendars
    for (auto calIt = m_calendars.constBegin(); calIt != m_calendars.constEnd(); ++calIt) {
        const auto &cal = calIt.value();
        if (cal.contains(recordId)) {
            return toBackendRecord(cal.value(recordId));
        }
    }
    return std::nullopt;
}

QString MockBackend::createRecord(const QString &collectionId, const BackendRecord &record)
{
    // Log every invocation BEFORE payload parse — lets tests verify
    // pass-through even for non-iCal payloads (e.g. Palm-wire bytes).
    m_createRecordCalls.append(record);

    auto incidence = fromBackendRecord(record);
    if (!incidence) {
        return {};
    }

    // Ensure the calendar bucket exists
    if (!m_calendars.contains(collectionId)) {
        m_calendars[collectionId] = QHash<QString, KCalendarCore::Incidence::Ptr>();
    }

    m_calendars[collectionId][incidence->uid()] = incidence;
    return incidence->uid();
}

bool MockBackend::updateRecord(const BackendRecord &record)
{
    // Find the calendar containing this uid
    for (auto calIt = m_calendars.begin(); calIt != m_calendars.end(); ++calIt) {
        if (calIt.value().contains(record.id)) {
            auto incidence = fromBackendRecord(record);
            if (!incidence) {
                return false;
            }
            calIt.value()[record.id] = incidence;
            return true;
        }
    }
    return false;
}

bool MockBackend::deleteRecord(const QString &recordId)
{
    for (auto calIt = m_calendars.begin(); calIt != m_calendars.end(); ++calIt) {
        if (calIt.value().contains(recordId)) {
            calIt.value().remove(recordId);
            m_deletionLog[calIt.key()].append({recordId, QDateTime::currentDateTimeUtc()});
            return true;
        }
    }
    return false;
}

// ============================================================================
// IBlobBackend — Change detection
// ============================================================================

QList<BackendRecord> MockBackend::modifiedSince(const QString &collectionId,
                                                 const QDateTime &since)
{
    logOperation(QStringLiteral("MODIFIED_SINCE"), collectionId);
    QList<BackendRecord> result;
    if (!m_calendars.contains(collectionId)) {
        return result;
    }
    const auto &cal = m_calendars.value(collectionId);
    for (auto it = cal.constBegin(); it != cal.constEnd(); ++it) {
        if (it.value() && it.value()->lastModified() > since) {
            result.append(toBackendRecord(it.value()));
        }
    }
    return result;
}

QStringList MockBackend::deletedSince(const QString &collectionId, const QDateTime &since)
{
    QStringList result;
    const auto &log = m_deletionLog.value(collectionId);
    for (const auto &entry : log) {
        if (entry.second > since) {
            result.append(entry.first);
        }
    }
    return result;
}


} // namespace Kalburator::Sync
