#include "orgbackend.h"
#include "syncoperation.h"
#include "backendcapabilities.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include "transcodingplan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include <QCryptographicHash>
#include <KCalendarCore/ICalFormat>

namespace Kalburator::Sync {

const QString OrgBackend::BackendTypeName = QStringLiteral("orgmode");

QString OrgBackend::backendType() const { return BackendTypeName; }

QList<Kalburator::Shape::Shape> OrgBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}

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
// Storing / Updating / Removing
// ============================================================================

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
                           const QMap<QString, QString> &stagedDeletions,
                           const TranscodingPlan& plan)
{
    Q_UNUSED(collectionId)

    if (!calendar) {
        qWarning() << "OrgBackend::startSync: Null calendar";
        emit syncCompleted(collectionId);
        return;
    }

    const QString calendarId = calendar->id();
    QList<KCalendarCore::Incidence::Ptr> allChanges = stagedCreations + stagedUpdates;

    if (!allChanges.isEmpty()) {
        OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calendarId);
        if (!orgFile) {
            qWarning() << "OrgBackend::startSync: Failed to load org file for" << calendarId;
        } else {
            // Write each incidence to the org file
            for (const auto &incidence : allChanges) {
                if (!incidence || incidence->uid().isEmpty())
                    continue;

                const QString uid = incidence->uid();
                auto headline = m_fileManager->findHeadlineByUid(
                    orgFile.staticCast<OrgMode::OrgElement>(), uid);

                if (headline) {
                    QString newParentUid = incidence->relatedTo(KCalendarCore::Incidence::RelTypeParent);
                    OrgMode::OrgElement *currentParent = headline->parent();
                    QString currentParentUid;
                    if (currentParent && currentParent != orgFile.data()) {
                        auto ph = dynamic_cast<OrgMode::Headline*>(currentParent);
                        if (ph) currentParentUid = m_fileManager->getUid(ph);
                    }
                    if (currentParentUid != newParentUid)
                        reparentHeadline(orgFile, headline, newParentUid);
                    m_fileManager->updateHeadlineFromIncidence(headline, incidence,
                        m_planningData.value(uid), m_roundtripData.value(uid));
                } else {
                    QString parentUid = incidence->relatedTo(KCalendarCore::Incidence::RelTypeParent);
                    OrgMode::OrgElement::Pointer parentElement;
                    if (!parentUid.isEmpty()) {
                        auto ph = m_fileManager->findHeadlineByUid(
                            orgFile.staticCast<OrgMode::OrgElement>(), parentUid);
                        if (ph) parentElement = ph.staticCast<OrgMode::OrgElement>();
                    }
                    if (!parentElement)
                        parentElement = orgFile.staticCast<OrgMode::OrgElement>();
                    m_fileManager->createNewHeadline(parentElement, incidence,
                        m_planningData.value(uid), m_roundtripData.value(uid), calendarId);
                }
            }
            m_fileManager->saveOrgFile(calendarId, collectSiblingOrders());
        }
    }

    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        removeItem(calendarId, it.key());
    }

    emit syncCompleted(collectionId);
}

void OrgBackend::storeCalendars(const QString &collectionId,
                               const QList<KCalendarCore::MemoryCalendar*> &calendars)
{
    Q_UNUSED(collectionId)
    Q_UNUSED(calendars)
    qWarning() << "OrgBackend::storeCalendars: Not implemented for OrgBackend";
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

// ============================================================================
// IBlobBackend implementation (Phase D Task 14)
//
// recordId     = uid derived from the org :ID: property (becomes incidence->uid())
// collectionId = calendarId (each .org file is a calendar)
// data         = serialized iCal text of the incidence via ICalFormat
// contentHash  = SHA-256 of the iCal bytes
// lastModified = QFileInfo::lastModified() of the .org file (whole-file granularity;
//                Phase E can improve to per-headline modified timestamp)
//
// GATED: this file is only compiled when KALBURATOR_HAVE_ORG_IO=ON.
// ============================================================================

namespace {

/// Serialize a single incidence to iCal bytes.
static QByteArray serializeIncidenceToIcal(const KCalendarCore::Incidence::Ptr &incidence)
{
    if (!incidence) return {};
    auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    tmpCal->addIncidence(incidence);
    QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});
    const QString icalStr = KCalendarCore::ICalFormat().toString(tmpCalPtr);
    return icalStr.toUtf8();
}

/// Build a BackendRecord from an incidence and .org file modification time.
static Kalburator::Sync::BackendRecord orgBlobRecord(
    const KCalendarCore::Incidence::Ptr &incidence,
    const QDateTime &orgFileMtime)
{
    const QByteArray bytes = serializeIncidenceToIcal(incidence);
    Kalburator::Sync::BackendRecord rec;
    rec.id          = incidence->uid();
    rec.type        = QStringLiteral("calendar");
    rec.data        = bytes;
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    rec.lastModified = orgFileMtime;
    rec.isDeleted   = false;
    return rec;
}

} // anonymous namespace

