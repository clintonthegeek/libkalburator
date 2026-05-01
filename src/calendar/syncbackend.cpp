#include "syncbackend.h"
#include "syncoperation.h"
#include "backendcapabilities.h"
#include "logicalcalendar.h"
#include "discoveredcalendar.h"
#include <KCalendarCore/RecurrenceRule>
#include <QDebug>

namespace Kalburator::Sync {

// ============================================================================
// RecurrenceCapabilities implementation
// ============================================================================

bool RecurrenceCapabilities::supportsFrequency(KCalendarCore::RecurrenceRule::PeriodType type) const
{
    using PT = KCalendarCore::RecurrenceRule::PeriodType;
    switch (type) {
    case PT::rDaily:    return supportsDaily;
    case PT::rWeekly:   return supportsWeekly;
    case PT::rMonthly:  return supportsMonthly;
    case PT::rYearly:   return supportsYearly;
    case PT::rHourly:   return supportsHourly;
    case PT::rMinutely: return supportsMinutely;
    case PT::rSecondly: return supportsSecondly;
    default:            return false;
    }
}

QString RecurrenceCapabilities::limitationsDescription() const
{
    QStringList limitations;

    // Frequency limitations
    QStringList unsupportedFreqs;
    if (!supportsHourly)   unsupportedFreqs << QStringLiteral("hourly");
    if (!supportsMinutely) unsupportedFreqs << QStringLiteral("minutely");
    if (!supportsSecondly) unsupportedFreqs << QStringLiteral("secondly");
    if (!unsupportedFreqs.isEmpty()) {
        limitations << QStringLiteral("No %1 recurrence").arg(unsupportedFreqs.join(QStringLiteral("/")));
    }

    // By-rule limitations
    if (!supportsByDay && !supportsByMonthDay && !supportsByYearDay &&
        !supportsByWeekNo && !supportsByMonth && !supportsBySetPos) {
        limitations << QStringLiteral("No complex patterns (BYDAY, BYMONTHDAY, etc.)");
    }

    // Count/Until limitations
    if (!supportsCount && !supportsUntil) {
        limitations << QStringLiteral("No repeat limits (COUNT/UNTIL)");
    }

    // Multiple rules and exceptions
    if (!supportsMultipleRRules) {
        limitations << QStringLiteral("Single recurrence rule only");
    }
    if (!supportsExDates && !supportsExRules) {
        limitations << QStringLiteral("No exception dates");
    }

    if (limitations.isEmpty()) {
        return QStringLiteral("Full iCalendar recurrence support");
    }

    return limitations.join(QStringLiteral("; "));
}

// ============================================================================
// RecurrenceLossInfo implementation
// ============================================================================

QString RecurrenceLossInfo::summary() const
{
    if (!hasLoss) {
        return QString();
    }

    if (lostDetails.isEmpty()) {
        return QStringLiteral("Some recurrence information may be lost.");
    }

    return lostDetails.join(QStringLiteral("\n"));
}

// ============================================================================
// SyncBackend implementation
// ============================================================================

SyncBackend::SyncBackend(QObject *parent)
    : QObject(parent)
{
}

QString SyncBackend::resourceId() const
{
    return QStringLiteral("backend:") +
        QString::number(reinterpret_cast<quintptr>(this), 16);
}

Kalburator::Shape::Shape SyncBackend::shapeFor(const QString &) const
{
    auto shapes = nativeShapes();
    if (shapes.isEmpty())
        return Kalburator::Shape::Shape::Any();
    return shapes.first();
}

BackendCapabilities SyncBackend::capabilities() const
{
    // Default implementation: full iCalendar support (LocalBackend-like)
    return BackendCapabilities::localDefaults();
}

RecurrenceCapabilities SyncBackend::recurrenceCapabilities() const
{
    // Default implementation: full iCalendar support (for LocalBackend)
    RecurrenceCapabilities caps;
    caps.supportsDaily = true;
    caps.supportsWeekly = true;
    caps.supportsMonthly = true;
    caps.supportsYearly = true;
    caps.supportsHourly = true;
    caps.supportsMinutely = true;
    caps.supportsSecondly = true;
    caps.supportsByDay = true;
    caps.supportsByMonthDay = true;
    caps.supportsByYearDay = true;
    caps.supportsByWeekNo = true;
    caps.supportsByMonth = true;
    caps.supportsBySetPos = true;
    caps.supportsCount = true;
    caps.supportsUntil = true;
    caps.maxInterval = 0;  // Unlimited
    caps.supportsMultipleRRules = true;
    caps.supportsExRules = true;
    caps.supportsRDates = true;
    caps.supportsExDates = true;
    caps.backendType = backendType();
    caps.displayName = QStringLiteral("iCalendar (ICS)");
    return caps;
}

RecurrenceLossInfo SyncBackend::analyzeRecurrenceLoss(
    const KCalendarCore::Incidence::Ptr &incidence) const
{
    RecurrenceLossInfo loss;

    if (!incidence || !incidence->recurs()) {
        return loss;  // No recurrence, no loss
    }

    const RecurrenceCapabilities caps = recurrenceCapabilities();
    const KCalendarCore::Recurrence *recurrence = incidence->recurrence();

    // Check for multiple RRULEs
    const auto rrules = recurrence->rRules();
    if (rrules.size() > 1 && !caps.supportsMultipleRRules) {
        loss.hasLoss = true;
        loss.multipleRulesLost = true;
        loss.lostDetails << QStringLiteral("Multiple recurrence rules will be collapsed to one");
    }

    // Analyze each RRULE
    for (const auto *rrule : rrules) {
        // Check frequency support
        if (!caps.supportsFrequency(rrule->recurrenceType())) {
            loss.hasLoss = true;
            loss.frequencyLost = true;
            QString freqName;
            switch (rrule->recurrenceType()) {
            case KCalendarCore::RecurrenceRule::rHourly:   freqName = QStringLiteral("Hourly"); break;
            case KCalendarCore::RecurrenceRule::rMinutely: freqName = QStringLiteral("Minutely"); break;
            case KCalendarCore::RecurrenceRule::rSecondly: freqName = QStringLiteral("Secondly"); break;
            default: freqName = QStringLiteral("Unknown"); break;
            }
            loss.lostDetails << QStringLiteral("%1 recurrence not supported").arg(freqName);
        }

        // Check interval limits
        if (caps.maxInterval > 0 && rrule->frequency() > caps.maxInterval) {
            loss.hasLoss = true;
            loss.lostDetails << QStringLiteral("Interval %1 exceeds maximum %2")
                                    .arg(rrule->frequency()).arg(caps.maxInterval);
        }

        // Check COUNT
        if (rrule->duration() > 0 && !caps.supportsCount) {
            loss.hasLoss = true;
            loss.countUntilLost = true;
            loss.lostDetails << QStringLiteral("Repeat count (%1 times) not supported")
                                    .arg(rrule->duration());
        }

        // Check UNTIL
        if (rrule->endDt().isValid() && !caps.supportsUntil) {
            loss.hasLoss = true;
            loss.countUntilLost = true;
            loss.lostDetails << QStringLiteral("End date (%1) not supported")
                                    .arg(rrule->endDt().date().toString(Qt::ISODate));
        }

        // Check by-rules
        if (!rrule->byDays().isEmpty() && !caps.supportsByDay) {
            loss.hasLoss = true;
            loss.byRulesLost = true;
            loss.lostDetails << QStringLiteral("Specific weekdays (BYDAY) not supported");
        }
        if (!rrule->byMonthDays().isEmpty() && !caps.supportsByMonthDay) {
            loss.hasLoss = true;
            loss.byRulesLost = true;
            loss.lostDetails << QStringLiteral("Specific month days (BYMONTHDAY) not supported");
        }
        if (!rrule->byYearDays().isEmpty() && !caps.supportsByYearDay) {
            loss.hasLoss = true;
            loss.byRulesLost = true;
            loss.lostDetails << QStringLiteral("Specific year days (BYYEARDAY) not supported");
        }
        if (!rrule->byWeekNumbers().isEmpty() && !caps.supportsByWeekNo) {
            loss.hasLoss = true;
            loss.byRulesLost = true;
            loss.lostDetails << QStringLiteral("Specific week numbers (BYWEEKNO) not supported");
        }
        if (!rrule->byMonths().isEmpty() && !caps.supportsByMonth) {
            loss.hasLoss = true;
            loss.byRulesLost = true;
            loss.lostDetails << QStringLiteral("Specific months (BYMONTH) not supported");
        }
        if (!rrule->bySetPos().isEmpty() && !caps.supportsBySetPos) {
            loss.hasLoss = true;
            loss.byRulesLost = true;
            loss.lostDetails << QStringLiteral("Position in set (BYSETPOS) not supported");
        }
    }

    // Check for EXRULEs
    if (!recurrence->exRules().isEmpty() && !caps.supportsExRules) {
        loss.hasLoss = true;
        loss.exceptionsLost = true;
        loss.lostDetails << QStringLiteral("Exclusion rules (EXRULE) not supported");
    }

    // Check for EXDATEs
    if (!recurrence->exDateTimes().isEmpty() && !caps.supportsExDates) {
        loss.hasLoss = true;
        loss.exceptionsLost = true;
        int count = recurrence->exDateTimes().size();
        loss.lostDetails << QStringLiteral("%1 exception date(s) will be lost").arg(count);
    }

    // Check for RDATEs
    if (!recurrence->rDateTimes().isEmpty() && !caps.supportsRDates) {
        loss.hasLoss = true;
        int count = recurrence->rDateTimes().size();
        loss.lostDetails << QStringLiteral("%1 additional date(s) will be lost").arg(count);
    }

    return loss;
}

// ============================================================================
// Binding Metadata Support
// ============================================================================

void SyncBackend::populateBindingMetadata(
    const DiscoveredCalendar &discovered,
    CalendarBackendBinding &binding) const
{
    // Default implementation: copy the metadata map directly
    binding.metadata = discovered.metadata;
}

void SyncBackend::prepareCreationMetadata(
    const QString &calendarId,
    CalendarBackendBinding &binding) const
{
    // Default implementation: no additional metadata
    Q_UNUSED(calendarId);
    Q_UNUSED(binding);
}

// ============================================================================
// Operation-Based API (default implementations)
// ============================================================================

FetchOperation* SyncBackend::fetchItems(const QString &calendarId)
{
    // Default implementation: create a failed operation
    // Subclasses should override this with actual implementation
    auto *op = new FetchOperation(calendarId, this);
    QString errorMsg = QStringLiteral("fetchItems() not implemented by this backend");
    op->fail(errorMsg);
    emit fetchFinished(calendarId, false, errorMsg);
    return op;
}

PushOperation* SyncBackend::pushItems(const QString &calendarId,
                                      const QList<KCalendarCore::Incidence::Ptr> &items,
                                      const TranscodingPlan &plan)
{
    Q_UNUSED(plan);
    auto *op = new PushOperation(calendarId, items, this);
    op->fail(QStringLiteral("pushItems() not implemented by this backend"));
    return op;
}

DeleteOperation* SyncBackend::deleteItems(const QString &calendarId,
                                          const QStringList &uids)
{
    // Default implementation: create a failed operation
    auto *op = new DeleteOperation(calendarId, uids, this);
    op->fail(QStringLiteral("deleteItems() not implemented by this backend"));
    return op;
}

// ============================================================================
// Operation Tracking
// ============================================================================

bool SyncBackend::hasPendingOperations() const
{
    for (auto it = m_pendingOperations.constBegin(); it != m_pendingOperations.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            return true;
        }
    }
    return false;
}

