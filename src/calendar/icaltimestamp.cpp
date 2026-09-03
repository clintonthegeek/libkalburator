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

QDateTime extractICalPropertyLiteral(const QByteArray &icalBytes, const QString &propertyName)
{
    return extractProperty(QString::fromUtf8(icalBytes), propertyName);
}

QByteArray stripICalPropertyLine(const QByteArray &icalBytes, const QString &propertyName)
{
    if (icalBytes.isEmpty())
        return icalBytes;
    // Same anchor/parameter shape as extractProperty's match, but captures
    // the whole line (any value) plus its line terminator so removal
    // doesn't leave a blank line behind.
    const QRegularExpression re(
        QStringLiteral("^%1(?:;[^:\\r\\n]*)?:[^\\r\\n]*\\r?\\n").arg(propertyName),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    QString text = QString::fromUtf8(icalBytes);
    text.remove(re);
    return text.toUtf8();
}

QByteArray stripICalPropertyParameter(const QByteArray &icalBytes, const QString &propertyName,
                                       const QString &parameterName)
{
    if (icalBytes.isEmpty())
        return icalBytes;

    const QString text = QString::fromUtf8(icalBytes);

    // Capture PROPERTY's whole logical content line, folds included: any
    // non-line-terminator character, or a fold sequence (CRLF/LF + a single
    // SPACE/HTAB), repeated until a real (unfolded) line terminator. This is
    // RFC 5545 §3.1's own definition of "unfolding" a content line, applied
    // without discarding the fold bytes so everything outside PARAMETER
    // round-trips byte-identical.
    const QRegularExpression lineRe(
        QStringLiteral("^%1(?:[^\\r\\n]|\\r?\\n[ \\t])*").arg(propertyName),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);

    // Within one such captured line, remove ";PARAM=value" — tolerating an
    // optional fold between the ";" and PARAM's name (the common case: a
    // fold lands right after the preceding parameter's trailing ";") and
    // within the value itself.
    const QRegularExpression paramRe(
        QStringLiteral(";(?:\\r?\\n[ \\t])?%1=(?:[^:;\\r\\n]|\\r?\\n[ \\t])*").arg(parameterName),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    out.reserve(text.size());
    int pos = 0;
    auto it = lineRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(pos, m.capturedStart() - pos);
        QString line = m.captured();
        line.remove(paramRe);
        out += line;
        pos = m.capturedEnd();
    }
    out += text.mid(pos);
    return out.toUtf8();
}

}  // namespace Kalburator::Calendar
