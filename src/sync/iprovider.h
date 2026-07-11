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

// PHASE1-TASK1.1 — v2 contract precursor: a pure-descriptor spec returned
// from the new createBackends(const QString &collectionId) entry point.
// PHASE1 only introduces the type and the pure virtual; providers stub the
// new method to return an empty list. Phase 2 fills the bodies. The struct
// is intentionally additive — the v1 createBackend(const QString &)
// contract stays intact, so no caller code changes shape this phase.
enum class BackendKind {
    Calendar,
    Contacts
};

/// PHASE1-TASK1.1 — Descriptor for a backend that should be created for a
/// given collection. At this stage providers fill in the basics
/// (collectionId + kind + displayName + a placeholder backendId of the
/// form "provider:collection"); richer fields (server-side color, content
/// types, capability flags) flow through in Phase 2 once the providers
/// populate them from their connect-time discovery caches.
struct ProviderBackendSpec {
    QString     collectionId;   ///< The collection the spec is for.
    BackendKind kind;           ///< Calendar vs Contacts.
    QString     backendId;      ///< Phase 1 stub: "providerId:collectionId".
                                ///< Phase 2: "providerId:domainId" (cal/contacts).
    QString     displayName;    ///< User-facing label (server display name).
    QString     color;          ///< Optional server-supplied color hint.
    QStringList contentTypes;   ///< Optional content-type capability hints.
};

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
    /// Apply/Save is brokered through IProviderConfigWidget (see
    /// iproviderconfigwidget.h); the widget calls the bridge, not
    /// the provider's load() directly.
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

    // PHASE1-TASK1.1 — v2 contract precursor. Returns the specs the
    // manager would feed to a future "create backend per spec" pipeline,
    // for the given single collection (the v1 granularity). Phase 1
    // implementations return an empty list as a no-op stub; Phase 2 will
    // make them return one real spec per collection.
    //
    // Marked `const` so callers can plan a registration walk on const
    // IProvider* without needing to assert mutability. ProviderManager
    // currently iterates registered providers it already owns non-const,
    // but future hooks (sync-engine re-discovery passes a const IProvider*)
    // will benefit from this.
    //
    // Pre-condition: the caller passes a collectionId that is in
    // collections(); behaviour for unknown ids is "return empty" —
    // matches the v1 createBackend() nullptr contract.
    //
    // Additive vs v1: the v1 createBackend(const QString&) above is
    // untouched. Both pipelines can coexist; ProviderManager keeps using
    // createBackend(this phase) and registers through the existing
    // registerProviderBackends() path. Phase 2 flips the wiring.
    virtual QList<ProviderBackendSpec>
        createBackends(const QString &collectionId) const = 0;

    // ── Optional UI / status accessors ─────────────────────────────
    /// Optional icon for the account-list row. Default: null QIcon.
    virtual QIcon icon() const { return {}; }

    /// Non-fatal warning from the last connect(). Empty = no warning.
    virtual QString lastWarning() const { return {}; }

    /// Last error from a failed connect(), or empty if the last connect()
    /// succeeded. Complement to the error() signal for callers that need to
    /// poll the failure reason after the fact (e.g., after future completion).
    virtual QString lastError() const { return {}; }

signals:
    /// Emitted when connect() succeeds (true) or when disconnect() is called
    /// / a runtime connection loss is detected (false). NOT emitted when
    /// connect() fails — use the future result or lastError() / error() for
    /// failure feedback.
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

Q_DECLARE_METATYPE(Kalburator::Sync::BackendKind)

#endif
