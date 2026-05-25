#include "markdownfilesbackend.h"
#include "backendrecord.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

using Kalburator::Sync::BackendRecord;

namespace Kalburator::Sinks {

namespace {

// Sanitise a filename stem (mirrors WildPalms' sanitiseFilenameStem).
QString sanitiseStem(const QString &input)
{
    static const QRegularExpression invalidChars(QStringLiteral("[^a-zA-Z0-9_\\-. ]"));
    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));
    QString stem = input;
    stem.replace(invalidChars, QStringLiteral("_"));
    stem.replace(multiSpace, QStringLiteral("_"));
    while (stem.startsWith(QLatin1Char('_'))) stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('_')))   stem.chop(1);
    return stem.trimmed();
}

// First non-empty body line, skipping a leading ---\n...\n--- frontmatter block.
QString firstBodyLine(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    if (text.startsWith(QStringLiteral("---\n"))) {
        const int close = text.indexOf(QStringLiteral("\n---"), 3);
        if (close >= 0) {
            int after = close + 4;
            if (after < text.size() && text.at(after) == QLatin1Char('\n')) ++after;
            text = text.mid(after);
        }
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) return t;
    }
    return {};
}

} // namespace

QString MarkdownFilesBackend::suffixFor(const QString & /*collectionId*/) const
{
    return QStringLiteral("md");
}

QString MarkdownFilesBackend::recordStem(const QString & /*collectionId*/,
                                         const BackendRecord &record) const
{
    const QString line = firstBodyLine(record.data);
    const QString stem = sanitiseStem(line.left(50));
    if (!stem.isEmpty())
        return stem;
    const QString id = record.id.isEmpty() ? QStringLiteral("0") : record.id;
    return QStringLiteral("note_") + id;
}

} // namespace Kalburator::Sinks
