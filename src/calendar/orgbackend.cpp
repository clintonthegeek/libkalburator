#include "orgbackend.h"
#include "syncoperation.h"
#include "backendcapabilities.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>

namespace Kalburator::Sync {

const QString OrgBackend::BackendTypeName = QStringLiteral("orgmode");

QString OrgBackend::backendType() const { return BackendTypeName; }

OrgBackend::OrgBackend(const QString &orgRootPath, QObject *parent)
    : SyncBackend(parent)
    , m_fileManager(new OrgFileManager(orgRootPath, this))
{
}

// ============================================================================
// Capability Declarations
// ============================================================================

BackendCapabilities OrgBackend::capabilities() const
{
    return BackendCapabilities::orgmodeDefaults();
}

bool OrgBackend::supportsCalendarCreation() const
{
    return true;
}

RecurrenceCapabilities OrgBackend::recurrenceCapabilities() const
{
    RecurrenceCapabilities caps;

    caps.supportsDaily = true;
    caps.supportsWeekly = true;
    caps.supportsMonthly = true;
    caps.supportsYearly = true;
    caps.supportsHourly = false;
    caps.supportsMinutely = false;
    caps.supportsSecondly = false;

    caps.supportsByDay = false;
    caps.supportsByMonthDay = false;
    caps.supportsByYearDay = false;
    caps.supportsByWeekNo = false;
    caps.supportsByMonth = false;
    caps.supportsBySetPos = false;

    caps.supportsCount = false;
    caps.supportsUntil = false;
    caps.maxInterval = 0;

    caps.supportsMultipleRRules = false;
    caps.supportsExRules = false;
    caps.supportsRDates = false;
    caps.supportsExDates = false;

    caps.backendType = backendType();
    caps.displayName = QStringLiteral("Org Mode");

    return caps;
}

bool OrgBackend::discoveredWritable(const QString &calendarId) const
{
    if (calendarId.isEmpty())
        return true;

    QString filePath = m_fileManager->filePathForCalendar(calendarId);
    QFileInfo fileInfo(filePath);

    if (!fileInfo.exists()) {
        QFileInfo dirInfo(m_fileManager->rootPath());
        return dirInfo.isWritable();
    }

    if (!fileInfo.isWritable()) {
        qDebug() << "OrgBackend: Calendar" << calendarId << "is read-only (no write permission)";
        return false;
    }

    return true;
}

// ============================================================================
// Private Helpers
// ============================================================================

void OrgBackend::reparentHeadline(OrgMode::OrgFile::Pointer orgFile,
                                   OrgMode::Headline::Pointer headline,
                                   const QString &newParentUid)
{
    OrgMode::OrgElement *currentParent = headline->parent();

    // Remove from current parent
    if (currentParent) {
        OrgMode::OrgElement::List currentChildren = currentParent->children();
        OrgMode::OrgElement::List newChildren;
        for (const auto &child : currentChildren) {
            if (child.data() != headline.data()) {
                newChildren.append(child);
            }
        }
        currentParent->setChildren(newChildren);
    }

    // Find new parent and add
    OrgMode::OrgElement *newParentElement = orgFile.data();
    if (!newParentUid.isEmpty()) {
        auto newParentHeadline = m_fileManager->findHeadlineByUid(
            orgFile.staticCast<OrgMode::OrgElement>(), newParentUid);
        if (newParentHeadline) {
            newParentElement = newParentHeadline.data();
        } else {
            qWarning() << "OrgBackend::reparentHeadline: Parent not found:" << newParentUid << "- using top level";
        }
    }

    newParentElement->addChild(headline);
}

QMap<QString, int> OrgBackend::collectSiblingOrders() const
{
    QMap<QString, int> orders;
    for (auto it = m_planningData.constBegin(); it != m_planningData.constEnd(); ++it) {
        if (it.value().siblingOrder >= 0) {
            orders.insert(it.key(), it.value().siblingOrder);
        }
    }
    return orders;
}

// ============================================================================
// Calendar Discovery
// ============================================================================

void OrgBackend::loadCalendars(const QString &collectionId)
{
    QString rootPath = m_fileManager->rootPath();
    if (rootPath.isEmpty()) {
        qWarning() << "OrgBackend: org root path is empty";
        emit loadCalendarsFinished(collectionId, false, QStringLiteral("Org root path is empty"));
        return;
    }

    QDir dir(rootPath);
    if (!dir.exists()) {
        qWarning() << "OrgBackend: org root directory does not exist:" << rootPath;
        emit loadCalendarsFinished(collectionId, false,
            QStringLiteral("Directory does not exist: %1").arg(rootPath));
        return;
    }

    QStringList orgFiles = dir.entryList(QStringList() << "*.org", QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &fileName : orgFiles) {
        QString calendarId = QFileInfo(fileName).baseName();
        emit calendarDiscovered(collectionId, calendarId);
        qDebug() << "OrgBackend: calendarDiscovered emitted for" << calendarId;
    }

    emit loadCalendarsFinished(collectionId, true);
}

// ============================================================================
// Loading
// ============================================================================

void OrgBackend::loadItems(KCalendarCore::MemoryCalendar *cal, bool suppressSignals)
{
    if (!cal) {
        qWarning() << "OrgBackend::loadItems: Null calendar";
        if (!suppressSignals) emit calendarLoaded(cal);
        return;
    }

    QString calendarId = cal->id();
    if (calendarId.isEmpty()) {
        qWarning() << "OrgBackend::loadItems: Empty calendar ID";
        if (!suppressSignals) emit calendarLoaded(cal);
        return;
    }

    OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calendarId);
    if (!orgFile) {
        qWarning() << "OrgBackend::loadItems: Failed to load org file for calendar" << calendarId;
        if (!suppressSignals) {
            emit fetchStarted(calendarId, 0);
            emit fetchFinished(calendarId, false, QStringLiteral("Failed to load org file"));
            emit calendarLoaded(cal);
        }
        return;
    }

