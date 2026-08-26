#include "multiprotocoldavprovider.h"

#include "multiprotocoldavconfigwidget.h"
#include "caldavcapabilitydiscovery.h"
#include "../calendar/remotecalendarbackend.h"
#include "../contacts/remotecontactsbackend.h"
#include "../universal/filteredcollectionbackend.h"
#include "../universal/kinddemuxbackend.h"
#include "carddavcapabilitydiscovery.h"
#include "caldavcontenttypes.h"
#include "davslug.h"

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
    // Idempotent: if a connect is already in flight, return its future.
    // Overwriting m_connectPromise would destroy the previous QPromise
    // unfinished, whose destructor cancels+reportFinished the underlying
    // QFutureInterface without a result — crashing any watcher::result()
    // observer (e.g. ProviderManager's per-provider connectAll watcher).
    // Checked before the URL-validity gate below since an in-flight attempt
    // already passed that gate on its first call.
    if (m_connectPromise) {
        return m_connectPromise->future();
    }

    emit connectionStateChanged(ProviderConnectionState::Connecting);

    if (m_serverUrl.isEmpty() || !m_serverUrl.isValid()) {
        m_lastError = QStringLiteral("No server URL configured.");
        emit error(m_lastError);
        emit connectionStateChanged(ProviderConnectionState::Error);
        QPromise<bool> p;
        auto fut = p.future();
        p.start(); p.addResult(false); p.finish();
        return fut;
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
    m_calUrlBySlug.clear();
    m_calCapsBySlug.clear();
    m_contactsUrlBySlug.clear();
    m_calDavCaps.clear();
    emit connectionStateChanged(false);
}

std::vector<ProviderBackendSpec> MultiProtocolDavProvider::createBackends()
{
    using Kalburator::Sinks::FilteredCollectionBackend;
    using Kalburator::Sinks::KindDemuxBackend;
    using SyncBackendBasePtr = std::shared_ptr<SyncBackendBase>;

    std::vector<ProviderBackendSpec> out;
    if (!m_connected) return out;

    QList<CollectionInfo> calCols, contactCols;
    for (const auto &col : std::as_const(m_collections))
        (col.type == QLatin1String("contacts") ? contactCols : calCols) << col;

    if (!calCols.isEmpty()) {
        // B2C P3.e kind-demux partition. Rectification rule (binding):
        // transport grouping never crosses a domain boundary — a CalDAV
        // collection advertising BOTH VTODO and VEVENT/VJOURNAL (hybrid)
        // must surface in the "cal" domain only as a VEVENT/VJOURNAL-filtered
        // view and in a NEW "todo" domain as a VTODO-filtered view, both over
        // the SAME underlying RemoteCalendarBackend instance, both keeping
        // the SAME collection id. Pure-VTODO-only collections go todo-spec
        // only; collections without VTODO stay cal-spec only; collections
        // with NO advertised contentTypes keep the legacy behavior (cal-spec
        // only, no filtering).
        const auto advertisesVTodo =
            [](const CollectionInfo &c) { return c.contentTypes.contains(QLatin1String("VTODO")); };
        const auto advertisesCalKind =
            [](const CollectionInfo &c) {
                return c.contentTypes.contains(QLatin1String("VEVENT"))
                    || c.contentTypes.contains(QLatin1String("VJOURNAL"));
            };

        bool anyTodoBearing = false;
        for (const auto &col : std::as_const(calCols))
            anyTodoBearing |= advertisesVTodo(col);

        if (!anyTodoBearing) {
            // Legacy shape: one unfiltered RemoteCalendarBackend for every
            // calendar (byte-for-byte today's behavior).
            ProviderBackendSpec spec;
            spec.domainId = QStringLiteral("cal");
            auto backend = std::make_unique<RemoteCalendarBackend>(m_serverUrl, m_username, m_password);
            QList<RemoteCalendarBackend::PrimedCalendar> primed;
            for (const auto &col : std::as_const(calCols)) {
                const QString url = m_calUrlBySlug.value(col.id);
                backend->registerCalendarUrl(col.id, url);
                const auto capIt = m_calCapsBySlug.constFind(col.id);
                if (capIt != m_calCapsBySlug.constEnd())
                    primed.append(RemoteCalendarBackend::PrimedCalendar{
                        col.id, url, capIt->serverColor, contentTypesFromCaps(*capIt)});
            }
            backend->primeCalendars(primed);
            spec.collections = calCols;
            spec.backend = std::move(backend);
            out.push_back(std::move(spec));
        } else {
            // Demuxed shape: shared transport + per-domain routed views.
            const QString parentId = QStringLiteral("%1-cal-transport").arg(m_id);
            auto parentShared = std::make_shared<RemoteCalendarBackend>(
                m_serverUrl, m_username, m_password);
            RemoteCalendarBackend *parent = parentShared.get();
            QList<RemoteCalendarBackend::PrimedCalendar> primed;
            QList<QPair<QString, SyncBackendBase*>> calRoutes;
            QList<QPair<QString, SyncBackendBase*>> todoRoutes;
            QList<CollectionInfo> calSpecCols, todoSpecCols;
            for (const auto &col : std::as_const(calCols)) {
                const QString url = m_calUrlBySlug.value(col.id);
                parent->registerCalendarUrl(col.id, url);
                const auto capIt = m_calCapsBySlug.constFind(col.id);
                if (capIt != m_calCapsBySlug.constEnd())
                    primed.append(RemoteCalendarBackend::PrimedCalendar{
                        col.id, url, capIt->serverColor, contentTypesFromCaps(*capIt)});

                if (advertisesVTodo(col) && advertisesCalKind(col)) {
                    // Hybrid: filtered views in BOTH domains, same id.
                    FilteredCollectionBackend *calView =
                        new FilteredCollectionBackend(parent, parentId, col.id, col.id,
                            QStringList{ QStringLiteral("VEVENT"),
                                         QStringLiteral("VJOURNAL") });
                    calRoutes.append({col.id, calView});
                    calSpecCols.append(col);
                    FilteredCollectionBackend *todoView =
                        new FilteredCollectionBackend(parent, parentId, col.id, col.id,
                            QStringList{ QStringLiteral("VTODO") });
                    todoRoutes.append({col.id, todoView});
                    todoSpecCols.append(col);
                } else if (advertisesVTodo(col)) {
                    // Pure-VTODO-only: todo domain exclusively.
                    FilteredCollectionBackend *todoView =
                        new FilteredCollectionBackend(parent, parentId, col.id, col.id,
                            QStringList{ QStringLiteral("VTODO") });
                    todoRoutes.append({col.id, todoView});
                    todoSpecCols.append(col);
                } else {
                    // Calendar-only: direct route into the shared transport.
                    calRoutes.append({col.id, parent});
                    calSpecCols.append(col);
                }
            }
            parent->primeCalendars(primed);

            ProviderBackendSpec calSpec;
            calSpec.domainId = QStringLiteral("cal");
            calSpec.collections = calSpecCols;
            calSpec.backend = std::make_unique<KindDemuxBackend>(
                calRoutes, parentShared);
            out.push_back(std::move(calSpec));

            if (!todoRoutes.isEmpty()) {
                ProviderBackendSpec todoSpec;
                todoSpec.domainId = QStringLiteral("todo");
                todoSpec.collections = todoSpecCols;
                todoSpec.backend = std::make_unique<KindDemuxBackend>(
                    todoRoutes, std::move(parentShared));
                out.push_back(std::move(todoSpec));
            }
        }
    }
    if (!m_calendarsOnly && !contactCols.isEmpty()) {
        ProviderBackendSpec spec;
        spec.domainId = QStringLiteral("contacts");
        auto backend = std::make_unique<RemoteContactsBackend>(m_serverUrl, m_username, m_password);
        for (const auto &col : std::as_const(contactCols))
            backend->registerAddressbookUrl(col.id, QUrl(m_contactsUrlBySlug.value(col.id)));
        spec.collections = contactCols;
        spec.backend = std::move(backend);
        out.push_back(std::move(spec));
    }
    return out;
}

