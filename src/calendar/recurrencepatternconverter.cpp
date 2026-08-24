#include "recurrencepatternconverter.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace {

using Kalburator::Calendar::RecurrencePattern::DemoteResult;

// --- shared vocabularies -----------------------------------------------------

// Graph day names → RFC5545 BYDAY codes.
const char* kGraphDayNames[] = {
    "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"
};
const char* kRfcDayCodes[] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };

QString graphDayToRfc(const QString& day)
{
    for (int i = 0; i < 7; ++i)
        if (day == QLatin1String(kGraphDayNames[i]))
            return QLatin1String(kRfcDayCodes[i]);
    return {};
}

QString rfcDayToGraph(const QString& code)
{
    for (int i = 0; i < 7; ++i)
        if (code == QLatin1String(kRfcDayCodes[i]))
            return QLatin1String(kGraphDayNames[i]);
    return {};
}

int rfcDayIndex(const QString& code)
{
    for (int i = 0; i < 7; ++i)
        if (code == QLatin1String(kRfcDayCodes[i]))
            return i;
    return -1;
}

QString graphIndexToBysetpos(const QString& index)
{
    if (index == QLatin1String("first"))   return QStringLiteral("1");
    if (index == QLatin1String("second"))  return QStringLiteral("2");
    if (index == QLatin1String("third"))   return QStringLiteral("3");
    if (index == QLatin1String("fourth"))  return QStringLiteral("4");
    if (index == QLatin1String("last"))    return QStringLiteral("-1");
    return {};
}

QString bysetposToGraphIndex(const QString& pos)
{
    if (pos == QLatin1String("1"))  return QStringLiteral("first");
    if (pos == QLatin1String("2"))  return QStringLiteral("second");
    if (pos == QLatin1String("3"))  return QStringLiteral("third");
    if (pos == QLatin1String("4"))  return QStringLiteral("fourth");
    if (pos == QLatin1String("-1")) return QStringLiteral("last");
    return {};
}

/// RRULE line "RRULE:FREQ=DAILY;INTERVAL=2" → {{"FREQ","DAILY"},{"INTERVAL","2"}}.
QHash<QString, QString> parseRruleParts(const QString& line)
{
    QHash<QString, QString> parts;
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0 || !line.startsWith(QLatin1String("RRULE"), Qt::CaseInsensitive))
        return parts;
    const QString body = line.mid(colon + 1);
    const QStringList segs = body.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& seg : segs) {
        const int eq = seg.indexOf(QLatin1Char('='));
        if (eq > 0)
            parts.insert(seg.left(eq).toUpper(), seg.mid(eq + 1));
    }
    return parts;
}

QJsonObject makePattern()
{
    QJsonObject pattern;
    pattern.insert(QStringLiteral("interval"), 1);
    return pattern;
}

/// Date portion of an ISO datetime/date string ("yyyy-MM-dd").
QString datePortionOf(const QString& iso)
{
    return iso.size() >= 10 ? iso.left(10) : iso;
}

} // namespace