    // Clear existing incidences
    for (const auto &inc : cal->incidences()) {
        cal->deleteIncidence(inc);
    }

    int totalCount = m_fileManager->countHeadlines(orgFile.staticCast<OrgMode::OrgElement>());
    if (!suppressSignals) {
        emit fetchStarted(calendarId, totalCount);
    }

    int currentProgress = 0;
    m_fileManager->traverseHeadlines(orgFile.staticCast<OrgMode::OrgElement>(), calendarId,
        [&](const OrgHeadlineResult &result) {
            if (!result.incidence)
                return;

            QString uid = result.incidence->uid();
            m_roundtripData.insert(uid, result.roundtrip);
            m_planningData.insert(uid, result.planning);

            cal->addIncidence(result.incidence);
            if (!suppressSignals) {
                emit itemLoaded(cal, result.incidence, QString());
                emit itemFetched(calendarId, result.incidence);
                currentProgress++;
                emit fetchProgressChanged(calendarId, currentProgress, totalCount);
            }
        });

    if (!suppressSignals) {
        emit fetchFinished(calendarId, true);
        emit calendarLoaded(cal);
    }
}

// ============================================================================
// Storing / Updating / Removing
// ============================================================================

void OrgBackend::storeItems(KCalendarCore::MemoryCalendar *cal,
                            const QList<KCalendarCore::Incidence::Ptr> &items)
{
    if (!cal) {
        qWarning() << "OrgBackend::storeItems: Null calendar";
        return;
    }

    QString calendarId = cal->id();
    if (calendarId.isEmpty()) {
        qWarning() << "OrgBackend::storeItems: Empty calendar ID";
        return;
    }

    OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calendarId);
    if (!orgFile) {
        qWarning() << "OrgBackend::storeItems: Failed to load org file";
        emit writeStarted(calendarId, 0);
        emit writeFinished(calendarId, false, QStringLiteral("Failed to load org file"));
        return;
    }

    int totalItems = items.size();
    int currentItem = 0;
    emit writeStarted(calendarId, totalItems);

    for (const auto &incidence : items) {
        if (!incidence)
            continue;

        QString uid = incidence->uid();
        if (uid.isEmpty()) {
            qWarning() << "OrgBackend::storeItems: Incidence with empty UID skipped";
            continue;
        }

        auto headline = m_fileManager->findHeadlineByUid(
            orgFile.staticCast<OrgMode::OrgElement>(), uid);

        if (headline) {
            // Check reparenting
            QString newParentUid = incidence->relatedTo(KCalendarCore::Incidence::RelTypeParent);
            OrgMode::OrgElement *currentParent = headline->parent();
            QString currentParentUid;
            if (currentParent && currentParent != orgFile.data()) {
                auto currentParentHeadline = dynamic_cast<OrgMode::Headline*>(currentParent);
                if (currentParentHeadline) {
                    currentParentUid = m_fileManager->getUid(currentParentHeadline);
                }
            }

            if (currentParentUid != newParentUid) {
                reparentHeadline(orgFile, headline, newParentUid);
            }

            m_fileManager->updateHeadlineFromIncidence(headline, incidence,
                m_planningData.value(uid), m_roundtripData.value(uid));
        } else {
            // Create new headline
            QString parentUid = incidence->relatedTo(KCalendarCore::Incidence::RelTypeParent);
            OrgMode::OrgElement::Pointer parentElement;

            if (!parentUid.isEmpty()) {
                auto parentHeadline = m_fileManager->findHeadlineByUid(
                    orgFile.staticCast<OrgMode::OrgElement>(), parentUid);
                if (parentHeadline) {
                    parentElement = parentHeadline.staticCast<OrgMode::OrgElement>();
                }
            }
            if (!parentElement) {
                parentElement = orgFile.staticCast<OrgMode::OrgElement>();
            }

            m_fileManager->createNewHeadline(parentElement, incidence,
                m_planningData.value(uid), m_roundtripData.value(uid), calendarId);
        }

        currentItem++;
        emit writeProgressChanged(calendarId, currentItem, totalItems);
    }

    m_fileManager->saveOrgFile(calendarId, collectSiblingOrders());
    emit writeFinished(calendarId, true);
}

