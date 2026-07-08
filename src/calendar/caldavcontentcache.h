#ifndef KALBURATOR_CALDAVCONTENTCACHE_H
#define KALBURATOR_CALDAVCONTENTCACHE_H

#include <QList>
#include <QString>

namespace Kalburator::Sync {

/**
 * @brief Persistent SQLite cache of raw iCal payloads, keyed by item URL + ETag.
 *
 * Extracted from RemoteCalendarBackend (Plan 7 T5): the delta-sync content
 * cache is a self-contained machine — own connection lifecycle, own schema,
 * no knowledge of the backend beyond an account seed string.
 *
 * The cache file name derives from a process-stable FNV-1a hash of
 * @p accountSeed (use `url.host() + url.path()`), NOT qHash(): qHash mixes a
 * per-process random seed and would orphan the previous run's cache on every
 * launch (v0.63 determinism contract, pinned by
 * tst_remotecalendarbackend_convergence).
 *
 * Unlike the pre-extraction code, the destructor closes and removes the
 * QSqlDatabase connection — the backend used to leak one registered
 * connection per instance.
 */
class CalDavContentCache
{
public:
    explicit CalDavContentCache(const QString &accountSeed);
    ~CalDavContentCache();

    CalDavContentCache(const CalDavContentCache &) = delete;
    CalDavContentCache &operator=(const CalDavContentCache &) = delete;

    /**
     * @brief Override the directory holding the cache DB.
     *
     * When set (non-empty), the DB lives under @p dir instead of
     * QStandardPaths::CacheLocation. Must be called before the first
     * ensureOpen() to take effect.
     */
    void setCacheDir(const QString &dir);

    /// Lazily create/open the DB and schema. Idempotent; false on failure.
    bool ensureOpen();

    /// Cached iCal for @p itemUrl iff its stored ETag equals @p expectedEtag.
    QString content(const QString &itemUrl, const QString &expectedEtag) const;

    /// Upsert a payload. No-op when the URL or ETag is empty.
    void store(const QString &itemUrl, const QString &etag,
               const QString &icalContent);

    /// Evict one item.
    void remove(const QString &itemUrl);

    /// True iff @p itemUrl has a cached row, regardless of its stored ETag.
    /// Ownership-lookup helper: unlike content(), does not require knowing
    /// the ETag in advance (E4/O32 — resolving which calendar owns a uid).
    bool contains(const QString &itemUrl) const;

    struct Row {
        QString url;
        QString ical;
    };

    /// All rows whose URL contains @p pathFragment (the per-calendar scan
    /// RemoteCalendarBackend::serveCachedItems performs on a CTag match).
    QList<Row> rowsByPathFragment(const QString &pathFragment) const;

private:
    QString m_seed;
    QString m_dirOverride;
    QString m_connectionName;
    bool m_open = false;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_CALDAVCONTENTCACHE_H
