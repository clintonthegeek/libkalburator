#include "davslug.h"

#include <QChar>
#include <QLatin1Char>
#include <QUrl>
#include <QString>

namespace Kalburator::Sync {

namespace {

// Internal: replace every non-[a-z0-9] code unit with '-', collapse
// runs of '-', and trim leading / trailing '-'. Lowercases on the way
// in so the result is always safe to embed in a backendId without
// further locale-dependent folding.
//
// Pass 1: a-z0-9 are lowercased and kept; everything else becomes '-'
// (this collapses Unicode into single '-' runs handled in pass 2).
// Pass 2: collapse runs of '-' into one separator.
// Pass 3: strip edges so the slug can never begin or end with a dash.
QString sanitiseDavSlug(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar ch : raw) {
        const ushort u = ch.toLower().unicode();
        const bool isAlnum = (u >= QLatin1Char('a').unicode()
                           && u <= QLatin1Char('z').unicode())
                          || (u >= QLatin1Char('0').unicode()
                           && u <= QLatin1Char('9').unicode());
        out.append(isAlnum ? ch.toLower() : QLatin1Char('-'));
    }

    QString collapsed;
    collapsed.reserve(out.size());
    bool prevDash = false;
    for (const QChar ch : out) {
        const bool isDash = (ch == QLatin1Char('-'));
        if (isDash && prevDash) continue;
        collapsed.append(ch);
        prevDash = isDash;
    }

    while (collapsed.startsWith(QLatin1Char('-'))) collapsed.remove(0, 1);
    while (collapsed.endsWith(QLatin1Char('-')))   collapsed.chop(1);
    return collapsed;
}

} // namespace

QString makeDavSlug(const QString &rawName, const QString &href)
{
    // Prefer last non-empty path segment of the href.
    QString lastSegment;
    {
        QString path = QUrl(href).path();
        if (path.endsWith(QLatin1Char('/'))) path.chop(1);
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        lastSegment = (slash >= 0) ? path.mid(slash + 1) : path;
    }
    QString slug = sanitiseDavSlug(lastSegment);
    if (slug.isEmpty()) {
        // Defensive: href had no usable URL characters (very unusual —
        // CardDAV discovery typically advertises a real path, but we
        // have observed calendars with "/" hrefs during manual probing).
        // Fall back to the human name, capped at 32 chars to keep the
        // backendId manageable.
        slug = sanitiseDavSlug(rawName.left(32));
    }
    return slug;
}

} // namespace Kalburator::Sync
