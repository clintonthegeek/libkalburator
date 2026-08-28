#include "todoseriessplitter.h"

#include "recurrencepatternconverter.h"

#include <QDate>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>

namespace {

using Kalburator::Calendar::RecurrencePattern::parseRruleParts;

/// Same sanitization algorithm as
/// RemoteCalendarBackend::generateItemUrlForCreate
/// (src/calendar/remotecalendarbackend.cpp:869-879): strip non-alphanumerics
/// from the UTC-ISO stamp so the result is URL/filesystem-safe and stable
/// across zone forms of the same instant.
QString sanitizedUtcIsoStamp(const QDateTime &instant)
{
    const QString stamp = instant.toUTC().toString(Qt::ISODate);
    QString sanitized;
    sanitized.reserve(stamp.size());
    for (const QChar c : stamp) {
        if (c.isLetterOrNumber())
            sanitized += c;
    }
    return sanitized;
}

/// Find the (first) RRULE line's index in a canon `recurrence` array, or -1.
int findRruleLineIndex(const QJsonArray &recurrence)
{
    for (int i = 0; i < recurrence.size(); ++i) {
        const QString line = recurrence.at(i).toString();
        if (line.startsWith(QStringLiteral("RRULE"), Qt::CaseInsensitive)
            && line.contains(QLatin1Char(':')))
            return i;
    }
    return -1;
}

/// UNTIL value is DATE-only ("yyyyMMdd", no 'T') vs DATE-TIME
/// ("yyyyMMddThhmmssZ"). RFC5545 requires UNTIL's value type to match
/// DTSTART's; we detect from the token itself when present.
bool isDateOnlyUntilToken(const QString &untilRaw)
{
    static const QRegularExpression dateOnly(QStringLiteral(R"(^\d{8}$)"));
    return dateOnly.match(untilRaw).hasMatch();
}

/// Whether the master's own start/due shape is date-only (allDay), used as
/// the value-type fallback when the RRULE carries no existing UNTIL to sniff.
bool masterIsDateOnly(const QJsonObject &masterCanon)
{
    for (const char *key : { "start", "due" }) {
        const QJsonObject obj = masterCanon.value(QLatin1String(key)).toObject();
        if (obj.contains(QStringLiteral("date")))
            return true;
        if (obj.contains(QStringLiteral("dateTime")))
            return false;
    }
    return false;
}

/// Parse an RFC5545 UNTIL token (either style) to a UTC QDateTime for
/// min()-comparison purposes. DATE-only tokens compare at end-of-day UTC
/// (23:59:59) so "never loosens past the original bound" behaves correctly
/// against a DATE-TIME splitInstant.
QDateTime parseUntilToUtc(const QString &untilRaw)
{
    if (untilRaw.isEmpty())
        return {};
    if (isDateOnlyUntilToken(untilRaw)) {
        const QDate d = QDate::fromString(untilRaw, QStringLiteral("yyyyMMdd"));
        return d.isValid() ? QDateTime(d, QTime(23, 59, 59), QTimeZone::utc()) : QDateTime{};
    }
    QString norm = untilRaw;
    if (norm.endsWith(QLatin1Char('Z')))
        norm.chop(1);
    const QDateTime dt = QDateTime::fromString(norm, QStringLiteral("yyyyMMddThhmmss"));
    if (!dt.isValid())
        return {};
    return QDateTime(dt.date(), dt.time(), QTimeZone::utc());
}

/// Text-level UNTIL rewrite: find/replace the `UNTIL=...` token inside the
/// verbatim RRULE line string (Open decision 6), preserving every other
/// byte of the line exactly. Inserts `;UNTIL=<value>` at the end when no
/// UNTIL token was present (unbounded RRULE).
QString rewriteUntilInRruleLine(const QString &line, const QString &newUntilValue)
{
    static const QRegularExpression untilToken(QStringLiteral(";UNTIL=[^;]*"));
    if (line.contains(QStringLiteral("UNTIL="))) {
        QString result = line;
        result.replace(untilToken, QStringLiteral(";UNTIL=") + newUntilValue);
        return result;
    }
    return line + QStringLiteral(";UNTIL=") + newUntilValue;
}

/// Retime a canon {date,allDay} or {dateTime,tz,floating} object to
/// `splitInstant`, preserving the original's shape (date-only vs
/// date-time, floating/tz flags untouched).
QJsonObject retimeCanonDateTimeField(const QJsonObject &original, const QDateTime &splitInstant)
{
    QJsonObject out = original;
    if (original.contains(QStringLiteral("date"))) {
        out.insert(QStringLiteral("date"), splitInstant.toUTC().date().toString(Qt::ISODate));
        return out;
    }
    if (original.contains(QStringLiteral("dateTime"))) {
        out.insert(QStringLiteral("dateTime"), splitInstant.toUTC().toString(Qt::ISODate));
        return out;
    }
    return original;
}

} // namespace

