#include "akonadiprovider.h"

#ifdef HAVE_AKONADI

#include "iblobbackend.h"
#include "backendconfiguration.h"

#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KContacts/Addressee>
#include <KJob>

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
    if (m_connected) {
        QFutureInterface<bool> fi;
        fi.reportStarted();
        fi.reportResult(true);
        fi.reportFinished();
        return fi.future();
    }

    m_connectPromise = std::make_shared<QPromise<bool>>();
    auto fut = m_connectPromise->future();
    m_connectPromise->start();

    if (!m_session) {
        m_session = new Akonadi::Session(
            QStringLiteral("kalburator-akonadi-provider-%1").arg(m_id).toUtf8(),
            this);
    }

    auto *job = new Akonadi::CollectionFetchJob(
        Akonadi::Collection::root(),
        Akonadi::CollectionFetchJob::Recursive,
        m_session);
    job->fetchScope().setContentMimeTypes({
        KCalendarCore::Event::eventMimeType(),
        KCalendarCore::Todo::todoMimeType(),
        KContacts::Addressee::mimeType(),
    });
    QObject::connect(job, &KJob::result,
        this, &AkonadiProvider::onCollectionsFetched);
    return fut;
}

void AkonadiProvider::onCollectionsFetched(KJob *kjob)
{
    auto *job = qobject_cast<Akonadi::CollectionFetchJob *>(kjob);
    if (!job || job->error()) {
        const QString msg = job ? job->errorString()
                                : QStringLiteral("Akonadi fetch job missing");
        Q_EMIT error(msg);
        if (m_connectPromise) {
            m_connectPromise->addResult(false);
            m_connectPromise->finish();
            m_connectPromise.reset();
        }
        return;
    }

    m_collections.clear();
    for (const auto &col : job->collections()) {
        const auto mimes = col.contentMimeTypes();
        QString type;
        if (mimes.contains(KCalendarCore::Event::eventMimeType()) ||
            mimes.contains(KCalendarCore::Todo::todoMimeType())) {
            type = QStringLiteral("calendar");
        } else if (mimes.contains(KContacts::Addressee::mimeType())) {
            type = QStringLiteral("contacts");
        } else {
            continue; // skip unknown collection types
        }

        CollectionInfo info;
        info.id   = QStringLiteral("akonadi-%1").arg(col.id());
        info.name = col.displayName();
        info.type = type;
        m_collections.append(info);
    }

    m_connected = true;
    Q_EMIT collectionsChanged();
    Q_EMIT connectionStateChanged(true);
    if (m_connectPromise) {
        m_connectPromise->addResult(true);
        m_connectPromise->finish();
        m_connectPromise.reset();
    }
}

void AkonadiProvider::disconnect()
{
    if (!m_connected)
        return;
    m_collections.clear();
    m_connected = false;
    if (m_session) {
        m_session->deleteLater();
        m_session = nullptr;
    }
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
