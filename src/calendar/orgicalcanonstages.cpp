#include "orgicalcanonstages.h"
#include "icalcanonstages.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/Recurrence>
#include <KCalendarCore/RecurrenceRule>

namespace {

// ---------------------------------------------------------------------------
// Helpers ported from src/transcoding/rruletranscoder.cpp (do not import
// from that file — it lives in a different namespace and is deleted later).
// We port ONLY the structural inspection / simplification logic.
// The stash/restore of RRULE text uses verbatim iCal bytes rather than
// ICalFormat::toString/fromString (the latter does not round-trip reliably
// for RecurrenceRule objects — fromString always fails in practice; see
// FINDINGS).  Custom property key pair ("X-ORIGINAL","RRULE") mirrors the
// original so the serialized iCal property name is "X-ORIGINAL-RRULE".
// ---------------------------------------------------------------------------

bool isComplexRecurrence(const KCalendarCore::Incidence::Ptr &incidence)
{
    if (!incidence)
        return false;

    KCalendarCore::Recurrence *recurrence = incidence->recurrence();
    if (!recurrence ||
        recurrence->recurrenceType() == KCalendarCore::Recurrence::rNone)
        return false;

    const auto rules = recurrence->rRules();

    // Multiple RRULEs = complex
    if (rules.size() > 1)
        return true;

    // Check for by-rules that org-mode doesn't support
    for (const auto *rule : rules) {
        if (!rule->byDays().isEmpty() ||
            !rule->byMonthDays().isEmpty() ||
            !rule->byYearDays().isEmpty() ||
            !rule->byWeekNumbers().isEmpty() ||
            !rule->byMonths().isEmpty() ||
            !rule->bySetPos().isEmpty())
            return true;
    }

    // RDATEs or EXDATEs = complex
    if (!recurrence->rDateTimes().isEmpty() ||
        !recurrence->exDateTimes().isEmpty())
        return true;

    return false;
}

void simplifyRecurrence(KCalendarCore::Incidence::Ptr &incidence)
{
    if (!incidence)
        return;

    KCalendarCore::Recurrence *recurrence = incidence->recurrence();
    if (!recurrence)
        return;

    // Capture primary recurrence parameters before clearing
    const ushort type      = recurrence->recurrenceType();
    const int interval     = recurrence->frequency();
    const int duration     = recurrence->duration();
    const QDateTime endDate = recurrence->endDateTime();

    recurrence->clear();

    // Values from KCalendarCore::Recurrence:
    // rNone=0, rMinutely=1, rHourly=2, rDaily=3, rWeekly=4,
    // rMonthlyPos=5, rMonthlyDay=6, rYearlyMonth=7, rYearlyDay=8, rYearlyPos=9
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
        recurrence->setDaily(1);
        break;
    }

    if (duration > 0)
        recurrence->setDuration(duration);
    else if (endDate.isValid())
        recurrence->setEndDateTime(endDate);
}

/// Collect verbatim RRULE/RDATE/EXDATE lines from raw iCal bytes (same
/// approach as extractRecurrenceLines in icalcanonstages.cpp).
QStringList extractRecurrenceLinesLocal(const QByteArray &icalBytes)
{
    QStringList lines;
    const auto text = QString::fromUtf8(icalBytes);
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("RRULE:"))  ||
            line.startsWith(QStringLiteral("RDATE:"))  ||
            line.startsWith(QStringLiteral("EXDATE:")))
            lines.append(line);
    }
    return lines;
}

/// Parse an iCal byte string to an Event::Ptr.
KCalendarCore::Event::Ptr parseEvent(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(data));
    return inc.dynamicCast<KCalendarCore::Event>();
}

/// Serialize an Event::Ptr back to iCal bytes.
QByteArray serializeEvent(const KCalendarCore::Event::Ptr &event)
{
    if (!event)
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(event).toUtf8();
}

/// Remove all lines matching any of the given prefixes from iCal bytes.
/// Works line-by-line on the raw bytes.
QByteArray removeLinesWithPrefixes(const QByteArray &icalBytes,
                                   const QList<QByteArray> &prefixes)
{
    QByteArray result;
    const auto lines = icalBytes.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray trimmed = rawLine.trimmed();
        bool remove = false;
        for (const QByteArray &prefix : prefixes) {
            if (trimmed.startsWith(prefix)) {
                remove = true;
                break;
            }
        }
        if (!remove) {
            result += rawLine;
            result += '\n';
        }
    }
    // Trim a trailing newline we may have added
    if (result.endsWith('\n'))
        result.chop(1);
    return result;
}

