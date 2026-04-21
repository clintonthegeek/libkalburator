#ifndef RRULETRANSCODER_H
#define RRULETRANSCODER_H

#include "propertytranscoder.h"

namespace Kalburator::Sync {

/**
 * @brief Transcoder for complex RRULE patterns to simpler backends.
 *
 * Some backends (like org-mode) only support simple repeat patterns
 * (daily, weekly, monthly, yearly) and cannot handle:
 * - BYDAY (specific days of week)
 * - BYMONTHDAY (specific day of month)
 * - BYSETPOS (e.g., "last weekday")
 * - Multiple RRULEs
 * - EXRULE, RDATE, EXDATE
 *
 * This transcoder detects complex RRULEs and:
 * 1. Simplifies them to the closest supported pattern
 * 2. Preserves the original RRULE as an X-property for restoration
 * 3. Warns the user about data loss
 */
class RRuleTranscoder : public PropertyTranscoder
{
public:
    RRuleTranscoder() = default;

    QString propertyName() const override { return QStringLiteral("RRULE"); }
    QString sourceBackendType() const override { return QStringLiteral("*"); }
    QString targetBackendType() const override { return QStringLiteral("orgmode"); }
    TranscodingFidelity fidelity() const override { return TranscodingFidelity::Lossy; }

    bool transcode(KCalendarCore::Incidence::Ptr &incidence) const override;

    QString description() const override {
        return QStringLiteral("Complex recurrence simplified for org-mode (original preserved in X-ORIGINAL-RRULE)");
    }

    int priority() const override { return 10; }  // Run before other RRULE transcoders

private:
    /**
     * @brief Check if the recurrence is too complex for org-mode.
     */
    bool isComplexRecurrence(const KCalendarCore::Incidence::Ptr &incidence) const;

    /**
     * @brief Simplify recurrence to a basic pattern.
     */
    void simplifyRecurrence(KCalendarCore::Incidence::Ptr &incidence) const;
};

/**
 * @brief Reverse transcoder to restore complex RRULE from X-property.
 */
class RRuleReverseTranscoder : public PropertyTranscoder
{
public:
    RRuleReverseTranscoder() = default;

    QString propertyName() const override { return QStringLiteral("RRULE"); }
    QString sourceBackendType() const override { return QStringLiteral("orgmode"); }
    QString targetBackendType() const override { return QStringLiteral("*"); }
    TranscodingFidelity fidelity() const override { return TranscodingFidelity::Lossless; }

    bool transcode(KCalendarCore::Incidence::Ptr &incidence) const override;

    QString description() const override {
        return QStringLiteral("Restores original complex RRULE from X-ORIGINAL-RRULE");
    }

    int priority() const override { return 10; }
};

} // namespace Kalburator::Sync

#endif // RRULETRANSCODER_H
