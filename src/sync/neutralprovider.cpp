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

std::unique_ptr<IBlobBackend> NeutralProvider::createBackend(const QString &collectionId) {
    if (!m_connected) return nullptr;
    if (collectionId != m_info.id) return nullptr;
    return m_factory ? m_factory() : nullptr;
}

// PHASE2-TASK2.3 — v2 contract entry point. Produces at most one
// spec, only for the collectionId this provider was constructed to
// wrap (m_info.id). ProviderManager's createBackendsForCollection
// routes through this once Phase 2.4+ flips to spec-driven
// registration; until then it's additive vs the v1 createBackend()
// path that connectAll() and addProvider() still use.
//
// backendId shape mirrors the CalDav / CardDav / multi-protocol DAV
// providers' "<providerId>:<collectionId>:<stableSlug>" triple so the
// Phase 2.4+ BackendRegistry can register uniformly. The "slug"
// component is m_info.id itself here (NeutralProvider has no
// server-derived href; using the id is safe because a single-collection
// neutral provider cannot produce a within-domain collision).
QList<ProviderBackendSpec>
NeutralProvider::createBackends(const QString &collectionId) const
{
    QList<ProviderBackendSpec> out;
    if (!m_connected) return out;
    if (collectionId.isEmpty()) return out;
    if (collectionId != m_info.id) return out;

    ProviderBackendSpec spec;
    spec.collectionId = m_info.id;
    spec.backendId = QStringLiteral("%1:%2:%2").arg(m_id, m_info.id);
    spec.displayName = m_info.name.isEmpty() ? m_info.id : m_info.name;

    // Infer BackendKind from m_info.type with a Calendar default for
    // the local use case (Phase 2 task: "always Calendar for its use
    // case" when no kind hint is available — see header doc).
    const QString type = m_info.type.toLower();
    if (type == QStringLiteral("contacts")) {
        spec.kind = BackendKind::Contacts;
        spec.contentTypes << QStringLiteral("VCARD");
    } else {
        // Includes "calendar", "" (unset), and anything else; the
        // existing user-base is overwhelmingly calendar-shaped local
        // files so defaulting here is the right behaviour for now.
        spec.kind = BackendKind::Calendar;
    }
    // Carry over any provider-set contentTypes on m_info so a caller
    // that already tagged the collection (e.g. VEVENT/VTODO for a
    // local .ics backend) isn't silently dropped.
    if (!m_info.contentTypes.isEmpty() && spec.contentTypes.isEmpty()) {
        spec.contentTypes = m_info.contentTypes;
    }

    out.append(spec);
    return out;
}

} // namespace Kalburator::Sync
