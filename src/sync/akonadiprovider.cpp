#include "akonadiprovider.h"

#ifdef HAVE_AKONADI

#include "akonadiconfigwidget.h"
#include "iblobbackend.h"
#include "backendconfiguration.h"
#include "akonadicollectionid.h"
#include "../calendar/akonadibackend.h"
#include "../contacts/akonadicontactsbackend.h"

#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KContacts/Addressee>
#include <KJob>

#include <QFutureInterface>
#include <QUuid>

namespace Kalburator::Sync {

AkonadiProvider::AkonadiProvider(bool calendarsOnly, QObject *parent)
    : IProvider(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_displayName(QStringLiteral("Local Akonadi"))
    , m_calendarsOnly(calendarsOnly)
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

QWidget *AkonadiProvider::createConfigWidget(QWidget *parent)
{
    return new AkonadiConfigWidget(parent);
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

    // Idempotent: if a connect is already in flight, return its future. A
    // second connect() call must NOT overwrite m_connectPromise — the assignment
    // would drop the last strong ref to the previous QPromise, whose destructor
    // would then cancel+reportFinished the underlying QFutureInterface without
    // a result. Any watcher::result() observer on that earlier future crashes.
    if (m_connectPromise) {
        return m_connectPromise->future();
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
    QList<QString> mimes{
        KCalendarCore::Event::eventMimeType(),
        KCalendarCore::Todo::todoMimeType(),
    };
    if (!m_calendarsOnly)
        mimes.append(KContacts::Addressee::mimeType());
    job->fetchScope().setContentMimeTypes(mimes);
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

        if (m_calendarsOnly && type != QStringLiteral("calendar"))
            continue;

        CollectionInfo info;
        info.id   = akonadiCollectionIdToString(col.id());  // shared scheme; see akonadicollectionid.h
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
AkonadiProvider::createBackend(const QString &collectionId)
{
    if (!m_connected)
        return nullptr;

    const auto it = std::find_if(m_collections.cbegin(), m_collections.cend(),
        [&](const CollectionInfo &c){ return c.id == collectionId; });
    if (it == m_collections.cend())
        return nullptr;

    if (it->type == QStringLiteral("calendar")) {
        QVariantMap cfg;
        cfg.insert(QStringLiteral("akonadiCollectionId"), collectionId);
        cfg.insert(QStringLiteral("providerId"), m_id);
        auto *b = static_cast<AkonadiBackend *>(
            AkonadiBackend::create(cfg, nullptr));
        return std::unique_ptr<IBlobBackend>(b);
    }

    if (it->type == QStringLiteral("contacts")) {
        QVariantMap cfg;
        cfg.insert(QStringLiteral("akonadiCollectionId"), collectionId);
        cfg.insert(QStringLiteral("providerId"), m_id);
        auto *b = static_cast<AkonadiContactsBackend *>(
            AkonadiContactsBackend::create(cfg, nullptr));
        return std::unique_ptr<IBlobBackend>(b);
    }
    return nullptr;
}

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
