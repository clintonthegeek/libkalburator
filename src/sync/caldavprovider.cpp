#include "caldavprovider.h"

#include "caldavconfigwidget.h"
#include "iblobbackend.h"
#include "backendconfiguration.h"
#include "caldavcapabilitydiscovery.h"
#include "caldavcontenttypes.h"
#include "davslug.h"
#include "remotecalendarbackend.h"

#include <QFutureInterface>
#include <QUrl>
#include <QUuid>

namespace Kalburator::Sync {

// PHASE2-TASK2.3 — slug derivation now lives in src/sync/davslug.h as
// Kalburator::Sync::makeDavSlug(). The CalDAV-specific anonymous-
// namespace copy from Tasks 2.1 / 2.2 was removed in Task 2.3 to
// eliminate duplication; the v2 spec producer below calls the shared
// helper directly. The CalDAV-specific rule (last segment of the href,
// sanitised) is unchanged — just no longer typed twice.

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

    if (m_serverUrl.isEmpty() || !m_serverUrl.isValid() || m_serverUrl.scheme().isEmpty()) {
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(false);
        fi.reportFinished();
        m_lastError = QStringLiteral("CalDavProvider: server URL is empty, invalid, or missing a scheme");
        emit error(m_lastError);
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
        m_calendarUrls = m_discovery->calendarUrls();
        m_perCalendarCaps = m_discovery->perCalendarCapabilities();  // retained for createBackend() priming
        m_collections.clear();
        for (auto it = m_perCalendarCaps.constBegin();
             it != m_perCalendarCaps.constEnd(); ++it) {
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
        m_lastError = errMsg.isEmpty()
                      ? QStringLiteral("CalDavProvider: discovery failed")
                      : errMsg;
        emit error(m_lastError);
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

// PHASE2-TASK2.1 — v1 createBackend() is intentionally kept verbatim. The
// createBackends() fanout entry-point below is additive; ProviderManager
// still routes through this method until Phase 2.4+ flips the registration
// pipeline to use spec.backendId directly. Do not delete or refactor this
// function in Task 2.1.
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

// PHASE2-TASK2.1 — v2 contract entry point. Produces ONE Calendar-kind
// ProviderBackendSpec for the given collection by reading the connect-time
// discovery caches (m_calendarUrls / m_perCalendarCaps / m_collections).
// Called by ProviderManager::createBackendsForCollection in Phase 2.4+
// to plan a registration walk that uses spec.backendId directly.
QList<ProviderBackendSpec>
CalDavProvider::createBackends(const QString &collectionId) const
{
    QList<ProviderBackendSpec> out;

    if (!m_connected) return out;
    if (collectionId.isEmpty()) return out;

    // The v1 contract returns nullptr for unknown collectionId — match
    // that by returning {} for the same input.
    const auto urlIt = m_calendarUrls.constFind(collectionId);
    if (urlIt == m_calendarUrls.constEnd()) return out;
    const QString href = urlIt.value();

    const auto capIt = m_perCalendarCaps.constFind(collectionId);

    // Display name priority: connect() precomputes ci.name from
    // caps.serverDisplayName (with collectionId fallback), so m_collections
    // is the highest-fidelity name source. caps.serverDisplayName is a
    // secondary fallback (kept for safety in case m_collections was
    // cleared without a disconnect path firing). Last resort: collectionId
    // itself, then href, which must be non-empty here because urlIt found
    // a mapping for it.
    QString displayName;
    for (const auto &c : m_collections) {
        if (c.id == collectionId) {
            displayName = c.name;
            break;
        }
    }
    if (displayName.isEmpty()
        && capIt != m_perCalendarCaps.constEnd()
        && !capIt.value().serverDisplayName.isEmpty()) {
        displayName = capIt.value().serverDisplayName;
    }
    if (displayName.isEmpty()) displayName = collectionId;
    if (displayName.isEmpty()) displayName = href;

    ProviderBackendSpec spec;
    spec.collectionId = collectionId;
    spec.kind = BackendKind::Calendar;
    spec.displayName = displayName;
    spec.backendId = QStringLiteral("%1:%2:%3").arg(
        m_id, collectionId,
        makeDavSlug(displayName, href));

    if (capIt != m_perCalendarCaps.constEnd()) {
        const PerCalendarCapabilities &caps = capIt.value();
        if (caps.serverColor.isValid()) {
            spec.color = caps.serverColor.name();
        }
        // Mirror onDiscoveryFinished()'s populate rule so spec.contentTypes
        // matches the CollectionInfo.contentTypes a manager can also pull.
        if (caps.supportsVEvent) spec.contentTypes << QStringLiteral("VEVENT");
        if (caps.supportsVTodo)  spec.contentTypes << QStringLiteral("VTODO");
    }

    out.append(spec);
    return out;
}

} // namespace Kalburator::Sync
