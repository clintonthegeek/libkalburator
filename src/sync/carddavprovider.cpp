#include "carddavprovider.h"

#include "iblobbackend.h"
#include "backendconfiguration.h"
#include "carddavcapabilitydiscovery.h"
#include "carddavconfigwidget.h"
#include "davslug.h"
#include "remotecontactsbackend.h"

#include <QFutureInterface>
#include <QFutureWatcher>
#include <QUuid>

namespace Kalburator::Sync {

CardDavProvider::CardDavProvider(QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

CardDavProvider::~CardDavProvider() = default;

void CardDavProvider::load(const BackendConfiguration &config) {
    if (!config.id.isEmpty()) m_id = config.id;
    m_displayName = config.displayName;
    m_serverUrl = QUrl(config.connectionParams.value(QStringLiteral("url")).toString());
    m_username  = config.connectionParams.value(QStringLiteral("username")).toString();
    m_password  = config.connectionParams.value(QStringLiteral("password")).toString();
}

BackendConfiguration CardDavProvider::save() const {
    BackendConfiguration cfg;
    cfg.id = m_id;
    cfg.type = kind();
    cfg.displayName = m_displayName;
    cfg.connectionParams[QStringLiteral("url")]      = m_serverUrl.toString();
    cfg.connectionParams[QStringLiteral("username")] = m_username;
    cfg.connectionParams[QStringLiteral("password")] = m_password;
    return cfg;
}

QWidget *CardDavProvider::createConfigWidget(QWidget *parent) {
    auto *w = new CardDavConfigWidget(parent);
    w->setConfiguration(save());   // provider -> widget, same as CalDav/multiproto
    return w;
}

QFuture<bool> CardDavProvider::connect() {
    if (m_connected) {
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(true);
        fi.reportFinished();
        return fi.future();
    }

    if (m_serverUrl.isEmpty() || !m_serverUrl.isValid() || m_serverUrl.scheme().isEmpty()) {
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(false);
        fi.reportFinished();
        emit error(QStringLiteral("CardDavProvider: server URL is empty, invalid, or missing a scheme"));
        return fi.future();
    }

    // Idempotent: if a connect is in flight, return its future. Overwriting
    // m_connectPromise would destroy the previous QPromise unfinished, whose
    // destructor would cancel+reportFinished the underlying QFutureInterface
    // without a result — crashing any watcher::result() observer.
    if (m_connectPromise) {
        return m_connectPromise->future();
    }

    // Drop any abandoned in-flight discovery before starting fresh.
    if (m_discovery) {
        m_discovery->disconnect(this);
        m_discovery->deleteLater();
        m_discovery = nullptr;
    }
    m_connectPromise.reset(new QPromise<bool>);
    m_connectPromise->start();

    m_discovery = new CardDavCapabilityDiscovery(this);
    m_discovery->setCredentials(m_serverUrl, m_username, m_password);

    // Track whether the discovery emitted an error before the future finished.
    // shared_ptr so both lambdas (error + watcher-finished) hold a refcount and the
    // bool outlives whichever fires last — the error/finished firing order is undefined.
    auto errorSeen = std::make_shared<bool>(false);
    QObject::connect(m_discovery, &CardDavCapabilityDiscovery::error,
                     this, [this, errorSeen](const QString &msg) {
        *errorSeen = true;
        emit error(msg);
    });

    QFuture<QList<CollectionInfo>> discFuture = m_discovery->discover();

    // Use a QFutureWatcher to observe the discovery future without blocking
    // the event loop. The watcher lives on the heap and self-deletes after
    // firing.
    auto *watcher = new QFutureWatcher<QList<CollectionInfo>>(this);
    QObject::connect(watcher, &QFutureWatcher<QList<CollectionInfo>>::finished,
                     this, [this, watcher, errorSeen]() {
        const bool hadError = *errorSeen;
        const QList<CollectionInfo> books = watcher->result();
        watcher->deleteLater();
        onDiscoveryFinished(books, hadError);
    });
    watcher->setFuture(discFuture);

    return m_connectPromise->future();
}

void CardDavProvider::onDiscoveryFinished(const QList<CollectionInfo> &books,
                                           bool hadError)
{
    const bool success = !hadError;

    if (success) {
        m_addressbookUrls = m_discovery ? m_discovery->addressbookUrls()
                                        : QMap<QString, QString>();
        m_collections = books;
        m_connected = true;
        emit collectionsChanged();
        emit connectionStateChanged(true);
    }
    // Error signal already forwarded in the lambda above.

    if (m_connectPromise) {
        m_connectPromise->addResult(success);
        m_connectPromise->finish();
        m_connectPromise.reset();
    }

    if (m_discovery) {
        m_discovery->disconnect(this);
        m_discovery->deleteLater();
        m_discovery = nullptr;
    }
}

void CardDavProvider::disconnect() {
    if (m_connectPromise) {
        m_connectPromise->addResult(false);
        m_connectPromise->finish();
        m_connectPromise.reset();
    }
    if (m_discovery) {
        m_discovery->disconnect(this);
        m_discovery->deleteLater();
        m_discovery = nullptr;
    }
    if (!m_connected) return;
    m_connected = false;
    m_collections.clear();
    m_addressbookUrls.clear();
    emit connectionStateChanged(false);
}

std::unique_ptr<IBlobBackend>
CardDavProvider::createBackend(const QString &collectionId) {
    if (!m_connected) {
        return nullptr;
    }
    const auto urlIt = m_addressbookUrls.constFind(collectionId);
    if (urlIt == m_addressbookUrls.constEnd()) {
        return nullptr;
    }

    // RemoteContactsBackend inherits SyncBackend which inherits IBlobBackend,
    // so the unique_ptr<IBlobBackend> upcast is implicit.
    auto backend = std::make_unique<RemoteContactsBackend>(m_serverUrl, m_username, m_password);
    backend->registerAddressbookUrl(collectionId, QUrl(urlIt.value()));
    return backend;
}

// PHASE2-TASK2.3 — v2 contract entry point. Produces ONE Contacts-kind
// ProviderBackendSpec for the given collection by reading the connect-time
// discovery caches (m_addressbookUrls + m_collections). ProviderManager's
// createBackendsForCollection() (Task 1.1 stub) routes through this once
// Phase 2.4+ flips the manager's registration walk to spec.backendId-based.
//
// Per-collection granularity (same shape CalDavProvider /
// MultiProtocolDavProvider use in Tasks 2.1 / 2.2): one spec per
// advertised id. The per-domain fanout collapse is the manager's job;
// this provider just hands back the descriptors the manager needs to do
// the merging.
QList<ProviderBackendSpec>
CardDavProvider::createBackends(const QString &collectionId) const
{
    QList<ProviderBackendSpec> out;

    if (!m_connected) return out;
    if (collectionId.isEmpty()) return out;

    // The v1 contract returns nullptr for unknown collectionId — match
    // that by returning {} for the same input.
    const auto urlIt = m_addressbookUrls.constFind(collectionId);
    if (urlIt == m_addressbookUrls.constEnd()) return out;
    const QString href = urlIt.value();

    // Display-name priority: connect() precomputes ci.name from the
    // server-supplied displayname (with collectionId fallback) in
    // m_collections, so that's the highest-fidelity name source.
    // Last resorts: collectionId then href (the latter must be
    // non-empty here because urlIt found a mapping for it, but keep
    // the chain explicit so a future code path that clears
    // m_collections without a disconnect firing still has a string).
    QString displayName;
    for (const auto &c : m_collections) {
        if (c.id == collectionId) {
            displayName = c.name;
            break;
        }
    }
    if (displayName.isEmpty()) displayName = collectionId;
    if (displayName.isEmpty()) displayName = href;

    ProviderBackendSpec spec;
    spec.collectionId = collectionId;
    spec.kind = BackendKind::Contacts;
    spec.displayName = displayName;
    // backendId shape mirrors CalDavProvider / MultiProtocolDavProvider
    // so ProviderManager Phase 2.4+ can parse a uniform
    // "<providerId>:<collectionId>:<stableSlug>" triple across every
    // DAV provider type.
    spec.backendId = QStringLiteral("%1:%2:%3").arg(
        m_id, collectionId, makeDavSlug(displayName, href));
    // CardDAV leg carries no per-collection caps here — VCARD per RFC
    // 6352. Keeps the same contentTypes key the CalDav and multiprotocol
    // legs populate so a downstream spec-driven render code can read
    // them uniformly.
    spec.contentTypes << QStringLiteral("VCARD");
    // No color: Phase 2 CardDAV discovery does not currently surface a
    // server-supplied color in m_addressbookUrls / m_collections. Keep
    // spec.color empty rather than synthesising one — a future phase
    // can wire in a CardDav collection-color PROPFIND if it adds value.

    out.append(spec);
    return out;
}

} // namespace Kalburator::Sync
