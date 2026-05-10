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

    QStringList unsupportedFreqs;
    if (!supportsHourly)   unsupportedFreqs << QStringLiteral("hourly");
    if (!supportsMinutely) unsupportedFreqs << QStringLiteral("minutely");
    if (!supportsSecondly) unsupportedFreqs << QStringLiteral("secondly");
    if (!unsupportedFreqs.isEmpty()) {
        limitations << QStringLiteral("No %1 recurrence").arg(unsupportedFreqs.join(QStringLiteral("/")));
    }

    if (!supportsByDay && !supportsByMonthDay && !supportsByYearDay &&
        !supportsByWeekNo && !supportsByMonth && !supportsBySetPos) {
        limitations << QStringLiteral("No complex patterns (BYDAY, BYMONTHDAY, etc.)");
    }

    if (!supportsCount && !supportsUntil) {
        limitations << QStringLiteral("No repeat limits (COUNT/UNTIL)");
    }

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
    : SyncBackendBase(parent)
{
}

BackendCapabilities SyncBackend::capabilities() const
{
    // Default implementation: full iCalendar support (LocalBackend-like)
    return BackendCapabilities::localDefaults();
}

RecurrenceCapabilities SyncBackend::recurrenceCapabilities() const
{
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
    caps.maxInterval = 0;
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
        return loss;
    }

    const RecurrenceCapabilities caps = recurrenceCapabilities();
    const KCalendarCore::Recurrence *recurrence = incidence->recurrence();

    const auto rrules = recurrence->rRules();
    if (rrules.size() > 1 && !caps.supportsMultipleRRules) {
        loss.hasLoss = true;
        loss.multipleRulesLost = true;
        loss.lostDetails << QStringLiteral("Multiple recurrence rules will be collapsed to one");
    }

    for (const auto *rrule : rrules) {
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

        if (caps.maxInterval > 0 && rrule->frequency() > caps.maxInterval) {
            loss.hasLoss = true;
            loss.lostDetails << QStringLiteral("Interval %1 exceeds maximum %2")
                                    .arg(rrule->frequency()).arg(caps.maxInterval);
        }

        if (rrule->duration() > 0 && !caps.supportsCount) {
            loss.hasLoss = true;
            loss.countUntilLost = true;
            loss.lostDetails << QStringLiteral("Repeat count (%1 times) not supported")
                                    .arg(rrule->duration());
        }

        if (rrule->endDt().isValid() && !caps.supportsUntil) {
            loss.hasLoss = true;
            loss.countUntilLost = true;
            loss.lostDetails << QStringLiteral("End date (%1) not supported")
                                    .arg(rrule->endDt().date().toString(Qt::ISODate));
        }

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

    if (!recurrence->exRules().isEmpty() && !caps.supportsExRules) {
        loss.hasLoss = true;
        loss.exceptionsLost = true;
        loss.lostDetails << QStringLiteral("Exclusion rules (EXRULE) not supported");
    }

    if (!recurrence->exDateTimes().isEmpty() && !caps.supportsExDates) {
        loss.hasLoss = true;
        loss.exceptionsLost = true;
        int count = recurrence->exDateTimes().size();
        loss.lostDetails << QStringLiteral("%1 exception date(s) will be lost").arg(count);
    }

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
    binding.metadata = discovered.metadata;
}

void SyncBackend::prepareCreationMetadata(
    const QString &calendarId,
    CalendarBackendBinding &binding) const
{
    Q_UNUSED(calendarId);
    Q_UNUSED(binding);
}

// ============================================================================
// Operation-Based API (default implementations)
// ============================================================================

FetchOperation* SyncBackend::fetchItems(const QString &calendarId)
{
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
    auto *op = new DeleteOperation(calendarId, uids, this);
    op->fail(QStringLiteral("deleteItems() not implemented by this backend"));
    return op;
}

} // namespace Kalburator::Sync