void OrgBackend::updateItem(KCalendarCore::MemoryCalendar *cal,
                            const KCalendarCore::Incidence::Ptr &item,
                            const QString &icalData)
{
    Q_UNUSED(icalData)

    if (!cal || !item) {
        qWarning() << "OrgBackend::updateItem: Invalid calendar or incidence";
        return;
    }

    QString calendarId = cal->id();
    if (calendarId.isEmpty()) {
        qWarning() << "OrgBackend::updateItem: Empty calendar ID";
        return;
    }

    OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calendarId);
    if (!orgFile) {
        qWarning() << "OrgBackend::updateItem: Failed to load org file";
        return;
    }

    QString uid = item->uid();
    auto headline = m_fileManager->findHeadlineByUid(
        orgFile.staticCast<OrgMode::OrgElement>(), uid);
    if (!headline) {
        qWarning() << "OrgBackend::updateItem: Headline not found for UID" << uid;
        return;
    }

    // Check reparenting
    QString newParentUid = item->relatedTo(KCalendarCore::Incidence::RelTypeParent);
    OrgMode::OrgElement *currentParent = headline->parent();
    QString currentParentUid;
    if (currentParent && currentParent != orgFile.data()) {
        auto currentParentHeadline = dynamic_cast<OrgMode::Headline*>(currentParent);
        if (currentParentHeadline) {
            currentParentUid = m_fileManager->getUid(currentParentHeadline);
        }
    }

    if (currentParentUid != newParentUid) {
        reparentHeadline(orgFile, headline, newParentUid);
    }

    m_fileManager->updateHeadlineFromIncidence(headline, item,
        m_planningData.value(uid), m_roundtripData.value(uid));

    m_fileManager->saveOrgFile(calendarId, collectSiblingOrders());
}

void OrgBackend::removeItem(const QString &calId, const QString &itemUid)
{
    if (calId.isEmpty() || itemUid.isEmpty()) {
        qWarning() << "OrgBackend::removeItem: Empty calendar ID or item UID";
        return;
    }

    OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calId);
    if (!orgFile) {
        qWarning() << "OrgBackend::removeItem: Failed to load org file";
        return;
    }

    bool removed = m_fileManager->removeHeadlineByUid(
        orgFile.staticCast<OrgMode::OrgElement>(), itemUid);
    if (removed) {
        m_fileManager->saveOrgFile(calId);
        m_roundtripData.remove(itemUid);
        m_planningData.remove(itemUid);
        qDebug() << "OrgBackend::removeItem: Removed headline with UID" << itemUid;
    } else {
        qWarning() << "OrgBackend::removeItem: Headline with UID not found:" << itemUid;
    }
}

void OrgBackend::startSync(const QString &collectionId,
                           KCalendarCore::MemoryCalendar *calendar,
                           const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                           const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                           const QMap<QString, QString> &stagedDeletions)
{
    Q_UNUSED(collectionId)

    if (!calendar) {
        qWarning() << "OrgBackend::startSync: Null calendar";
        emit syncCompleted(collectionId);
        return;
    }

    QList<KCalendarCore::Incidence::Ptr> allChanges = stagedCreations + stagedUpdates;
    if (!allChanges.isEmpty()) {
        storeItems(calendar, allChanges);
    }

    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        removeItem(calendar->id(), it.key());
    }

    emit syncCompleted(collectionId);
}

