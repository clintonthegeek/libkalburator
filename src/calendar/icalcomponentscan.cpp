#include "icalcomponentscan.h"

#include <QRegularExpression>

namespace Kalburator::Calendar {

QStringList extractComponentRecurrenceLines(const QByteArray &icalBytes,
                                             QByteArrayView componentName,
                                             const QString &uid)
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
    QStringList currentBlock;
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
            }
            continue;
        }

        if (line.startsWith(QStringLiteral("UID:")) &&
            line.mid(4).trimmed() == uid)
            blockUidMatches = true;
        if (line.startsWith(QStringLiteral("RECURRENCE-ID")))
            blockHasRecurrenceId = true;

        if (line.startsWith(QStringLiteral("RRULE:")) ||
            line.startsWith(QStringLiteral("RDATE:")) ||
            line.startsWith(QStringLiteral("EXDATE:")))
            currentBlock.append(line);
    }

    if (!masterCandidates.isEmpty())
        return masterCandidates.first();

    QStringList concatenated;
    for (const auto &block : allCandidates)
        concatenated += block;
    return concatenated;
}

}  // namespace Kalburator::Calendar