void MultiProtocolDavProvider::onCalDavFinished(bool success)
{
    if (success) {
        m_calDavUrlMap = m_caldavDiscovery->calendarUrls();
        m_calDavCaps = m_caldavDiscovery->perCalendarCapabilities();  // retained for createBackends() priming
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
    m_calUrlBySlug.clear();
    m_calCapsBySlug.clear();
    m_contactsUrlBySlug.clear();

    // Task 2.2: re-key CalDAV's display-name-ish discovery key by URL slug —
    // the stable, server-unique, rename-surviving id CollectionInfo/backends
    // now use (same idiom as CalDavProvider, Task 2.1).
    for (auto info : m_calDavResult) {
        const QString innerKey = info.id;
        const QString href = m_calDavUrlMap.value(innerKey);
        const QString slug = davSlugFromUrl(href);
        if (slug.isEmpty()) continue;  // no lookup key; cannot be primed
        info.id = slug;
        m_calUrlBySlug.insert(slug, href);
        const auto capIt = m_calDavCaps.constFind(innerKey);
        if (capIt != m_calDavCaps.constEnd())
            m_calCapsBySlug.insert(slug, capIt.value());
        m_collections.append(info);
    }
    if (!m_calendarsOnly) {
        // CardDavCapabilityDiscovery already keys by the last DAV URL path
        // segment (its own id derivation is slug-shaped) — no re-keying needed.
        for (auto info : m_cardDavResult) {
            if (m_cardDavUrlMap.contains(info.id))
                m_contactsUrlBySlug.insert(info.id, m_cardDavUrlMap.value(info.id));
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
        emit connectionStateChanged(ProviderConnectionState::Connected);
    } else {
        // m_lastError may still be empty here (e.g. calendarsOnly mode with
        // an empty-but-error-free CalDAV result) — fall back to a generic
        // message so lastError() is always populated alongside Error.
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral(
                "Connect failed: no calendars or addressbooks discovered");
        emit connectionStateChanged(ProviderConnectionState::Error);
    }
    // connectionStateChanged(false) is NOT emitted on connect failure —
    // only emitted from disconnect() when leaving a connected state.
    // Callers check the future result or lastError() for failure feedback.
}

} // namespace Kalburator::Sync
