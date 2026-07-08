#ifndef KALBURATOR_SYNC_CHANGEDETECTION_H
#define KALBURATOR_SYNC_CHANGEDETECTION_H

#include <QMap>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {

/**
 * @brief Capability interface — collection-level change-detection token.
 *
 * Backends that can answer "has this collection changed since I last
 * looked?" cheaply (without fetching every record) opt into this
 * interface. The engine's fast-path skip consults
 * `collectionRevision()` (fresh) vs `cachedCollectionRevision()`
 * (persisted) and skips the full diff for unchanged mappings.
 *
 * Examples of backend-specific tokens:
 *   - CalDAV / CardDAV servers expose a `cs:getctag` PROPFIND value.
 *   - Local file directories can compute a fingerprint hash of
 *     `(filename | mtime | size)` tuples.
 *   - Palm hot-sync devices have a per-database sync anchor token.
 *   - Akonadi has a per-collection mtime / revision number.
 *
 * Backends that have no cheap revision query simply do not implement
 * this interface; the engine treats them as "always changed" and
 * proceeds to a full fetch+diff (correct, just slower).
 *
 * Pure-virtual non-QObject interface — one of the neutral backend
 * contracts that live in `sync/` (alongside `SyncBackendBase`).
 * Backends inherit it as a mixin via multiple inheritance:
 *
 *   class RemoteCalendarBackend : public Sync::SyncBackend,
 *                                 public Sync::ChangeDetection { ... };
 */
class ChangeDetection
{
public:
    virtual ~ChangeDetection() = default;

    /**
     * @brief Fresh revision token for a single collection.
     *
     * Returns the *current* revision token. May involve a network or
     * filesystem query. Empty string means "I cannot answer cheaply
     * right now" — engine treats as changed.
     */
    virtual QString collectionRevision(const QString &collectionId) = 0;

    /**
     * @brief Fresh revision tokens for multiple collections (batched).
     *
     * Backends that can batch revision queries (CalDAV PROPFIND with
     * multiple hrefs, single Palm hot-sync handshake) override this
     * for efficiency. Default impl loops over `collectionRevision`.
     *
     * Collections whose revision the backend can't answer are absent
     * from the returned map (do not appear with empty value).
     */
    virtual QMap<QString, QString>
    collectionRevisions(const QStringList &collectionIds)
    {
        QMap<QString, QString> out;
        for (const auto &id : collectionIds) {
            const QString rev = collectionRevision(id);
            if (!rev.isEmpty())
                out.insert(id, rev);
        }
        return out;
    }

    /**
     * @brief Last persisted revision token for a collection.
     *
     * Returns the revision the backend believes was current at the
     * end of the last successful sync. The engine compares this
     * against the fresh `collectionRevision()` to decide whether
     * the collection is unchanged.
     *
     * Empty string means "no persisted value" — engine treats as
     * changed (forces full sync, then primes cache for next time).
     *
     * Engine no longer calls this (sync-hardening H3, 2026-07-05); the
     * engine's skip check now compares against its own per-mapping
     * sync-progress token in BaselineStore instead. This method remains
     * for backend-internal use and external consumers (e.g. WildPalms).
     */
    virtual QString cachedCollectionRevision(const QString &collectionId) const = 0;

    /**
     * @brief Whether this backend persists collection revisions across
     * process restarts. Default: true.
     */
    virtual bool persistsCollectionRevisions() const { return true; }
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_CHANGEDETECTION_H
