#include "rruletranscoder.h"

#include <KCalendarCore/Recurrence>
#include <KCalendarCore/RecurrenceRule>
#include <KCalendarCore/ICalFormat>

bool RRuleTranscoder::transcode(KCalendarCore::Incidence::Ptr &incidence) const
{
    if (!incidence) {
        return false;
    }

    // Only transcode if there's a complex recurrence
    if (!isComplexRecurrence(incidence)) {
        return false;
    }

    // Preserve the original RRULE as X-property
    KCalendarCore::ICalFormat format;
    QString originalRRule;

    KCalendarCore::Recurrence *recurrence = incidence->recurrence();
    if (recurrence) {
        const auto rules = recurrence->rRules();
        for (const auto *rule : rules) {
            if (!originalRRule.isEmpty()) {
                originalRRule += QLatin1Char(';');
            }
            // Clone the rule to get a non-const pointer for toString
            KCalendarCore::RecurrenceRule ruleCopy(*rule);
            originalRRule += format.toString(&ruleCopy);
        }

        if (!originalRRule.isEmpty()) {
            incidence->setCustomProperty("X-ORIGINAL", "RRULE", originalRRule);
        }
    }

    // Simplify the recurrence
    simplifyRecurrence(incidence);

    return true;
}

bool RRuleTranscoder::isComplexRecurrence(const KCalendarCore::Incidence::Ptr &incidence) const
{
    if (!incidence) {
        return false;
    }

    KCalendarCore::Recurrence *recurrence = incidence->recurrence();
    if (!recurrence || recurrence->recurrenceType() == KCalendarCore::Recurrence::rNone) {
        return false;
    }

    const auto rules = recurrence->rRules();

    // Multiple RRULEs = complex
    if (rules.size() > 1) {
        return true;
    }

    // Check for by-rules that org-mode doesn't support
    for (const auto *rule : rules) {
        if (!rule->byDays().isEmpty() ||
            !rule->byMonthDays().isEmpty() ||
            !rule->byYearDays().isEmpty() ||
            !rule->byWeekNumbers().isEmpty() ||
            !rule->byMonths().isEmpty() ||
            !rule->bySetPos().isEmpty()) {
            return true;
        }
    }

    // RDATEs or EXDATEs = complex
    if (!recurrence->rDateTimes().isEmpty() ||
        !recurrence->exDateTimes().isEmpty()) {
        return true;
    }

    return false;
}

void RRuleTranscoder::simplifyRecurrence(KCalendarCore::Incidence::Ptr &incidence) const
{
    if (!incidence) {
        return;
    }

    KCalendarCore::Recurrence *recurrence = incidence->recurrence();
    if (!recurrence) {
        return;
    }

    // Get the primary recurrence type and interval
    ushort type = recurrence->recurrenceType();
    int interval = recurrence->frequency();
    int duration = recurrence->duration();
    QDateTime endDate = recurrence->endDateTime();

    // Clear everything and create a simple recurrence
    recurrence->clear();

    // Set up simple recurrence based on primary type
    // Values from KCalendarCore::Recurrence: rNone=0, rMinutely=1, rHourly=2, rDaily=3, rWeekly=4, rMonthlyPos=5, rMonthlyDay=6, rYearlyMonth=7, rYearlyDay=8, rYearlyPos=9
    switch (type) {
    case 3:  // rDaily
        recurrence->setDaily(interval);
        break;
    case 4:  // rWeekly
        recurrence->setWeekly(interval);
        break;
    case 5:  // rMonthlyPos
    case 6:  // rMonthlyDay
        recurrence->setMonthly(interval);
        break;
    case 7:  // rYearlyMonth
    case 8:  // rYearlyDay
    case 9:  // rYearlyPos
        recurrence->setYearly(interval);
        break;
    default:
        // For unsupported types, default to daily
        recurrence->setDaily(1);
        break;
    }

    // Restore duration or end date
    if (duration > 0) {
        recurrence->setDuration(duration);
    } else if (endDate.isValid()) {
        recurrence->setEndDateTime(endDate);
    }
}

bool RRuleReverseTranscoder::transcode(KCalendarCore::Incidence::Ptr &incidence) const
{
    if (!incidence) {
        return false;
    }

    // Check for preserved original RRULE
    QString originalRRule = incidence->customProperty("X-ORIGINAL", "RRULE");
    if (originalRRule.isEmpty()) {
        return false;  // Nothing to restore
    }

    // Parse and restore the original RRULE
    KCalendarCore::ICalFormat format;
    KCalendarCore::Recurrence *recurrence = incidence->recurrence();

    if (recurrence) {
        recurrence->clear();

        // Split multiple RRULEs
        const QStringList rules = originalRRule.split(QLatin1Char(';'));
        for (const QString &ruleStr : rules) {
            if (ruleStr.isEmpty()) continue;

            // Parse RRULE string
            KCalendarCore::RecurrenceRule rule;
            if (format.fromString(&rule, ruleStr)) {
                recurrence->addRRule(&rule);
            }
        }
    }

    // Remove the X-property
    incidence->removeCustomProperty("X-ORIGINAL", "RRULE");

    return true;
}