namespace Kalburator::Todo {

SeriesSplitResult splitSeriesAtInstant(const QJsonObject &masterCanon,
                                       const QDateTime &splitInstant,
                                       const QList<QJsonObject> &allExceptions)
{
    SeriesSplitResult result;

    const QString oldUid = masterCanon.value(QStringLiteral("uid")).toString();
    if (oldUid.isEmpty()) {
        result.error = QStringLiteral("masterCanon has no uid");
        return result;
    }
    if (masterCanon.contains(QStringLiteral("recurrenceId"))) {
        result.error = QStringLiteral(
            "masterCanon carries a recurrenceId — splitSeriesAtInstant "
            "operates on a series MASTER, not a detached exception");
        return result;
    }
    if (!splitInstant.isValid()) {
        result.error = QStringLiteral("splitInstant is not a valid QDateTime");
        return result;
    }

    const QJsonArray recurrence = masterCanon.value(QStringLiteral("recurrence")).toArray();
    const int rruleIdx = findRruleLineIndex(recurrence);
    if (rruleIdx < 0) {
        result.error = QStringLiteral(
            "masterCanon has no RRULE line in its recurrence array — "
            "nothing to split");
        return result;
    }
    const QString rruleLine = recurrence.at(rruleIdx).toString();
    const QHash<QString, QString> rruleParts = parseRruleParts(rruleLine);

    // Open decision 5 — COUNT-bounded RRULE: v1 fails loud rather than
    // guessing a recomputed COUNT.
    if (rruleParts.contains(QStringLiteral("COUNT"))) {
        result.error = QStringLiteral(
            "RRULE is COUNT-bounded (%1) — splitSeriesAtInstant (v1) does "
            "not recompute COUNT for a split; the caller must resolve this "
            "before calling").arg(rruleLine);
        return result;
    }

    // ---- value-type + tightened UNTIL for the old master -------------------
    const bool dateOnly = rruleParts.contains(QStringLiteral("UNTIL"))
        ? isDateOnlyUntilToken(rruleParts.value(QStringLiteral("UNTIL")))
        : masterIsDateOnly(masterCanon);

    QString newUntilToken;
    if (dateOnly) {
        QDate newUntilDate = splitInstant.toUTC().date().addDays(-1);
        const QDateTime existing = parseUntilToUtc(rruleParts.value(QStringLiteral("UNTIL")));
        if (existing.isValid() && existing.date() < newUntilDate)
            newUntilDate = existing.date(); // never loosen past the original bound
        newUntilToken = newUntilDate.toString(QStringLiteral("yyyyMMdd"));
    } else {
        QDateTime newUntilDt = splitInstant.toUTC().addSecs(-1);
        const QDateTime existing = parseUntilToUtc(rruleParts.value(QStringLiteral("UNTIL")));
        if (existing.isValid() && existing < newUntilDt)
            newUntilDt = existing; // never loosen past the original bound
        newUntilToken = newUntilDt.toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    }

    const QString tightenedRruleLine = rewriteUntilInRruleLine(rruleLine, newUntilToken);

    QJsonObject updatedOldMaster = masterCanon;
    {
        QJsonArray updatedRecurrence = recurrence;
        updatedRecurrence.replace(rruleIdx, tightenedRruleLine);
        updatedOldMaster.insert(QStringLiteral("recurrence"), updatedRecurrence);
    }

    // ---- new master ----------------------------------------------------------
    const QString newUid =
        oldUid + QStringLiteral("-split-") + sanitizedUtcIsoStamp(splitInstant);

    QJsonObject newMaster = masterCanon;
    newMaster.insert(QStringLiteral("uid"), newUid);
    newMaster.insert(QStringLiteral("recurrence"), QJsonArray{ rruleLine }); // verbatim, untightened
    newMaster.insert(QStringLiteral("seriesSplitOf"), oldUid);
    newMaster.remove(QStringLiteral("recurrenceId"));
    newMaster.remove(QStringLiteral("recurrenceRange"));
    if (masterCanon.contains(QStringLiteral("start"))) {
        newMaster.insert(QStringLiteral("start"),
            retimeCanonDateTimeField(masterCanon.value(QStringLiteral("start")).toObject(),
                                     splitInstant));
    }
    if (masterCanon.contains(QStringLiteral("due"))) {
        newMaster.insert(QStringLiteral("due"),
            retimeCanonDateTimeField(masterCanon.value(QStringLiteral("due")).toObject(),
                                     splitInstant));
    }

    // ---- rebase exceptions at/after splitInstant ------------------------------
    QList<QJsonObject> rebased;
    const QDateTime splitUtc = splitInstant.toUTC();
    for (const QJsonObject &exception : allExceptions) {
        const QJsonObject recIdObj = exception.value(QStringLiteral("recurrenceId")).toObject();
        const QString recIdStr = recIdObj.value(QStringLiteral("dateTime")).toString();
        if (recIdStr.isEmpty())
            continue; // no parseable recurrenceId — leave with the old master
        const QDateTime recId = QDateTime::fromString(recIdStr, Qt::ISODate);
        if (!recId.isValid())
            continue;
        if (recId.toUTC() < splitUtc)
            continue; // before the split point — stays with the old master

        QJsonObject rebasedException = exception;
        rebasedException.insert(QStringLiteral("uid"), newUid);
        rebased.append(rebasedException);
    }

    result.ok = true;
    result.updatedOldMaster = updatedOldMaster;
    result.newMaster = newMaster;
    result.rebasedExceptions = rebased;
    return result;
}

}  // namespace Kalburator::Todo
