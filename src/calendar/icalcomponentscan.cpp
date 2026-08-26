#include "icalcomponentscan.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>

namespace Kalburator::Calendar {

namespace {

/// Parse an iCal DATE-TIME value. Qt's Qt::ISODate parser only accepts the
/// EXTENDED form ("2026-06-02T09:00:00Z"); RFC 5545 wires BASIC form
/// ("20260602T090000Z", UTC suffix 'Z' — embedded numeric offsets are not
/// legal iCal), so try both. Floating values come back as Qt::LocalTime wall
/// time; callers decide which zone to read them in (O60 house rule).
QDateTime parseICalDateTimeValue(const QString &value)
{
    QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
    if (dt.isValid())
        return dt;
    if (value.endsWith(QLatin1Char('Z'))) {
        dt = QDateTime::fromString(value.left(value.size() - 1),
                                   QStringLiteral("yyyyMMddTHHmmss"));
        if (dt.isValid())
            return QDateTime(dt.date(), dt.time(), QTimeZone::utc());
    }
    return QDateTime::fromString(value, QStringLiteral("yyyyMMddTHHmmss"));
}

/// Normalize a RECURRENCE-ID property line's value to UTC ISO-8601 so
/// selector matching is zone-form-independent. `propertyLine` is the whole
/// logical line ("RECURRENCE-ID[;PARAMS]:VALUE"). DATE-form values
/// (VALUE=DATE, "YYYYMMDD") normalize to UTC midnight of that date; floating
/// date-time values (no zone designator) are read as UTC wall time directly
/// IN the target zone (O60 house rule — never via LocalTime round-trip).
/// Returns an empty string when the value cannot be parsed.
QString normalizeRecurrenceIdLine(const QString &propertyLine)
{
    const int colon = propertyLine.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return {};
    const QString value = propertyLine.mid(colon + 1).trimmed();

    // Honor a TZID parameter (possibly quoted) — a floating-looking value
    // with TZID is zone-qualified, not UTC wall time.
    QTimeZone tz = QTimeZone::utc();
    {
        static const QRegularExpression tzRe(
            QStringLiteral("TZID=\"?([^\"]+)\"?"));
        const auto m = tzRe.match(propertyLine.left(colon));
        if (m.hasMatch()) {
            const QTimeZone parsed(m.captured(1).toLatin1());
            if (parsed.isValid())
                tz = parsed;
        }
    }

    // VALUE=DATE form: bare YYYYMMDD → UTC midnight of that date
    // (DATE values carry no zone designator).
    if (value.size() == 8 && !value.contains(QLatin1Char('T'))) {
        const QDate d = QDate::fromString(value, QStringLiteral("yyyyMMdd"));
        if (!d.isValid())
            return {};
        return QDateTime(d, QTime(0, 0), QTimeZone::utc()).toUTC().toString(Qt::ISODate);
    }

    QDateTime dt = parseICalDateTimeValue(value);
    if (!dt.isValid())
        return {};
    if (dt.timeSpec() == Qt::LocalTime)
        dt = QDateTime(dt.date(), dt.time(), tz);
    return dt.toUTC().toString(Qt::ISODate);
}

} // namespace

QStringList extractComponentRecurrenceLines(const QByteArray &icalBytes,
                                             QByteArrayView componentName,
                                             const QString &uid,
                                             const QString &recurrenceIdUtc)
{
    // Unfold: normalize CRLF to LF, then join any continuation line (one
    // starting with a space or tab) onto its predecessor (RFC 5545 §3.1)
    // before splitting into logical lines.
    QString text = QString::fromUtf8(icalBytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    static const QRegularExpression foldRe(QStringLiteral("\\n[ \\t]"));
    text.replace(foldRe, QString());
    const QStringList allLines = text.split(QLatin1Char('\n'));

    const QString name       = QString::fromUtf8(componentName.toByteArray());
    const QString wantedBegin = QStringLiteral("BEGIN:") + name;
    const QString wantedEnd   = QStringLiteral("END:") + name;

    QList<QStringList> masterCandidates;  // matching-uid blocks, no RECURRENCE-ID
    QList<QStringList> allCandidates;      // matching-uid blocks, any shape

    bool inTarget = false;
    bool blockUidMatches = false;
    bool blockHasRecurrenceId = false;
    QString blockRecurrenceIdUtc;
    QStringList currentBlock;

    // Selector mode: first matching-uid block whose normalized RECURRENCE-ID
    // equals the requested value wins outright.
    QStringList selectedBlock;

    int vtimezoneDepth = 0;
    int valarmDepth = 0;

    for (const QString &raw : allLines) {
        const QString line = raw.trimmed();

        if (line == QStringLiteral("BEGIN:VTIMEZONE")) { ++vtimezoneDepth; continue; }
        if (line == QStringLiteral("END:VTIMEZONE")) {
            if (vtimezoneDepth > 0) --vtimezoneDepth;
            continue;
        }
        if (vtimezoneDepth > 0)
            continue;  // never look inside VTIMEZONE (its STANDARD/DAYLIGHT
                       // sub-blocks may carry their own RRULE — N1).

        if (!inTarget) {
            if (line == wantedBegin) {
                inTarget = true;
                currentBlock.clear();
                blockUidMatches = false;
                blockHasRecurrenceId = false;
                blockRecurrenceIdUtc.clear();
                valarmDepth = 0;
            }
            continue;
        }

        if (line == QStringLiteral("BEGIN:VALARM")) { ++valarmDepth; continue; }
        if (line == QStringLiteral("END:VALARM")) {
            if (valarmDepth > 0) --valarmDepth;
            continue;
        }
        if (valarmDepth > 0)
            continue;  // skip nested VALARM body

        if (line == wantedEnd) {
            inTarget = false;
            if (blockUidMatches) {
                if (blockHasRecurrenceId)
                    allCandidates.append(currentBlock);
                else
                    masterCandidates.append(currentBlock);

                if (!recurrenceIdUtc.isEmpty()
                    && blockHasRecurrenceId
                    && blockRecurrenceIdUtc == recurrenceIdUtc
                    && selectedBlock.isEmpty()) {
                    selectedBlock = currentBlock;
                }
            }
            continue;
        }

        if (line.startsWith(QStringLiteral("UID:")) &&
            line.mid(4).trimmed() == uid)
            blockUidMatches = true;
        if (line.startsWith(QStringLiteral("RECURRENCE-ID"))) {
            blockHasRecurrenceId = true;
            blockRecurrenceIdUtc = normalizeRecurrenceIdLine(line);
        }

        if (line.startsWith(QStringLiteral("RRULE:")) ||
            line.startsWith(QStringLiteral("RDATE:")) ||
            line.startsWith(QStringLiteral("EXDATE:")))
            currentBlock.append(line);
    }

    if (!recurrenceIdUtc.isEmpty())
        return selectedBlock;

    if (!masterCandidates.isEmpty())
        return masterCandidates.first();

    QStringList concatenated;
    for (const auto &block : allCandidates)
        concatenated += block;
    return concatenated;
}

}  // namespace Kalburator::Calendar
