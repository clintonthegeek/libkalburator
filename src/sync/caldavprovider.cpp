#include "caldavprovider.h"

#include "caldavconfigwidget.h"
#include "iblobbackend.h"
#include "backendconfiguration.h"
#include "caldavcapabilitydiscovery.h"
#include "remotecalendarbackend.h"
#include "caldavcontenttypes.h"
#include "davslug.h"

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

    // Idempotent: if a connect is in flight, return its future. Overwriting
    // m_connectPromise would destroy the previous QPromise unfinished, whose
    // destructor would cancel+reportFinished the underlying QFutureInterface
    // without a result — crashing any watcher::result() observer. Checked
    // before the URL-validity gate below since an in-flight attempt already
    // passed that gate on its first call.
    if (m_connectPromise) {
        return m_connectPromise->future();
    }

    emit connectionStateChanged(ProviderConnectionState::Connecting);

    if (m_serverUrl.isEmpty() || !m_serverUrl.isValid() || m_serverUrl.scheme().isEmpty()) {
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(false);
        fi.reportFinished();
        m_lastError = QStringLiteral("CalDavProvider: server URL is empty, invalid, or missing a scheme");
        emit error(m_lastError);
        emit connectionStateChanged(ProviderConnectionState::Error);
        return fi.future();
    }

    // Drop any abandoned in-flight discovery before starting fresh.
    if (m_discovery) {
        m_discovery->disconnect(this);
        m_discovery->deleteLater();
        m_discovery = nullptr;
    }
    m_lastError.clear();
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
        // Task 2.1: discovery's maps are keyed by display name (or a
        // display-name-shaped fallback); re-key by URL slug — the stable,
        // server-unique, rename-surviving id CollectionInfo/backends now use.
        // Consumed as locals only; the raw display-name-keyed maps are never
        // retained as members.
        const auto urls = m_discovery->calendarUrls();          // name -> href
        const auto caps = m_discovery->perCalendarCapabilities(); // name -> caps
        m_urlBySlug.clear();
        m_capsBySlug.clear();
        m_collections.clear();
        for (auto it = caps.constBegin(); it != caps.constEnd(); ++it) {
            const QString url = urls.value(it.key());
            const QString slug = davSlugFromUrl(url);
            if (slug.isEmpty()) continue;  // no lookup key; cannot be primed

            CollectionInfo ci;
            ci.id   = slug;
            ci.name = it.value().serverDisplayName.isEmpty() ? it.key()
                                                              : it.value().serverDisplayName;
            ci.type = QStringLiteral("calendar");
            ci.isDefault = false;
            ci.readOnly = !it.value().writable;  // thread discovered writability (Phase 2C authority seed)
            if (it.value().supportsVEvent) ci.contentTypes << QStringLiteral("VEVENT");
            if (it.value().supportsVTodo)  ci.contentTypes << QStringLiteral("VTODO");
            m_collections.append(ci);

            m_urlBySlug.insert(slug, url);
            m_capsBySlug.insert(slug, it.value());
        }
        m_connected = true;
        emit collectionsChanged();
        emit connectionStateChanged(true);
        emit connectionStateChanged(ProviderConnectionState::Connected);
    } else {
        const QString errMsg = m_discovery->errorMessage();
        m_lastError = errMsg.isEmpty()
                      ? QStringLiteral("CalDavProvider: discovery failed")
                      : errMsg;
        emit error(m_lastError);
        emit connectionStateChanged(ProviderConnectionState::Error);
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
    m_urlBySlug.clear();
    m_capsBySlug.clear();
    emit connectionStateChanged(false);
}

std::vector<ProviderBackendSpec> CalDavProvider::createBackends()
{
    std::vector<ProviderBackendSpec> out;
    if (!m_connected || m_collections.isEmpty()) return out;
    ProviderBackendSpec spec;
    spec.domainId = QStringLiteral("cal");
    auto backend = std::make_unique<RemoteCalendarBackend>(m_serverUrl, m_username, m_password);
    QList<RemoteCalendarBackend::PrimedCalendar> primed;
    for (const auto &col : std::as_const(m_collections)) {
        const QString url = m_urlBySlug.value(col.id);
        backend->registerCalendarUrl(col.id, url);
        const auto capIt = m_capsBySlug.constFind(col.id);
        if (capIt != m_capsBySlug.constEnd())
            primed.append(RemoteCalendarBackend::PrimedCalendar{
                col.id, url, capIt->serverColor, contentTypesFromCaps(*capIt)});
    }
    backend->primeCalendars(primed);
    spec.collections = m_collections;
    spec.backend = std::move(backend);
    out.push_back(std::move(spec));
    return out;
}

} // namespace Kalburator::Sync