namespace Kalburator::Calendar {
namespace RecurrencePattern {

// ---------------------------------------------------------------------------
// Promote: Graph patternedRecurrence → RFC5545 lines (lossless, §1.3)
// ---------------------------------------------------------------------------

QStringList patternedRecurrenceToRruleLines(const QJsonObject& pr,
                                            const QStringList& cancelledOccurrences,
                                            QStringList* unparsedCancellations)
{
    QStringList lines;
    const QJsonObject pattern = pr.value(QStringLiteral("pattern")).toObject();
    const QJsonObject range = pr.value(QStringLiteral("range")).toObject();
    if (pattern.isEmpty())
        return lines;

    // interval — O57(e): unused numerics serialize as 0; floor at 1.
    int interval = pattern.value(QStringLiteral("interval")).toInt(1);
    if (interval < 1)
        interval = 1;

    const QString type = pattern.value(QStringLiteral("type")).toString();
    QStringList rruleParts;
    QString freq;
    if (type == QLatin1String("daily")) {
        freq = QStringLiteral("DAILY");
    } else if (type == QLatin1String("weekly")) {
        freq = QStringLiteral("WEEKLY");
    } else if (type == QLatin1String("absoluteMonthly")
               || type == QLatin1String("relativeMonthly")) {
        freq = QStringLiteral("MONTHLY");
    } else if (type == QLatin1String("absoluteYearly")
               || type == QLatin1String("relativeYearly")) {
        freq = QStringLiteral("YEARLY");
    } else {
        return lines; // unknown type: emit nothing rather than a wrong rule
    }
    rruleParts << QStringLiteral("FREQ=%1").arg(freq)
               << QStringLiteral("INTERVAL=%1").arg(interval);

    // daysOfWeek (weekly/relative*)
    QStringList dayCodes;
    for (const auto& d : pattern.value(QStringLiteral("daysOfWeek")).toArray()) {
        const QString code = graphDayToRfc(d.toString());
        if (!code.isEmpty() && !dayCodes.contains(code))
            dayCodes << code;
    }

    // Zero-sentinel discipline (O57(e)): 0 means absent.
    const int dayOfMonth = pattern.value(QStringLiteral("dayOfMonth")).toInt();
    const int month = pattern.value(QStringLiteral("month")).toInt();

    // range.startDate — the derivation fallback for missing month/dayOfMonth.
    const QString startDateIso =
        datePortionOf(range.value(QStringLiteral("startDate")).toString());
    const QDate startDate = QDate::fromString(startDateIso, QStringLiteral("yyyy-MM-dd"));

    // `index` is only meaningful on relative* patterns (O57(f)) — ignored
    // elsewhere even when serialized (live weekly patterns carry "first").
    const bool relative = type.startsWith(QLatin1String("relative"));
    const QString bysetpos = relative
        ? graphIndexToBysetpos(pattern.value(QStringLiteral("index")).toString())
        : QString();

    if (type == QLatin1String("weekly")) {
        if (!dayCodes.isEmpty())
            rruleParts << QStringLiteral("BYDAY=%1").arg(dayCodes.join(QLatin1Char(',')));
        const QString wkst = graphDayToRfc(
            pattern.value(QStringLiteral("firstDayOfWeek")).toString());
        if (!wkst.isEmpty())
            rruleParts << QStringLiteral("WKST=%1").arg(wkst);
    } else if (type == QLatin1String("absoluteMonthly")) {
        int dom = dayOfMonth;
        if (dom <= 0 && startDate.isValid())
            dom = startDate.day(); // derive from range.startDate
        if (dom > 0)
            rruleParts << QStringLiteral("BYMONTHDAY=%1").arg(dom);
    } else if (type == QLatin1String("relativeMonthly")) {
        if (!dayCodes.isEmpty())
            rruleParts << QStringLiteral("BYDAY=%1").arg(dayCodes.join(QLatin1Char(',')));
        if (!bysetpos.isEmpty())
            rruleParts << QStringLiteral("BYSETPOS=%1").arg(bysetpos);
    } else if (type == QLatin1String("absoluteYearly")) {
        int m = month;
        if (m <= 0 && startDate.isValid())
            m = startDate.month();
        int dom = dayOfMonth;
        if (dom <= 0 && startDate.isValid())
            dom = startDate.day();
        if (m > 0)
            rruleParts << QStringLiteral("BYMONTH=%1").arg(m);
        if (dom > 0)
            rruleParts << QStringLiteral("BYMONTHDAY=%1").arg(dom);
    } else if (type == QLatin1String("relativeYearly")) {
        int m = month;
        if (m <= 0 && startDate.isValid())
            m = startDate.month();
        if (m > 0)
            rruleParts << QStringLiteral("BYMONTH=%1").arg(m);
        if (!dayCodes.isEmpty())
            rruleParts << QStringLiteral("BYDAY=%1").arg(dayCodes.join(QLatin1Char(',')));
        if (!bysetpos.isEmpty())
            rruleParts << QStringLiteral("BYSETPOS=%1").arg(bysetpos);
    }

    // Range: endDate→UNTIL, numbered→COUNT, noEnd→neither. UNTIL wins when
    // both are present (a bounded end subsumes a count). Both ride INSIDE
    // the RRULE line (RFC5545 grammar) — never as separate lines.
    const QString endDateIso =
        datePortionOf(range.value(QStringLiteral("endDate")).toString());
    // LIVE-CHECKPOINT FINDING (7.B): Graph serializes range.endDate as the
    // .NET year-1 sentinel ("0001-01-01") on numbered ranges instead of
    // omitting it. Treating it as a real UNTIL amputates the series at
    // year 1 — same sentinel discipline as O57(d).
    const QDate endDate =
        endDateIso == QLatin1String("0001-01-01")
            ? QDate()
            : QDate::fromString(endDateIso, QStringLiteral("yyyy-MM-dd"));
    const int count = range.value(QStringLiteral("numberOfOccurrences")).toInt();
    if (endDate.isValid()) {
        rruleParts << QStringLiteral("UNTIL=%1T235959Z")
                          .arg(endDate.toString(QStringLiteral("yyyyMMdd")));
    } else if (count > 0) {
        rruleParts << QStringLiteral("COUNT=%1").arg(count);
    }

    lines << QStringLiteral("RRULE:") + rruleParts.join(QLatin1Char(';'));

    // cancelledOccurrences[] → EXDATE (parseable entries only).
    QStringList exdates;
    for (const QString& occ : cancelledOccurrences) {
        const QDateTime dt = QDateTime::fromString(occ, Qt::ISODate);
        if (dt.isValid()) {
            exdates << dt.toUTC().toString(QStringLiteral("yyyyMMddThhmmssZ"));
        } else if (unparsedCancellations) {
            *unparsedCancellations << occ;
        }
    }
    if (!exdates.isEmpty())
        lines << QStringLiteral("EXDATE:") + exdates.join(QLatin1Char(','));

    return lines;
}

// ---------------------------------------------------------------------------
// Demote: RFC5545 lines → Graph patternedRecurrence (lossy, declared)
// ---------------------------------------------------------------------------

DemoteResult rruleLinesToPatternedRecurrence(const QStringList& lines,
                                             const QString& dtstartDate)
{
    DemoteResult result;

    // First RRULE drives the pattern; everything else is carried verbatim.
    QHash<QString, QString> rr;
    bool found = false;
    for (const QString& line : lines) {
        if (!found && line.startsWith(QLatin1String("RRULE"), Qt::CaseInsensitive)) {
            rr = parseRruleParts(line);
            found = true;
            continue;
        }
        // EXDATE demotes to cancelledOccurrences where computable offline.
        if (line.startsWith(QLatin1String("EXDATE"), Qt::CaseInsensitive)) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                for (const QString& val :
                     line.mid(colon + 1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    const QDateTime dt = QDateTime::fromString(val, QStringLiteral("yyyyMMddThhmmssZ"));
                    result.exdates << (dt.isValid()
                                           ? dt.toUTC().toString(Qt::ISODate)
                                           : val);
                }
            }
            continue;
        }
        result.carriedLines << line; // extra RRULE / EXRULE / RDATE / unknown
    }
    if (!found)
        return result;