void OrgBackend::storeCalendars(const QString &collectionId,
                               const QList<KCalendarCore::MemoryCalendar*> &calendars)
{
    Q_UNUSED(collectionId)
    Q_UNUSED(calendars)
    qWarning() << "OrgBackend::storeCalendars: Not implemented -- use storeItems() for individual items";
}

// ============================================================================
// Operation-Based API
// ============================================================================

FetchOperation* OrgBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    registerOperation(op);

    QTimer::singleShot(0, this, [this, op, calendarId]() {
        if (op->state() == SyncOperation::Cancelled)
            return;

        op->setState(SyncOperation::Running);

        if (calendarId.isEmpty()) {
            op->fail(QStringLiteral("Invalid calendar ID"));
            emit fetchFinished(calendarId, false, QStringLiteral("Invalid calendar ID"));
            return;
        }

        QString filePath = m_fileManager->filePathForCalendar(calendarId);
        if (!QFile::exists(filePath)) {
            QString errorMsg = QStringLiteral("Org file does not exist: %1").arg(filePath);
            op->fail(errorMsg);
            emit fetchFinished(calendarId, false, errorMsg);
            return;
        }

        OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calendarId);
        if (!orgFile) {
            QString errorMsg = QStringLiteral("Failed to parse org file");
            op->fail(errorMsg);
            emit fetchFinished(calendarId, false, errorMsg);
            return;
        }

        int totalCount = m_fileManager->countHeadlines(orgFile.staticCast<OrgMode::OrgElement>());
        emit fetchStarted(calendarId, totalCount);

        if (totalCount == 0) {
            op->setFetchedItems({});
            op->complete();
            emit fetchFinished(calendarId, true);
            return;
        }

        // Collect incidences via traversal
        QList<KCalendarCore::Incidence::Ptr> fetchedItems;
        int currentProgress = 0;

        m_fileManager->traverseHeadlines(orgFile.staticCast<OrgMode::OrgElement>(), calendarId,
            [&](const OrgHeadlineResult &result) {
                if (!result.incidence)
                    return;

                QString uid = result.incidence->uid();
                m_roundtripData.insert(uid, result.roundtrip);
                m_planningData.insert(uid, result.planning);

                fetchedItems.append(result.incidence);
                emit itemFetched(calendarId, result.incidence);
                currentProgress++;
                emit fetchProgressChanged(calendarId, currentProgress, totalCount);
            });

        qDebug() << "OrgBackend::fetchItems: Fetched" << fetchedItems.size()
                 << "incidences for calendar" << calendarId;

        op->setFetchedItems(fetchedItems);
        op->complete();
        emit fetchFinished(calendarId, true);
    });

    return op;
}

PushOperation* OrgBackend::pushItems(const QString &calendarId,
                                      const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);

    QTimer::singleShot(0, this, [this, op, calendarId, items]() {
        if (op->state() == SyncOperation::Cancelled)
            return;

        op->setState(SyncOperation::Running);

        if (calendarId.isEmpty()) {
            op->fail(QStringLiteral("Invalid calendar ID"));
            return;
        }

        OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calendarId);
        if (!orgFile) {
            // Try to create the file
            QString filePath = m_fileManager->filePathForCalendar(calendarId);
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                op->fail(QStringLiteral("Failed to create org file: %1").arg(filePath));
                return;
            }
            file.close();

            orgFile = m_fileManager->loadOrgFile(calendarId);
            if (!orgFile) {
                op->fail(QStringLiteral("Failed to initialize org file"));
                return;
            }
        }

        QStringList succeededUids;
        QStringList failedUids;
        int totalItems = items.size();
        int currentItem = 0;

        emit writeStarted(calendarId, totalItems);

        for (const auto &incidence : items) {
            if (incidence.isNull())
                continue;

            QString uid = incidence->uid();
            if (uid.isEmpty()) {
                qWarning() << "OrgBackend::pushItems: Incidence with empty UID skipped";
                failedUids.append(QStringLiteral("(empty-uid)"));
                continue;
            }

            auto headline = m_fileManager->findHeadlineByUid(
                orgFile.staticCast<OrgMode::OrgElement>(), uid);

            if (headline) {
                m_fileManager->updateHeadlineFromIncidence(headline, incidence,
                    m_planningData.value(uid), m_roundtripData.value(uid));
            } else {
                // Resolve parent so the headline is nested correctly
                QString parentUid = incidence->relatedTo(KCalendarCore::Incidence::RelTypeParent);
                OrgMode::OrgElement::Pointer parentElement;
                if (!parentUid.isEmpty()) {
                    auto parentHeadline = m_fileManager->findHeadlineByUid(
                        orgFile.staticCast<OrgMode::OrgElement>(), parentUid);
                    if (parentHeadline)
                        parentElement = parentHeadline.staticCast<OrgMode::OrgElement>();
                }
                if (!parentElement)
                    parentElement = orgFile.staticCast<OrgMode::OrgElement>();

                m_fileManager->createNewHeadline(
                    parentElement, incidence,
                    m_planningData.value(uid), m_roundtripData.value(uid), calendarId);
            }

            succeededUids.append(uid);
            currentItem++;
            emit writeProgressChanged(calendarId, currentItem, totalItems);
        }

        m_fileManager->saveOrgFile(calendarId, collectSiblingOrders());
        emit writeFinished(calendarId, true);

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