bool SyncBackend::hasPendingOperationsFor(const QString &calendarId) const
{
    return !m_pendingOperations.value(calendarId).isEmpty();
}

QList<SyncOperation*> SyncBackend::pendingOperations() const
{
    QList<SyncOperation*> all;
    for (auto it = m_pendingOperations.constBegin(); it != m_pendingOperations.constEnd(); ++it) {
        all.append(it.value());
    }
    return all;
}

QList<SyncOperation*> SyncBackend::pendingOperationsFor(const QString &calendarId) const
{
    return m_pendingOperations.value(calendarId);
}

void SyncBackend::cancelOperationsFor(const QString &calendarId)
{
    QList<SyncOperation*> ops = m_pendingOperations.value(calendarId);
    for (SyncOperation *op : ops) {
        if (!op->isFinished()) {
            op->cancel();
        }
    }
    // Note: Operations are removed from tracking when they emit finished()
}

void SyncBackend::cancelAllOperations()
{
    for (auto it = m_pendingOperations.begin(); it != m_pendingOperations.end(); ++it) {
        for (SyncOperation *op : it.value()) {
            if (!op->isFinished()) {
                op->cancel();
            }
        }
    }
}

void SyncBackend::registerOperation(SyncOperation *op)
{
    if (!op) return;

    const QString calId = op->calendarId();
    m_pendingOperations[calId].append(op);

    // Auto-unregister when operation finishes
    connect(op, &SyncOperation::finished, this, [this, op]() {
        unregisterOperation(op);
    });
    // Debug log removed - too verbose for normal operation
}

