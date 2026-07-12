#include "neutralprovider.h"
#include "iblobbackend.h"
#include "backendconfiguration.h"
#include <QFutureInterface>
#include <QUuid>

namespace Kalburator::Sync {

NeutralProvider::NeutralProvider(QString kind, CollectionInfo info,
                                  BackendFactory factory, QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_kind(std::move(kind))
    , m_info(std::move(info))
    , m_factory(std::move(factory))
{}

NeutralProvider::~NeutralProvider() = default;

void NeutralProvider::load(const BackendConfiguration &config) {
    if (!config.id.isEmpty()) m_id = config.id;
    if (!config.displayName.isEmpty()) m_info.name = config.displayName;
}

BackendConfiguration NeutralProvider::save() const {
    BackendConfiguration cfg;
    cfg.id = m_id;
    cfg.type = m_kind;
    cfg.displayName = m_info.name;
    return cfg;
}

QWidget *NeutralProvider::createConfigWidget(QWidget *) { return nullptr; }

QFuture<bool> NeutralProvider::connect() {
    if (!m_connected) {
        m_connected = true;
        Q_EMIT connectionStateChanged(true);
        Q_EMIT collectionsChanged();
    }
    QFutureInterface<bool> fi;
    fi.reportStarted();
    fi.reportResult(true);
    fi.reportFinished();
    return fi.future();
}

void NeutralProvider::disconnect() {
    if (m_connected) {
        m_connected = false;
        Q_EMIT connectionStateChanged(false);
    }
}

std::vector<ProviderBackendSpec> NeutralProvider::createBackends()
{
    std::vector<ProviderBackendSpec> out;
    if (!m_connected || !m_factory) return out;
    ProviderBackendSpec spec;
    spec.domainId = m_info.id;          // single-collection: domain == collection
    spec.backend = m_factory();
    spec.collections = { m_info };
    if (spec.backend) out.push_back(std::move(spec));
    return out;
}

} // namespace Kalburator::Sync
