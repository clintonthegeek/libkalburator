#include "akonadiprovider.h"

#ifdef HAVE_AKONADI

#include "iblobbackend.h"
#include "backendconfiguration.h"

#include <QFutureInterface>
#include <QUuid>

namespace Kalburator::Sync {

AkonadiProvider::AkonadiProvider(QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_displayName(QStringLiteral("Local Akonadi"))
{
}

AkonadiProvider::~AkonadiProvider() = default;

QString AkonadiProvider::kind() const
{
    return QStringLiteral("akonadi");
}

void AkonadiProvider::load(const BackendConfiguration &config)
{
    if (!config.id.isEmpty())
        m_id = config.id;
    if (!config.displayName.isEmpty())
        m_displayName = config.displayName;
}

BackendConfiguration AkonadiProvider::save() const
{
    BackendConfiguration cfg;
    cfg.id          = m_id;
    cfg.type        = kind();
    cfg.displayName = m_displayName;
    return cfg;
}

QWidget *AkonadiProvider::createConfigWidget(QWidget * /*parent*/)
{
    // Phase L.8 will implement a real widget.
    return nullptr;
}

QFuture<bool> AkonadiProvider::connect()
{
    // Phase L.4 will open a real Akonadi session.
    QFutureInterface<bool> fi;
    fi.reportStarted();
    fi.reportResult(false);
    fi.reportFinished();
    return fi.future();
}

void AkonadiProvider::disconnect()
{
    if (!m_connected)
        return;
    m_connected = false;
    m_collections.clear();
    Q_EMIT connectionStateChanged(false);
}

std::unique_ptr<IBlobBackend>
AkonadiProvider::createBackend(const QString & /*collectionId*/)
{
    // Phase L.5 (calendar) and L.7 (contacts) will wire real backends.
    return nullptr;
}

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