    // Feature triage: anything not representable ⇒ carry the FULL original
    // line and reduce the emitted pattern to its nearest representable form.
    const QString freq = rr.value(QStringLiteral("FREQ"));
    static const QSet<QString> kRepresentableFreqs = {
        QStringLiteral("DAILY"), QStringLiteral("WEEKLY"),
        QStringLiteral("MONTHLY"), QStringLiteral("YEARLY")
    };
    bool reduced = false;

    const QString untilRaw = rr.value(QStringLiteral("UNTIL"));
    const QString countRaw = rr.value(QStringLiteral("COUNT"));
    const QStringList bydayList =
        rr.value(QStringLiteral("BYDAY")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList bymonthdayList =
        rr.value(QStringLiteral("BYMONTHDAY")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList bymonthList =
        rr.value(QStringLiteral("BYMONTH")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList bysetposList =
        rr.value(QStringLiteral("BYSETPOS")).split(QLatin1Char(','), Qt::SkipEmptyParts);

    // Sub-daily FREQ → nearest daily (declared Simplified).
    if (!kRepresentableFreqs.contains(freq)) {
        reduced = true;
    }
    // BYWEEKNO / BYYEARDAY → dropped from pattern (declared Simplified).
    if (rr.contains(QStringLiteral("BYWEEKNO"))
        || rr.contains(QStringLiteral("BYYEARDAY")))
        reduced = true;
    // Friday-the-13th intersection (BYMONTHDAY+BYDAY together).
    if (!bymonthdayList.isEmpty() && !bydayList.isEmpty()
        && !(freq == QLatin1String("WEEKLY")))
        reduced = true;
    // General BYSETPOS (multi-value, or single value outside index semantics
    // on non-relative patterns).
    if (bysetposList.size() > 1)
        reduced = true;
    // Multi-value BYMONTHDAY / BYMONTH → first value emitted (declared).
    if (bymonthdayList.size() > 1 || bymonthList.size() > 1)
        reduced = true;
    // Ordinal-prefixed BYDAY codes outside weekly (e.g. monthly BYDAY=2MO with
    // no BYSETPOS) cannot map onto index cleanly unless BYSETPOS carries it.
    // Plain BYDAY on MONTHLY/YEARLY without BYSETPOS means "every <day>" —
    // relative*+index:first is NOT equivalent — so reduce + carry too.
    if (freq != QLatin1String("WEEKLY") && !bydayList.isEmpty()
        && bysetposList.isEmpty())
        reduced = true;
    // WKST is only representable on weekly.
    if (rr.contains(QStringLiteral("WKST")) && freq != QLatin1String("WEEKLY"))
        reduced = true;
    if (reduced)
        result.carriedLines.prepend(lines.first());

    // Multiple RRULE lines: the first drives the pattern, ALL are carried
    // (declared Simplified) so re-promote sees the complete set.
    const int rruleCount = std::count_if(lines.cbegin(), lines.cend(),
        [](const QString& l) { return l.startsWith(QLatin1String("RRULE"),
                                                Qt::CaseInsensitive); });
    if (rruleCount > 1 && !result.carriedLines.contains(lines.first()))
        result.carriedLines.prepend(lines.first());

    // ---- build pattern -------------------------------------------------------
    QJsonObject pattern = makePattern();
    pattern.insert(QStringLiteral("interval"),
                   qMax(1, rr.value(QStringLiteral("INTERVAL")).toInt()));

    const QString startDate = dtstartDate.isEmpty()
        ? QStringLiteral("1970-01-01") : datePortionOf(dtstartDate);

    if (freq == QLatin1String("DAILY") || !kRepresentableFreqs.contains(freq)) {
        pattern.insert(QStringLiteral("type"), QStringLiteral("daily"));
    } else if (freq == QLatin1String("WEEKLY")) {
        pattern.insert(QStringLiteral("type"), QStringLiteral("weekly"));
        if (!bydayList.isEmpty()) {
            QJsonArray days;
            for (const QString& code : bydayList) {
                const QString day = rfcDayToGraph(code.right(2));
                if (!day.isEmpty())
                    days.append(day);
            }
            if (!days.isEmpty())
                pattern.insert(QStringLiteral("daysOfWeek"), days);
        }
        if (rr.contains(QStringLiteral("WKST"))) {
            const QString wkst = rfcDayToGraph(rr.value(QStringLiteral("WKST")));
            if (!wkst.isEmpty())
                pattern.insert(QStringLiteral("firstDayOfWeek"), wkst);
        }
    } else if (freq == QLatin1String("MONTHLY")) {
        if (!bymonthdayList.isEmpty()) {
            pattern.insert(QStringLiteral("type"), QStringLiteral("absoluteMonthly"));
            pattern.insert(QStringLiteral("dayOfMonth"), bymonthdayList.first().toInt());
        } else if (!bydayList.isEmpty()) {
            pattern.insert(QStringLiteral("type"), QStringLiteral("relativeMonthly"));
            QJsonArray days;
            for (const QString& code : bydayList) {
                const QString day = rfcDayToGraph(code.right(2));
                if (!day.isEmpty())
                    days.append(day);
            }
            pattern.insert(QStringLiteral("daysOfWeek"), days);
            // BYSETPOS ↔ index when it has index semantics; plain-BYDAY
            // monthly ("every Monday of the month") reduces to first + carry.
            if (bysetposList.size() == 1) {
                const QString idx = bysetposToGraphIndex(bysetposList.first());
                if (!idx.isEmpty())
                    pattern.insert(QStringLiteral("index"), idx);
            } else {
                pattern.insert(QStringLiteral("index"), QStringLiteral("first"));
            }
        } else {
            // Bare FREQ=MONTHLY → same-date monthly from DTSTART.
            pattern.insert(QStringLiteral("type"), QStringLiteral("absoluteMonthly"));
            const QDate sd = QDate::fromString(startDate, QStringLiteral("yyyy-MM-dd"));
            if (sd.isValid())
                pattern.insert(QStringLiteral("dayOfMonth"), sd.day());
        }
    } else { // YEARLY
        const QDate sd = QDate::fromString(startDate, QStringLiteral("yyyy-MM-dd"));
        if (!bydayList.isEmpty() && bysetposList.size() == 1) {
            pattern.insert(QStringLiteral("type"), QStringLiteral("relativeYearly"));
            if (!bymonthList.isEmpty())
                pattern.insert(QStringLiteral("month"), bymonthList.first().toInt());
            else if (sd.isValid())
                pattern.insert(QStringLiteral("month"), sd.month());
            QJsonArray days;
            for (const QString& code : bydayList) {
                const QString day = rfcDayToGraph(code.right(2));
                if (!day.isEmpty())
                    days.append(day);
            }
            pattern.insert(QStringLiteral("daysOfWeek"), days);
            const QString idx = bysetposToGraphIndex(bysetposList.first());
            pattern.insert(QStringLiteral("index"),
                           idx.isEmpty() ? QStringLiteral("first") : idx);
        } else if (!bymonthList.isEmpty() && !bymonthdayList.isEmpty()) {
            pattern.insert(QStringLiteral("type"), QStringLiteral("absoluteYearly"));
            pattern.insert(QStringLiteral("month"), bymonthList.first().toInt());
            pattern.insert(QStringLiteral("dayOfMonth"), bymonthdayList.first().toInt());
        } else {
            pattern.insert(QStringLiteral("type"), QStringLiteral("absoluteYearly"));
            if (sd.isValid()) {
                pattern.insert(QStringLiteral("month"), sd.month());
                pattern.insert(QStringLiteral("dayOfMonth"), sd.day());
            }
        }
    }

    // ---- build range -----------------------------------------------------------
    QJsonObject range;
    range.insert(QStringLiteral("startDate"), startDate);
    // recurrenceTimeZone deliberately NOT emitted: zone vocabulary lives on
    // start/end (declared loss profile); a constant here would break
    // promote⇄demote convergence on patterns that never carried one.
    if (!untilRaw.isEmpty()) {
        // UNTIL=20261231T235959Z or UNTIL=20261231.
        const QString digits = untilRaw.left(qMin(untilRaw.size(), 8));
        const QDate d = QDate::fromString(digits, QStringLiteral("yyyyMMdd"));
        if (d.isValid()) {
            range.insert(QStringLiteral("type"), QStringLiteral("endDate"));
            range.insert(QStringLiteral("endDate"),
                         d.toString(QStringLiteral("yyyy-MM-dd")));
        } else {
            range.insert(QStringLiteral("type"), QStringLiteral("noEnd"));
        }
    } else if (!countRaw.isEmpty() && countRaw.toInt() > 0) {
        range.insert(QStringLiteral("type"), QStringLiteral("numbered"));
        range.insert(QStringLiteral("numberOfOccurrences"), countRaw.toInt());
    } else {
        range.insert(QStringLiteral("type"), QStringLiteral("noEnd"));
    }

    QJsonObject pr;
    pr.insert(QStringLiteral("pattern"), pattern);
    pr.insert(QStringLiteral("range"), range);
    result.patternedRecurrence = pr;
    return result;
}

} // namespace RecurrencePattern
} // namespace Kalburator::Calendar