// --- Identity ---------------------------------------------------------------

QString OrgBackend::backendId() const
{
    const QString rootPath = m_fileManager->rootPath();
    const QByteArray h = QCryptographicHash::hash(
        (BackendTypeName + QLatin1Char(':') + rootPath).toUtf8(),
        QCryptographicHash::Sha256);
    return BackendTypeName + QLatin1Char(':') + QString::fromLatin1(h.toHex().left(16));
}

QString OrgBackend::displayName() const
{
    return QStringLiteral("OrgBackend(%1)").arg(m_fileManager->rootPath());
}

bool OrgBackend::isAvailable() const
{
    const QString rootPath = m_fileManager->rootPath();
    return !rootPath.isEmpty() && QDir(rootPath).exists();
}

// --- Collections ------------------------------------------------------------

QList<CollectionInfo> OrgBackend::availableCollections()
{
    QList<CollectionInfo> result;
    const QString rootPath = m_fileManager->rootPath();
    if (rootPath.isEmpty()) return result;

    const QDir dir(rootPath);
    if (!dir.exists()) return result;

    const QStringList orgFiles = dir.entryList(
        QStringList() << QStringLiteral("*.org"),
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &fileName : orgFiles) {
        const QString calId = QFileInfo(fileName).baseName();
        CollectionInfo info;
        info.id   = calId;
        info.name = calId;
        info.path = dir.filePath(fileName);
        info.type = QStringLiteral("calendar");
        result.append(info);
    }
    return result;
}

CollectionInfo OrgBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.name = collectionId;
    info.path = m_fileManager->filePathForCalendar(collectionId);
    info.type = QStringLiteral("calendar");
    return info;
}

QString OrgBackend::createCollection(const CollectionInfo &info)
{
    // Delegate to existing calendar creation infrastructure.
    if (createCalendar(QString(), info.id, info.name.isEmpty() ? info.id : info.name)) {
        return info.id;
    }
    return {};
}

// --- Records ----------------------------------------------------------------

QList<BackendRecord> OrgBackend::loadRecords(const QString &collectionId)
{
    QList<BackendRecord> result;
    if (collectionId.isEmpty()) return result;

    OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(collectionId);
    if (!orgFile) return result;

    const QFileInfo fi(m_fileManager->filePathForCalendar(collectionId));
    const QDateTime mtime = fi.lastModified();

    m_fileManager->traverseHeadlines(orgFile.staticCast<OrgMode::OrgElement>(), collectionId,
        [&](const OrgHeadlineResult &hr) {
            if (!hr.incidence) return;
            result.append(orgBlobRecord(hr.incidence, mtime));
        });
    return result;
}

std::optional<BackendRecord> OrgBackend::loadRecord(const QString &recordId)
{
    // recordId == uid; search all .org calendars.
    if (recordId.isEmpty()) return std::nullopt;

    const QString rootPath = m_fileManager->rootPath();
    const QDir dir(rootPath);
    const QStringList orgFiles = dir.entryList(
        QStringList() << QStringLiteral("*.org"),
        QDir::Files | QDir::NoDotAndDotDot);

    for (const QString &fileName : orgFiles) {
        const QString calId = QFileInfo(fileName).baseName();
        OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calId);
        if (!orgFile) continue;

        const QFileInfo fi(dir.filePath(fileName));
        const QDateTime mtime = fi.lastModified();
        std::optional<BackendRecord> found;

        m_fileManager->traverseHeadlines(orgFile.staticCast<OrgMode::OrgElement>(), calId,
            [&](const OrgHeadlineResult &hr) {
                if (!hr.incidence) return;
                if (hr.incidence->uid() == recordId) {
                    found = orgBlobRecord(hr.incidence, mtime);
                }
            });
        if (found.has_value()) return found;
    }
    return std::nullopt;
}

QString OrgBackend::createRecord(const QString &collectionId,
                                  const BackendRecord &record)
{
    if (collectionId.isEmpty() || record.id.isEmpty() || record.data.isEmpty())
        return {};

    // Parse the iCal back to an Incidence::Ptr, then write to the org file.
    auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});
    if (!KCalendarCore::ICalFormat().fromRawString(tmpCalPtr, record.data)) {
        qWarning() << "OrgBackend::createRecord: cannot parse iCal for uid" << record.id;
        return {};
    }
    const auto incidences = tmpCal->incidences();
    if (incidences.isEmpty()) {
        qWarning() << "OrgBackend::createRecord: no incidences in iCal for uid" << record.id;
        return {};
    }

    OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(collectionId);
    if (!orgFile) {
        qWarning() << "OrgBackend::createRecord: Failed to load org file for" << collectionId;
        return {};
    }
    for (const auto &incidence : incidences) {
        if (!incidence || incidence->uid().isEmpty()) continue;
        const QString uid = incidence->uid();
        QString parentUid = incidence->relatedTo(KCalendarCore::Incidence::RelTypeParent);
        OrgMode::OrgElement::Pointer parentElement;
        if (!parentUid.isEmpty()) {
            auto ph = m_fileManager->findHeadlineByUid(
                orgFile.staticCast<OrgMode::OrgElement>(), parentUid);
            if (ph) parentElement = ph.staticCast<OrgMode::OrgElement>();
        }
        if (!parentElement)
            parentElement = orgFile.staticCast<OrgMode::OrgElement>();
        m_fileManager->createNewHeadline(parentElement, incidence,
            m_planningData.value(uid), m_roundtripData.value(uid), collectionId);
    }
    m_fileManager->saveOrgFile(collectionId, collectSiblingOrders());
    return record.id;
}