void SyncBackend::unregisterOperation(SyncOperation *op)
{
    if (!op) return;

    const QString calId = op->calendarId();
    QList<SyncOperation*> &ops = m_pendingOperations[calId];
    ops.removeAll(op);

    if (ops.isEmpty()) {
        m_pendingOperations.remove(calId);
    }
    // Debug log removed - too verbose for normal operation
}


// ============================================================================
// IBlobBackend default implementations
// These emit qWarning if invoked before a concrete backend overrides them.
// They cover the build window between Task 10 (hoist) and Tasks 11-18
// (per-backend overrides). Once every backend has its overrides, these
// bodies are only reachable by mistake.
// ============================================================================

// --- Identity / capability (sensible fallbacks, no warning) ---

QString SyncBackend::backendId() const
{
    return backendType();
}

QString SyncBackend::displayName() const
{
    return backendType();
}

bool SyncBackend::isAvailable() const
{
    return true;
}

bool SyncBackend::supportsBatch() const
{
    return false;
}

bool SyncBackend::supportsDeleteTracking() const
{
    return false;
}

// --- Batch (no-op true defaults) ---

void SyncBackend::beginBatch() {}

bool SyncBackend::commitBatch() { return true; }

void SyncBackend::rollbackBatch() {}

// --- Data-path (emit warning and return empty/false) ---

