#include "icaltimestamp.h"

#include <QRegularExpression>
#include <QTimeZone>

namespace Kalburator::Calendar {

namespace {

QDateTime extractProperty(const QString &text, const QString &propertyName)
{
    // Matches "PROPERTY:20250101T000000Z" (optionally with parameters before
    // the colon, e.g. "DTSTAMP;VALUE=DATE-TIME:..."), anchored to the start
    // of a line so we don't match inside an unrelated property's value.
    const QRegularExpression re(
        QStringLiteral("^%1(?:;[^:\\r\\n]*)?:(\\d{8}T\\d{6}Z)").arg(propertyName),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(text);
    if (!match.hasMatch())
        return {};
    QDateTime dt = QDateTime::fromString(match.captured(1),
                                          QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    if (dt.isValid())
        dt.setTimeZone(QTimeZone::utc());
    return dt;
}

}  // namespace

QDateTime extractICalTimestamp(const QByteArray &icalBytes)
{
    const QString text = QString::fromUtf8(icalBytes);

    if (const QDateTime lastMod = extractProperty(text, QStringLiteral("LAST-MODIFIED"));
        lastMod.isValid())
        return lastMod;

    if (const QDateTime dtstamp = extractProperty(text, QStringLiteral("DTSTAMP"));
        dtstamp.isValid())
        return dtstamp;

    if (const QDateTime created = extractProperty(text, QStringLiteral("CREATED"));
        created.isValid())
        return created;

    return {};
}

}  // namespace Kalburator::Calendar
