#ifndef KALBURATOR_SYNC_IPROVIDER_H
#define KALBURATOR_SYNC_IPROVIDER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QFuture>
#include <QIcon>
#include <memory>

#include "collectioninfo.h"
#include "backendconfiguration.h"
#include "iblobbackend.h"

class QWidget;

namespace Kalburator::Sync {

/**
 * @brief Source-of-many-collections under a single auth/connection.
 *
 * Providers model real-world account types: Akonadi (a single resource
 * exposing calendar/contacts/todo/note collections), Nextcloud (one
 * server speaking CalDAV+CardDAV), Google (one OAuth account fanning
 * out to multiple APIs), etc.
 *
 * A provider's role: from a single configured account, enumerate
 * collections and produce IBlobBackend instances per collection. The
 * collections share the underlying connection/auth, so a user
 * configures the account once and references its collections from
 * multiple SyncMapping instances.
 *
 * Lifecycle:
 *   1. Construct (typically by ProviderManager from persisted config).
 *   2. load(BackendConfiguration) — apply persisted account settings.
 *   3. connect() — async; auth + capability discovery + collection
 *      enumeration. Emits connectionStateChanged(true) on success.
 *   4. createBackend(collectionId) — produce an IBlobBackend for one
 *      collection. Caller takes ownership; backend self-marshals async
 *      operations if needed.
 *   5. disconnect() — tears down backends, closes connection.
 *
 * Multiple instances of the same provider kind are allowed (e.g., two
 * CalDavProvider instances for two different Nextcloud accounts).
 * Each has its own id().
 */
class IProvider : public QObject
{
    Q_OBJECT
public:
    explicit IProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~IProvider() override = default;

    // ── Identity ───────────────────────────────────────────────────
    /// Stable per-instance UUID. Persisted across runs. Generated at
    /// "Add Account" time; never mutates.
    virtual QString id() const = 0;

    /// Static identifier of the provider class. "caldav", "akonadi",
    /// "google", etc. Multiple instances of the same kind() are
    /// allowed.
    virtual QString kind() const = 0;

    /// User-facing display string ("Work Nextcloud", etc.). Mutable
    /// via the config widget. Stored in BackendConfiguration.
    virtual QString displayName() const = 0;

    // ── Persistence ────────────────────────────────────────────────
    /// Apply previously-persisted config. Called by ProviderManager
    /// after construction. Implementations should NOT auto-connect;
    /// connect() is a separate explicit step.
    virtual void load(const BackendConfiguration &config) = 0;

    /// Serialize current state for persistence. Includes id, kind,
    /// displayName, and provider-specific connectionParams.
    virtual BackendConfiguration save() const = 0;

    /// O.1.3: Apply edited config to a possibly-connected provider.
    /// Default impl: if connected, disconnect synchronously, load() the
    /// new config, then connect() again (fire-and-forget; caller can
    /// observe via connectionStateChanged or via
    /// ProviderManager::providerStateChanged from O.1.2).
    ///
    /// Subclasses override only when they need special tear-down
    /// (e.g. CalDavProvider clearing cached cookies, AkonadiProvider
    /// re-binding to a new resource).
    virtual void applyConfig(const BackendConfiguration &cfg) {
        const bool wasConnected = isConnected();
        if (wasConnected) {
            disconnect();
        }
        load(cfg);
        if (wasConnected) {
            (void)connect();
        }
    }

    // ── Config UI ──────────────────────────────────────────────────
    /// Build a widget for editing this provider's config (server URL,
    /// credentials, display name, etc.). Caller takes ownership.
    /// Widget should call the provider's load() with the user's
    /// edited config when the user clicks "Apply" / "Save"; the
    /// widget owns the unsaved state until then.
    virtual QWidget *createConfigWidget(QWidget *parent) = 0;

    // ── Lifecycle ──────────────────────────────────────────────────
    /// Open the connection: auth + capability discovery + collection
    /// enumeration. Async — returns a future that resolves to true on
    /// success, false on failure. On failure, error() is emitted with
    /// a human-readable message.
    ///
    /// Calling connect() while already connected is a no-op (returns
    /// an immediately-resolved true future).
    virtual QFuture<bool> connect() = 0;

    /// Tear down the connection. Synchronous. Destroys any backends
    /// that were handed out via createBackend (callers must not hold
    /// raw pointers across this call — pass via unique_ptr only).
    /// No-op if not connected.
    virtual void disconnect() = 0;

    virtual bool isConnected() const = 0;

    // ── Collections ────────────────────────────────────────────────
    /// Collections discovered during connect(). Empty until connected.
    virtual QList<CollectionInfo> collections() const = 0;

    /// Build a backend for one of the discovered collections. Caller
    /// takes ownership. Returns nullptr if collectionId isn't in
    /// collections() or if the provider isn't connected.
    ///
    /// Implementations should return a SyncBackend-derived backend
    /// (SyncBackend inherits IBlobBackend in this codebase), so
    /// callers (notably ProviderManager in Task 2) can register the
    /// instance with BackendRegistry, which currently stores
    /// SyncBackend* instances. The IProvider API uses IBlobBackend to
    /// keep the contract minimal.
    ///
    /// The returned backend's resourceId() must encode this provider's
    /// id() so MappingScheduler can detect resource contention
    /// correctly (e.g., "caldav:<provider-id>:<collection-id>").
    virtual std::unique_ptr<IBlobBackend>
        createBackend(const QString &collectionId) = 0;

    // ── Optional UI / status accessors ─────────────────────────────
    /// Optional icon for the account-list row. Default: null QIcon.
    virtual QIcon icon() const { return {}; }

    /// Non-fatal warning from the last connect(). Empty = no warning.
    virtual QString lastWarning() const { return {}; }

signals:
    /// Emitted when connect() completes (true) or disconnect() runs
    /// (false), or when a runtime connection loss is detected.
    void connectionStateChanged(bool connected);

    /// Emitted when collections() result changes (typically just once
    /// after connect()'s capability discovery completes; possibly
    /// again if the server pushes a notification, but most providers
    /// don't support that).
    void collectionsChanged();

    /// Human-readable error during connect() or runtime. Surfaced to
    /// UI as a status bar / dialog message.
    void error(QString message);
};

} // namespace Kalburator::Sync

#endif