/// Inject iCal lines immediately before END:VEVENT.
QByteArray injectBeforeEndVevent(const QByteArray &icalBytes,
                                 const QStringList &lines)
{
    if (lines.isEmpty())
        return icalBytes;

    const QByteArray marker = "END:VEVENT";
    const int pos = icalBytes.indexOf(marker);
    if (pos < 0)
        return icalBytes;

    QByteArray injection;
    for (const QString &line : lines) {
        injection += line.toUtf8();
        injection += '\n';
    }

    QByteArray result = icalBytes;
    result.insert(pos, injection);
    return result;
}

} // namespace

namespace Kalburator::Calendar {

// ---------------------------------------------------------------------------
// CanonToOrgICalStage
// canon JSON → org-ical bytes (with RRULE simplification)
// ---------------------------------------------------------------------------

QByteArray CanonToOrgICalStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};

    // (1) Produce standard iCal bytes from canon (recurrence injected as
    //     verbatim RRULE text per invariant 3).
    const QByteArray icalBytes = CanonToICalStage{}.transform(canonBytes);
    if (icalBytes.isEmpty())
        return {};

    // (2) Capture verbatim RRULE/RDATE/EXDATE lines BEFORE any parsing.
    //     These are what we stash for the reverse direction.
    const QStringList origRecLines = extractRecurrenceLinesLocal(icalBytes);

    // (3) Parse to Event — invariant-3-sanctioned recurrence parse.
    auto event = parseEvent(icalBytes);
    if (!event)
        return {};

    // (4) Check complexity.
    KCalendarCore::Incidence::Ptr inc = event.staticCast<KCalendarCore::Incidence>();
    if (!isComplexRecurrence(inc)) {
        // Simple recurrence — no simplification needed, return ical as-is.
        return icalBytes;
    }

    // (5) Stash the verbatim original recurrence lines joined by the pipe
    //     character '|' (does not appear in iCal RRULE/RDATE/EXDATE text).
    if (!origRecLines.isEmpty()) {
        const QString stash = origRecLines.join(QLatin1Char('|'));
        inc->setCustomProperty("X-ORIGINAL", "RRULE", stash);
    }

    // (6) Simplify the recurrence.
    simplifyRecurrence(inc);

    // (7) Re-serialize.
    return serializeEvent(event);
}

// ---------------------------------------------------------------------------
// OrgICalToCanonStage
// org-ical bytes → canon JSON (restoring stashed RRULE before encoding)
// ---------------------------------------------------------------------------

QByteArray OrgICalToCanonStage::transform(const QByteArray& orgIcalBytes) const
{
    if (orgIcalBytes.isEmpty())
        return {};

    // (1) Parse to Event to read the X-ORIGINAL-RRULE custom property.
    auto event = parseEvent(orgIcalBytes);
    if (!event)
        return {};

    const QString stash = event->customProperty("X-ORIGINAL", "RRULE");

    if (stash.isEmpty()) {
        // No stash — no simplification was applied; pass directly to canon stage.
        return ICalToCanonStage{}.transform(orgIcalBytes);
    }

    // (2) Recover the original verbatim recurrence lines.
    const QStringList origRecLines = stash.split(QLatin1Char('|'));

    // (3) Byte-level manipulation of the iCal text:
    //   (a) Remove simplified RRULE/RDATE/EXDATE lines
    //   (b) Remove the X-ORIGINAL-RRULE property line
    //   (c) Inject the original recurrence lines before END:VEVENT
    QByteArray modified = removeLinesWithPrefixes(orgIcalBytes, {
        "RRULE:", "RDATE:", "EXDATE:", "X-ORIGINAL-RRULE:"
    });
    modified = injectBeforeEndVevent(modified, origRecLines);

    // (4) Run ICalToCanonStage — captures the restored RRULE verbatim per
    //     invariant 3 (extractRecurrenceLines in that stage reads the text).
    return ICalToCanonStage{}.transform(modified);
}

}  // namespace Kalburator::Calendar
