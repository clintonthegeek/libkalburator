#include "multiprotocoldavprovider.h"

#include "../calendar/caldavcapabilitydiscovery.h"
#include "carddavcapabilitydiscovery.h"

#include <QFutureWatcher>
#include <QUuid>

namespace Kalburator::Sync {

MultiProtocolDavProvider::MultiProtocolDavProvider(QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_displayName(QStringLiteral("DAV account"))
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
    return c;
}

QWidget *MultiProtocolDavProvider::createConfigWidget(QWidget *)
{
    // Task 6 implements this.
    return nullptr;
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

    // CalDAV half — signal-based
    if (m_caldavDiscovery) {
        m_caldavDiscovery->disconnect(this);
        m_caldavDiscovery->deleteLater();
        m_caldavDiscovery = nullptr;
    }
    m_caldavDiscovery = new CalDavCapabilityDiscovery(m_serverUrl, m_username, m_password, this);
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
    emit connectionStateChanged(false);
}

std::unique_ptr<IBlobBackend>
MultiProtocolDavProvider::createBackend(const QString &)
{
    // Task 5 implements this.
    return nullptr;
}

void MultiProtocolDavProvider::onCalDavFinished(bool success)
{
    if (success) {
        const auto caps = m_caldavDiscovery->discoveredCapabilities();
        m_calDavUrlMap = m_caldavDiscovery->calendarUrls();
        for (auto it = caps.perCalendarCapabilities.constBegin();
             it != caps.perCalendarCapabilities.constEnd(); ++it) {
            CollectionInfo ci;
            ci.id   = it.key();
            ci.name = it.value().serverDisplayName.isEmpty()
                          ? it.key() : it.value().serverDisplayName;
            ci.type = QStringLiteral("calendar");
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
    for (auto info : m_cardDavResult) {
        const QString innerKey = info.id;
        const QString prefixedId =
            QStringLiteral("multiproto-dav:%1:contacts:%2").arg(m_id, innerKey);
        if (m_cardDavUrlMap.contains(innerKey))
            m_urlByCollectionId[prefixedId] = m_cardDavUrlMap[innerKey];
        info.id = prefixedId;
        m_collections.append(info);
    }

    const bool calOk  = m_calDavError.isEmpty()  && !m_calDavResult.isEmpty();
    const bool cardOk = m_cardDavError.isEmpty() && !m_cardDavResult.isEmpty();

    if (!calOk && cardOk)
        m_lastWarning = QStringLiteral("Calendar discovery failed: %1")
                            .arg(m_calDavError);
    else if (calOk && !cardOk)
        m_lastWarning = QStringLiteral("Addressbook discovery failed: %1")
                            .arg(m_cardDavError);

    const bool anyOk = calOk || cardOk;
    if (!anyOk) {
        QString combined = m_calDavError;
        if (!m_cardDavError.isEmpty()) {
            if (!combined.isEmpty()) combined += QStringLiteral("; ");
            combined += m_cardDavError;
        }
        if (!combined.isEmpty()) emit error(combined);
    }

    m_connected = anyOk;
    m_connectPromise->addResult(anyOk);
    m_connectPromise->finish();
    m_connectPromise.reset();

    if (anyOk) emit collectionsChanged();
    emit connectionStateChanged(anyOk);
}

} // namespace Kalburator::Sync
