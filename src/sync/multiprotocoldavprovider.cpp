#include "multiprotocoldavprovider.h"

#include "multiprotocoldavconfigwidget.h"
#include "caldavcapabilitydiscovery.h"
#include "../calendar/remotecalendarbackend.h"
#include "../contacts/remotecontactsbackend.h"
#include "carddavcapabilitydiscovery.h"
#include "caldavcontenttypes.h"

#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QUuid>

Q_LOGGING_CATEGORY(lcMultiDav, "kalburator.sync.multidav")

namespace Kalburator::Sync {

MultiProtocolDavProvider::MultiProtocolDavProvider(bool calendarsOnly, QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_displayName(QStringLiteral("DAV account"))
    , m_calendarsOnly(calendarsOnly)
{
}

MultiProtocolDavProvider::~MultiProtocolDavProvider() = default;

QString MultiProtocolDavProvider::kind() const
{
    return QStringLiteral("multiproto-dav");
}

void MultiProtocolDavProvider::load(const BackendConfiguration &config)
{
    if (!config.id.isEmpty()) m_id = config.id;
    if (!config.displayName.isEmpty()) m_displayName = config.displayName;

    const auto &p = config.connectionParams;
    m_serverUrl              = QUrl(p.value(QStringLiteral("url")).toString());
    m_username               = p.value(QStringLiteral("username")).toString();
    m_password               = p.value(QStringLiteral("password")).toString();
    m_manualCalDavPrincipal  = p.value(QStringLiteral("manualCaldavPrincipal")).toString();
    m_manualCardDavPrincipal = p.value(QStringLiteral("manualCarddavPrincipal")).toString();
    // Restore the persisted calendars-only mode. Absent (legacy configs) keeps
    // the value the provider was constructed with, preserving old behavior.
    if (p.contains(QStringLiteral("calendarsOnly")))
        m_calendarsOnly = p.value(QStringLiteral("calendarsOnly")).toBool();
}

BackendConfiguration MultiProtocolDavProvider::save() const
{
    BackendConfiguration c;
    c.id = m_id;
    c.type = kind();
    c.displayName = m_displayName;
    c.connectionParams[QStringLiteral("url")]      = m_serverUrl.toString();
    c.connectionParams[QStringLiteral("username")] = m_username;
    c.connectionParams[QStringLiteral("password")] = m_password;
    if (!m_manualCalDavPrincipal.isEmpty())
        c.connectionParams[QStringLiteral("manualCaldavPrincipal")] = m_manualCalDavPrincipal;
    if (!m_manualCardDavPrincipal.isEmpty())
        c.connectionParams[QStringLiteral("manualCarddavPrincipal")] = m_manualCardDavPrincipal;
    // Persist calendars-only mode so a registry-reconstructed provider (which
    // the contribution builds with calendarsOnly=false) restores it on load().
    c.connectionParams[QStringLiteral("calendarsOnly")] = m_calendarsOnly;
    return c;
}

QWidget *MultiProtocolDavProvider::createConfigWidget(QWidget *parent)
{
    auto *w = new MultiProtocolDavConfigWidget(parent);
    w->setConfiguration(save());
    return w;
}

QFuture<bool> MultiProtocolDavProvider::connect()
{
    if (m_connected) {
        QPromise<bool> p;
        auto fut = p.future();
        p.start(); p.addResult(true); p.finish();
        return fut;
    }
    if (m_serverUrl.isEmpty() || !m_serverUrl.isValid()) {
        emit error(QStringLiteral("No server URL configured."));
        QPromise<bool> p;
        auto fut = p.future();
        p.start(); p.addResult(false); p.finish();
        return fut;
    }

    // Idempotent: if a connect is already in flight, return its future.
    // Overwriting m_connectPromise would destroy the previous QPromise
    // unfinished, whose destructor cancels+reportFinished the underlying
    // QFutureInterface without a result — crashing any watcher::result()
    // observer (e.g. ProviderManager's per-provider connectAll watcher).
    if (m_connectPromise) {
        return m_connectPromise->future();
    }

    qCInfo(lcMultiDav).nospace()
        << "connect: probing " << m_serverUrl.toString()
        << " as user '" << m_username << "' (CalDAV + CardDAV)";

    m_connectPromise = std::make_shared<QPromise<bool>>();
    m_connectPromise->start();

    m_calDavDone = m_cardDavDone = false;
    m_calDavResult.clear();
    m_cardDavResult.clear();
    m_calDavError.clear();
    m_cardDavError.clear();
    m_calDavUrlMap.clear();
    m_cardDavUrlMap.clear();
    m_lastWarning.clear();
    m_lastError.clear();

    // CalDAV half — signal-based
    if (m_caldavDiscovery) {
        m_caldavDiscovery->disconnect(this);
        m_caldavDiscovery->deleteLater();
        m_caldavDiscovery = nullptr;
    }
    m_caldavDiscovery = new CalDavCapabilityDiscovery(m_serverUrl, m_username, m_password, this);
    if (!m_manualCalDavPrincipal.isEmpty())
        m_caldavDiscovery->setPrincipalUrlOverride(m_manualCalDavPrincipal);
    QObject::connect(m_caldavDiscovery, &CalDavCapabilityDiscovery::finished,
                     this, &MultiProtocolDavProvider::onCalDavFinished);
    m_caldavDiscovery->start();

    // CardDAV half — future-based
    if (m_carddavDiscovery) {
        m_carddavDiscovery->disconnect(this);
        m_carddavDiscovery->deleteLater();
        m_carddavDiscovery = nullptr;
    }
    m_carddavDiscovery = new CardDavCapabilityDiscovery(this);
    m_carddavDiscovery->setCredentials(m_serverUrl, m_username, m_password);
    if (!m_manualCardDavPrincipal.isEmpty())
        m_carddavDiscovery->setPrincipalHrefOverride(m_manualCardDavPrincipal);
    QObject::connect(m_carddavDiscovery, &CardDavCapabilityDiscovery::error,
                     this, [this](const QString &msg) { m_cardDavError = msg; });
    QFuture<QList<CollectionInfo>> cardFut = m_carddavDiscovery->discover();
    auto *cardWatcher = new QFutureWatcher<QList<CollectionInfo>>(this);
    QObject::connect(cardWatcher, &QFutureWatcher<QList<CollectionInfo>>::finished,
                     this, [this, cardWatcher]() {
                         onCardDavFinished(cardWatcher);
                     });
    cardWatcher->setFuture(cardFut);

    return m_connectPromise->future();
}

void MultiProtocolDavProvider::disconnect()
{
    if (m_connectPromise) {
        m_connectPromise->addResult(false);
        m_connectPromise->finish();
        m_connectPromise.reset();
    }
    if (m_caldavDiscovery) {
        m_caldavDiscovery->disconnect(this);
        m_caldavDiscovery->deleteLater();
        m_caldavDiscovery = nullptr;
    }
    if (m_carddavDiscovery) {
        m_carddavDiscovery->disconnect(this);
        m_carddavDiscovery->deleteLater();
        m_carddavDiscovery = nullptr;
    }
    if (!m_connected) return;
    m_connected = false;
    m_collections.clear();
    m_urlByCollectionId.clear();
    m_calDavCaps.clear();
    emit connectionStateChanged(false);
}

std::unique_ptr<IBlobBackend>
MultiProtocolDavProvider::createBackend(const QString &collectionId)
{
    if (!m_connected) return nullptr;
    if (!m_urlByCollectionId.contains(collectionId)) return nullptr;
    const QString href = m_urlByCollectionId.value(collectionId);

    const QString calPrefix      = QStringLiteral("multiproto-dav:%1:cal:").arg(m_id);
    const QString contactsPrefix = QStringLiteral("multiproto-dav:%1:contacts:").arg(m_id);

    if (collectionId.startsWith(calPrefix)) {
        auto backend = std::make_unique<RemoteCalendarBackend>(
            m_serverUrl, m_username, m_password);
        backend->registerCalendarUrl(collectionId, href);

        // Seed the bound calendar from connect-time discovery so loadCalendars()
        // skips its server-wide PROPFIND (v0.63). The discovery caps are keyed by
        // the inner (unprefixed) key, but the calendar must be primed under the
        // *prefixed* collectionId — that is the id this provider advertised in
        // collections() (maybeResolveConnect sets CollectionInfo.id = prefixedId),
        // hence the id the host built its bindings from. PrimedCalendar.calendarId
        // is the id loadCalendars() emits via calendarDiscovered(); priming with the
        // inner key made discovery emit a different id than the binding, so the host
        // orphaned every calendar and raised a false "missing calendar" — even though
        // fetch (which uses the prefixed id registered above) worked fine.
        const QString innerKey = collectionId.mid(calPrefix.length());
        const auto capIt = m_calDavCaps.constFind(innerKey);
        if (capIt != m_calDavCaps.constEnd()) {
            backend->primeCalendars({ RemoteCalendarBackend::PrimedCalendar{
                collectionId,
                href,
                capIt.value().serverColor,
                contentTypesFromCaps(capIt.value()) } });
        }
        return backend;
    }
    if (collectionId.startsWith(contactsPrefix)) {
        auto backend = std::make_unique<RemoteContactsBackend>(
            m_serverUrl, m_username, m_password);
        backend->registerAddressbookUrl(collectionId, QUrl(href));
        return backend;
    }
    return nullptr;
}

void MultiProtocolDavProvider::onCalDavFinished(bool success)
{
    if (success) {
        m_calDavUrlMap = m_caldavDiscovery->calendarUrls();
        m_calDavCaps = m_caldavDiscovery->perCalendarCapabilities();  // retained for createBackend() priming
        for (auto it = m_calDavCaps.constBegin();
             it != m_calDavCaps.constEnd(); ++it) {
            CollectionInfo ci;
            ci.id   = it.key();
            ci.name = it.value().serverDisplayName.isEmpty()
                          ? it.key() : it.value().serverDisplayName;
            ci.type = QStringLiteral("calendar");
            ci.readOnly = !it.value().writable;  // thread discovered writability (Phase 2C authority seed)
            if (it.value().supportsVEvent) ci.contentTypes << QStringLiteral("VEVENT");
            if (it.value().supportsVTodo)  ci.contentTypes << QStringLiteral("VTODO");
            m_calDavResult.append(ci);
        }
    } else {
        m_calDavError = m_caldavDiscovery->errorMessage();
        if (m_calDavError.isEmpty())
            m_calDavError = QStringLiteral("CalDAV discovery failed");
    }
    m_caldavDiscovery->disconnect(this);
    m_caldavDiscovery->deleteLater();
    m_caldavDiscovery = nullptr;
    m_calDavDone = true;
    maybeResolveConnect();
}

void MultiProtocolDavProvider::onCardDavFinished(QFutureWatcher<QList<CollectionInfo>> *w)
{
    if (w) {
        m_cardDavResult = w->result();
        if (m_carddavDiscovery)
            m_cardDavUrlMap = m_carddavDiscovery->addressbookUrls();
        w->deleteLater();
    }
    if (m_carddavDiscovery) {
        m_carddavDiscovery->disconnect(this);
        m_carddavDiscovery->deleteLater();
        m_carddavDiscovery = nullptr;
    }
    m_cardDavDone = true;
    maybeResolveConnect();
}

void MultiProtocolDavProvider::maybeResolveConnect()
{
    if (!m_calDavDone || !m_cardDavDone) return;
    if (!m_connectPromise) return;

    m_collections.clear();
    m_urlByCollectionId.clear();

    for (auto info : m_calDavResult) {
        const QString innerKey = info.id;
        const QString prefixedId =
            QStringLiteral("multiproto-dav:%1:cal:%2").arg(m_id, innerKey);
        if (m_calDavUrlMap.contains(innerKey))
            m_urlByCollectionId[prefixedId] = m_calDavUrlMap[innerKey];
        info.id = prefixedId;
        m_collections.append(info);
    }
    if (!m_calendarsOnly) {
        for (auto info : m_cardDavResult) {
            const QString innerKey = info.id;
            const QString prefixedId =
                QStringLiteral("multiproto-dav:%1:contacts:%2").arg(m_id, innerKey);
            if (m_cardDavUrlMap.contains(innerKey))
                m_urlByCollectionId[prefixedId] = m_cardDavUrlMap[innerKey];
            info.id = prefixedId;
            m_collections.append(info);
        }
    }

    const bool calOk  = m_calDavError.isEmpty()  && !m_calDavResult.isEmpty();
    const bool cardOk = m_cardDavError.isEmpty() && !m_cardDavResult.isEmpty();

    // Log each protocol's outcome at a boundary so failures are visible on the
    // console even if the UI only shows a summary.
    if (calOk)
        qCInfo(lcMultiDav) << "CalDAV: discovered" << m_calDavResult.size() << "calendar(s)";
    else
        qCWarning(lcMultiDav).noquote()
            << "CalDAV: no calendars —" << (m_calDavError.isEmpty()
                ? QStringLiteral("empty result") : m_calDavError);
    if (!m_calendarsOnly) {
        if (cardOk)
            qCInfo(lcMultiDav) << "CardDAV: discovered" << m_cardDavResult.size() << "addressbook(s)";
        else
            qCWarning(lcMultiDav).noquote()
                << "CardDAV: no addressbooks —" << (m_cardDavError.isEmpty()
                    ? QStringLiteral("empty result") : m_cardDavError);
    }

    // In calendarsOnly mode only CalDAV matters — CardDAV success/failure is
    // not surfaced as a warning and does not contribute to anyOk.
    if (m_calendarsOnly) {
        if (!calOk && !m_calDavError.isEmpty()) {
            m_lastError = m_calDavError;
            emit error(m_calDavError);
        }
    } else {
        if (!calOk && cardOk)
            m_lastWarning = QStringLiteral("Calendar discovery failed: %1")
                                .arg(m_calDavError);
        else if (calOk && !cardOk)
            m_lastWarning = QStringLiteral("Addressbook discovery failed: %1")
                                .arg(m_cardDavError);

        if (!calOk && !cardOk) {
            QString combined = m_calDavError;
            if (!m_cardDavError.isEmpty()) {
                if (!combined.isEmpty()) combined += QStringLiteral("; ");
                combined += m_cardDavError;
            }
            if (!combined.isEmpty()) {
                m_lastError = combined;
                emit error(combined);
            }
        }
    }

    const bool anyOk = m_calendarsOnly ? calOk : (calOk || cardOk);

    m_connected = anyOk;
    m_connectPromise->addResult(anyOk);
    m_connectPromise->finish();
    m_connectPromise.reset();

    if (anyOk) {
        emit collectionsChanged();
        emit connectionStateChanged(true);
    }
    // connectionStateChanged(false) is NOT emitted on connect failure —
    // only emitted from disconnect() when leaving a connected state.
    // Callers check the future result or lastError() for failure feedback.
}

} // namespace Kalburator::Sync
