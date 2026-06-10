#include "caldavprovider.h"

#include "caldavconfigwidget.h"
#include "iblobbackend.h"
#include "backendconfiguration.h"
#include "caldavcapabilitydiscovery.h"
#include "remotecalendarbackend.h"
#include "caldavcontenttypes.h"

#include <QFutureInterface>
#include <QUuid>

namespace Kalburator::Sync {

CalDavProvider::CalDavProvider(QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

CalDavProvider::~CalDavProvider() = default;

QString CalDavProvider::kind() const {
    // Matches RemoteCalendarBackend::BackendTypeName ("caldav") so
    // BackendRegistry / BackendConfiguration::friendlyTypeName agree.
    return QStringLiteral("caldav");
}

void CalDavProvider::load(const BackendConfiguration &config) {
    if (!config.id.isEmpty()) m_id = config.id;
    m_displayName = config.displayName;
    m_serverUrl = QUrl(config.connectionParams.value(QStringLiteral("url")).toString());
    m_username  = config.connectionParams.value(QStringLiteral("username")).toString();
    m_password  = config.connectionParams.value(QStringLiteral("password")).toString();
}

BackendConfiguration CalDavProvider::save() const {
    BackendConfiguration cfg;
    cfg.id = m_id;
    cfg.type = kind();
    cfg.displayName = m_displayName;
    cfg.connectionParams[QStringLiteral("url")]      = m_serverUrl.toString();
    cfg.connectionParams[QStringLiteral("username")] = m_username;
    cfg.connectionParams[QStringLiteral("password")] = m_password;
    return cfg;
}

QWidget *CalDavProvider::createConfigWidget(QWidget *parent) {
    return new CalDavConfigWidget(this, parent);
}

QFuture<bool> CalDavProvider::connect() {
    if (m_connected) {
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(true);
        fi.reportFinished();
        return fi.future();
    }

    if (m_serverUrl.isEmpty() || !m_serverUrl.isValid()) {
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(false);
        fi.reportFinished();
        emit error(QStringLiteral("CalDavProvider: server URL is empty or invalid"));
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

    m_discovery = new CalDavCapabilityDiscovery(m_serverUrl, m_username, m_password, this);
    QObject::connect(m_discovery, &CalDavCapabilityDiscovery::finished,
                     this, &CalDavProvider::onDiscoveryFinished);
    m_discovery->start();

    return m_connectPromise->future();
}

void CalDavProvider::onDiscoveryFinished(bool success) {
    if (!m_discovery) {
        return;
    }

    if (success) {
        const auto caps = m_discovery->discoveredCapabilities();
        m_calendarUrls = m_discovery->calendarUrls();
        m_perCalendarCaps = caps.perCalendarCapabilities;  // retained for createBackend() priming
        m_collections.clear();
        for (auto it = caps.perCalendarCapabilities.constBegin();
             it != caps.perCalendarCapabilities.constEnd(); ++it) {
            CollectionInfo ci;
            ci.id   = it.key();
            ci.name = it.value().serverDisplayName.isEmpty() ? it.key()
                                                              : it.value().serverDisplayName;
            ci.type = QStringLiteral("calendar");
            ci.isDefault = false;
            ci.readOnly = !it.value().writable;  // thread discovered writability (Phase 2C authority seed)
            if (it.value().supportsVEvent) ci.contentTypes << QStringLiteral("VEVENT");
            if (it.value().supportsVTodo)  ci.contentTypes << QStringLiteral("VTODO");
            m_collections.append(ci);
        }
        m_connected = true;
        emit collectionsChanged();
        emit connectionStateChanged(true);
    } else {
        const QString errMsg = m_discovery->errorMessage();
        emit error(errMsg.isEmpty()
                   ? QStringLiteral("CalDavProvider: discovery failed")
                   : errMsg);
    }

    if (m_connectPromise) {
        m_connectPromise->addResult(success);
        m_connectPromise->finish();
        m_connectPromise.reset();
    }

    m_discovery->disconnect(this);
    m_discovery->deleteLater();
    m_discovery = nullptr;
}

void CalDavProvider::disconnect() {
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
    m_calendarUrls.clear();
    m_perCalendarCaps.clear();
    emit connectionStateChanged(false);
}

std::unique_ptr<IBlobBackend>
CalDavProvider::createBackend(const QString &collectionId) {
    if (!m_connected) {
        return nullptr;
    }
    const auto urlIt = m_calendarUrls.constFind(collectionId);
    if (urlIt == m_calendarUrls.constEnd()) {
        return nullptr;
    }

    // RemoteCalendarBackend inherits SyncBackend which inherits IBlobBackend, so
    // the unique_ptr<IBlobBackend> upcast is implicit.
    auto backend = std::make_unique<RemoteCalendarBackend>(m_serverUrl, m_username, m_password);
    backend->registerCalendarUrl(collectionId, urlIt.value());

    // Seed the bound calendar from connect-time discovery so the backend's
    // loadCalendars() short-circuits its server-wide PROPFIND (v0.63). For plain
    // CalDav the collectionId IS the discovery key.
    const auto capIt = m_perCalendarCaps.constFind(collectionId);
    if (capIt != m_perCalendarCaps.constEnd()) {
        backend->primeCalendars({ RemoteCalendarBackend::PrimedCalendar{
            collectionId,
            urlIt.value(),
            capIt.value().serverColor,
            contentTypesFromCaps(capIt.value()) } });
    }
    return backend;
}

} // namespace Kalburator::Sync
