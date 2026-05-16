#include "multiprotocoldavprovider.h"

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
    // Task 4 implements this. Skeleton: resolves false.
    QPromise<bool> p;
    auto fut = p.future();
    p.start();
    p.addResult(false);
    p.finish();
    return fut;
}

void MultiProtocolDavProvider::disconnect()
{
    m_connected = false;
}

std::unique_ptr<IBlobBackend>
MultiProtocolDavProvider::createBackend(const QString &)
{
    // Task 5 implements this.
    return nullptr;
}

void MultiProtocolDavProvider::onCalDavFinished()  {}
void MultiProtocolDavProvider::onCardDavFinished() {}
void MultiProtocolDavProvider::maybeResolveConnect() {}

} // namespace Kalburator::Sync
