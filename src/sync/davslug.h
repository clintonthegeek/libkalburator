#ifndef KALBURATOR_SYNC_DAVSLUG_H
#define KALBURATOR_SYNC_DAVSLUG_H
#include <QString>
#include <QUrl>
namespace Kalburator::Sync {
/// Stable per-account calendar key: last path segment of the DAV URL
/// ("https://…/calendars/user/personal/" -> "personal"). Unlike display
/// names, slugs are server-unique within a calendar home and survive
/// renames.
inline QString davSlugFromUrl(const QString &href)
{
    const auto parts = QUrl(href).path(QUrl::FullyDecoded)
                           .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : parts.constLast();
}
} // namespace Kalburator::Sync
#endif
