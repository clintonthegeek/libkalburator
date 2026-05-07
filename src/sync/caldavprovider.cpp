#include "caldavprovider.h"

#include "iblobbackend.h"
#include "backendconfiguration.h"

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
    // Matches RemoteBackend::BackendTypeName ("caldav") so
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

QWidget *CalDavProvider::createConfigWidget(QWidget * /*parent*/) {
    // Task 7 fills this in.
    return nullptr;
}

QFuture<bool> CalDavProvider::connect() {
    // Task 5 fills this in. Skeleton: report failure synchronously.
    QFutureInterface<bool> fi;
    fi.reportStarted();
    fi.reportResult(false);
    fi.reportFinished();
    emit error(QStringLiteral("CalDavProvider::connect not yet implemented (Task 5)"));
    return fi.future();
}

void CalDavProvider::disconnect() {
    if (!m_connected) return;
    m_connected = false;
    m_collections.clear();
    emit connectionStateChanged(false);
}

std::unique_ptr<IBlobBackend>
CalDavProvider::createBackend(const QString & /*collectionId*/) {
    // Task 5 fills this in.
    return nullptr;
}

} // namespace Kalburator::Sync