DeleteOperation* OrgBackend::deleteItems(const QString &calendarId,
                                          const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);
    registerOperation(op);

    QTimer::singleShot(0, this, [this, op, calendarId, uids]() {
        if (op->state() == SyncOperation::Cancelled)
            return;

        op->setState(SyncOperation::Running);

        if (calendarId.isEmpty()) {
            op->fail(QStringLiteral("Invalid calendar ID"));
            return;
        }

        OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calendarId);
        if (!orgFile) {
            op->fail(QStringLiteral("Failed to load org file"));
            return;
        }

        QStringList succeededUids;
        QStringList failedUids;

        for (const QString &uid : uids) {
            if (uid.isEmpty())
                continue;

            bool removed = m_fileManager->removeHeadlineByUid(
                orgFile.staticCast<OrgMode::OrgElement>(), uid);
            if (removed) {
                succeededUids.append(uid);
                m_roundtripData.remove(uid);
                m_planningData.remove(uid);
            } else {
                failedUids.append(uid);
                qWarning() << "OrgBackend::deleteItems: Headline with UID not found:" << uid;
            }
        }

        if (!succeededUids.isEmpty()) {
            m_fileManager->saveOrgFile(calendarId);
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
// Calendar Management (delegates to OrgFileManager)
// ============================================================================

bool OrgBackend::createCalendar(const QString &collectionId, const QString &calendarId,
                                 const QString &name, CalendarType type)
{
    Q_UNUSED(collectionId)
    Q_UNUSED(type)
    return m_fileManager->createOrgFile(calendarId, name);
}

bool OrgBackend::deleteCalendar(const QString &collectionId, const QString &calendarId)
{
    Q_UNUSED(collectionId)

    bool result = m_fileManager->deleteOrgFile(calendarId);
    if (result) {
        // Clear internal maps for this calendar's items
        QMutableHashIterator<QString, OrgPlanningData> pit(m_planningData);
        while (pit.hasNext()) {
            pit.next();
            // We don't track which UIDs belong to which calendar in maps,
            // but the OrgFileManager cache is already cleared.
        }
    }
    return result;
}

bool OrgBackend::updateCalendar(const QString &collectionId, const QString &calendarId,
                                 const QVariantMap &properties)
{
    Q_UNUSED(collectionId)

    bool result = m_fileManager->updateOrgFileHeaders(calendarId, properties);
    if (result) {
        emit calendarUpdated(collectionId, calendarId);
    }
    return result;
}

// ============================================================================
// Calendar Property Getters
// ============================================================================

QColor OrgBackend::calendarColor(const QString &calendarId) const
{
    QString colorStr = m_fileManager->getFileHeader(calendarId, QStringLiteral("COLOR"));
    if (colorStr.isEmpty())
        return QColor();

    QColor color(colorStr);
    return color.isValid() ? color : QColor();
}

QString OrgBackend::calendarDescription(const QString &calendarId) const
{
    return m_fileManager->getFileHeader(calendarId, QStringLiteral("DESCRIPTION"));
}

// ============================================================================
// Source File Access
// ============================================================================

QString OrgBackend::sourceFilePath(const QString &calendarId) const
{
    return m_fileManager->filePathForCalendar(calendarId);
}


} // namespace Kalburator::Sync
