#include "carddavprovider.h"

#include "iblobbackend.h"
#include "backendconfiguration.h"
#include "carddavcapabilitydiscovery.h"
#include "carddavconfigwidget.h"
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

} // namespace Kalburator::Sync