bool OrgBackend::updateRecord(const BackendRecord &record)
{
    if (record.id.isEmpty() || record.data.isEmpty()) return false;

    auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});
    if (!KCalendarCore::ICalFormat().fromRawString(tmpCalPtr, record.data))
        return false;

    const auto incidences = tmpCal->incidences();
    if (incidences.isEmpty()) return false;

    // Find which calendar owns this uid.
    const QString rootPath = m_fileManager->rootPath();
    const QDir dir(rootPath);
    const QStringList orgFiles = dir.entryList(
        QStringList() << QStringLiteral("*.org"),
        QDir::Files | QDir::NoDotAndDotDot);

    for (const QString &fileName : orgFiles) {
        const QString calId = QFileInfo(fileName).baseName();
        OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calId);
        if (!orgFile) continue;

        bool found = false;
        m_fileManager->traverseHeadlines(orgFile.staticCast<OrgMode::OrgElement>(), calId,
            [&](const OrgHeadlineResult &hr) {
                if (hr.incidence && hr.incidence->uid() == record.id) found = true;
            });
        if (!found) continue;

        for (const auto &inc : incidences) {
            inc->setReadOnly(false);
            const QString uid = inc->uid();
            auto headline = m_fileManager->findHeadlineByUid(
                orgFile.staticCast<OrgMode::OrgElement>(), uid);
            if (!headline) {
                qWarning() << "OrgBackend::updateRecord: Headline not found for UID" << uid;
                continue;
            }
            QString newParentUid = inc->relatedTo(KCalendarCore::Incidence::RelTypeParent);
            OrgMode::OrgElement *currentParent = headline->parent();
            QString currentParentUid;
            if (currentParent && currentParent != orgFile.data()) {
                auto ph = dynamic_cast<OrgMode::Headline*>(currentParent);
                if (ph) currentParentUid = m_fileManager->getUid(ph);
            }
            if (currentParentUid != newParentUid)
                reparentHeadline(orgFile, headline, newParentUid);
            m_fileManager->updateHeadlineFromIncidence(headline, inc,
                m_planningData.value(uid), m_roundtripData.value(uid));
        }
        m_fileManager->saveOrgFile(calId, collectSiblingOrders());
        return true;
    }
    return false;
}

bool OrgBackend::deleteRecord(const QString &recordId)
{
    if (recordId.isEmpty()) return false;

    const QString rootPath = m_fileManager->rootPath();
    const QDir dir(rootPath);
    const QStringList orgFiles = dir.entryList(
        QStringList() << QStringLiteral("*.org"),
        QDir::Files | QDir::NoDotAndDotDot);

    for (const QString &fileName : orgFiles) {
        const QString calId = QFileInfo(fileName).baseName();
        OrgMode::OrgFile::Pointer orgFile = m_fileManager->loadOrgFile(calId);
        if (!orgFile) continue;

        bool found = false;
        m_fileManager->traverseHeadlines(orgFile.staticCast<OrgMode::OrgElement>(), calId,
            [&](const OrgHeadlineResult &hr) {
                if (hr.incidence && hr.incidence->uid() == recordId) found = true;
            });
        if (!found) continue;

        removeItem(calId, recordId);
        return true;
    }
    return false;
}

// --- Change detection -------------------------------------------------------

QList<BackendRecord> OrgBackend::modifiedSince(const QString &collectionId,
                                                const QDateTime &since)
{
    // .org files have no per-headline mtime; use whole-file mtime.
    if (collectionId.isEmpty()) return {};

    const QString filePath = m_fileManager->filePathForCalendar(collectionId);
    const QFileInfo fi(filePath);
    if (!fi.exists()) return {};

    if (since.isValid() && fi.lastModified() <= since) {
        return {};  // org file not touched since last sync
    }

    return loadRecords(collectionId);
}

QStringList OrgBackend::deletedSince(const QString &collectionId,
                                      const QDateTime &since)
{
    // OrgBackend has no deletion log — Phase E can improve this.
    Q_UNUSED(collectionId)
    Q_UNUSED(since)
    return {};
}

} // namespace Kalburator::Sync
