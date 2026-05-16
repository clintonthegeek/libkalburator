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

void MultiProtocolDavProvider::load(const BackendConfiguration &)
{
    // Task 3 implements this.
}

BackendConfiguration MultiProtocolDavProvider::save() const
{
    // Task 3 implements this.
    return {};
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