QList<BackendRecord> SyncBackend::loadRecords(const QString &collectionId)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "loadRecords(" << collectionId << ")";
    return {};
}

std::optional<BackendRecord> SyncBackend::loadRecord(const QString &recordId)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "loadRecord(" << recordId << ")";
    return std::nullopt;
}

QString SyncBackend::createRecord(const QString &collectionId, const BackendRecord &record)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "createRecord(" << collectionId << ")";
    Q_UNUSED(record);
    return {};
}

bool SyncBackend::updateRecord(const BackendRecord &record)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "updateRecord";
    Q_UNUSED(record);
    return false;
}

bool SyncBackend::deleteRecord(const QString &recordId)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "deleteRecord(" << recordId << ")";
    return false;
}

QList<BackendRecord> SyncBackend::modifiedSince(const QString &collectionId,
                                                 const QDateTime &since)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "modifiedSince(" << collectionId << ")";
    Q_UNUSED(since);
    return {};
}

QStringList SyncBackend::deletedSince(const QString &collectionId, const QDateTime &since)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "deletedSince(" << collectionId << ")";
    Q_UNUSED(since);
    return {};
}

QList<CollectionInfo> SyncBackend::availableCollections()
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "availableCollections";
    return {};
}

CollectionInfo SyncBackend::collectionInfo(const QString &collectionId)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "collectionInfo(" << collectionId << ")";
    return {};
}

QString SyncBackend::createCollection(const CollectionInfo &info)
{
    qWarning() << "SyncBackend default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "createCollection";
    Q_UNUSED(info);
    return {};
}

} // namespace Kalburator::Sync
