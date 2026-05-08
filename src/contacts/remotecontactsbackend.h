#ifndef REMOTECONTACTSBACKEND_H
#define REMOTECONTACTSBACKEND_H

#include "syncbackend.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include "shape.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <memory>
#include <optional>

namespace Kalburator::Sync {

/**
 * @brief CardDAV-backed contacts backend.
 *
 * Implements IBlobBackend (via SyncBackend) for vCard resources on a
 * CardDAV server. Read-side (Task 5) is fully implemented; write-side
 * stubs are filled in by Task 6.
 *
 * recordId  = "<collectionId>:<vcard-uid>"
 * data      = raw vCard bytes (verbatim from server GET)
 * shape     = (contacts, vcard4) or (contacts, vcard3) depending on VERSION: line
 *
 * All network I/O blocks on a QEventLoop (acceptable for the blob-view /
 * engine-worker call site; Phase F revisits true async).
 */
class RemoteContactsBackend : public SyncBackend
{
    Q_OBJECT

public:
    explicit RemoteContactsBackend(const QUrl &serverRoot,
                                   const QString &username,
                                   const QString &password,
                                   QObject *parent = nullptr);
    ~RemoteContactsBackend() override;

    /**
     * @brief Factory method for BackendRegistry.
     *
     * Expected config keys:
     *   - serverRoot : QString   CardDAV server root URL
     *   - username   : QString
     *   - password   : QString
     */
    static std::unique_ptr<RemoteContactsBackend>
        create(const QVariantMap &config, QObject *parent = nullptr);

    /**
     * @brief Pre-populate an addressbook URL discovered by CardDavProvider.
     *
     * Must be called for each addressbook before loadRecords() is called.
     * The absoluteUrl is the full server URL for the addressbook collection
     * (e.g. http://host/addressbooks/user/personal/).
     */
    void registerAddressbookUrl(const QString &addressbookId,
                                const QUrl &absoluteUrl);

    // --- IBlobBackend identity ----------------------------------------------

    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // --- IBlobBackend collections -------------------------------------------

    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override
    { Q_UNUSED(info); return {}; } // not implemented — Task 8

    // --- IBlobBackend records (read-side — Task 5) --------------------------

    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;

    // --- IBlobBackend records (write-side stubs — filled in by Task 6) -----

    QString createRecord(const QString &collectionId,
                         const BackendRecord &record) override
    { Q_UNUSED(collectionId); Q_UNUSED(record); return {}; }

    bool updateRecord(const BackendRecord &record) override
    { Q_UNUSED(record); return false; }

    bool deleteRecord(const QString &recordId) override
    { Q_UNUSED(recordId); return false; }

    // --- Change detection (minimal) ----------------------------------------

    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                              const QDateTime &since) override
    { Q_UNUSED(collectionId); Q_UNUSED(since); return {}; }

    bool supportsDeleteTracking() const override { return false; }

    // --- Batch (no-ops) ----------------------------------------------------
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

    // --- Shape --------------------------------------------------------------

    QList<Kalburator::Shape::Shape> nativeShapes() const override
    {
        return { Kalburator::Shape::Shape{
            Kalburator::Shape::DomainId{QStringLiteral("contacts")},
            Kalburator::Shape::EncodingId{QStringLiteral("vcard4")} } };
    }

    // --- SyncBackend mandatory calendar-API stubs --------------------------
    // RemoteContactsBackend is contacts-only; the calendar-level API is unused.

    QString backendType() const override
    { return QStringLiteral("carddav-contacts"); }

    void loadCalendars(const QString &) override {}

    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}

    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const TranscodingPlan &) override {}

    void removeItem(const QString &, const QString &) override {}

private:
    // Internal record handle — not exposed through BackendRecord
    struct RecordHandle {
        QUrl    href;   ///< Absolute URL on server  (e.g. /addressbooks/u/p/uid.vcf)
        QString etag;   ///< Server-supplied ETag
    };

    /// Build a BackendRecord from raw vCard bytes and a composite recordId.
    static BackendRecord recordFromVCard(const QString &recordId,
                                         const QByteArray &vcardBytes);

    /// Detect vCard version from raw bytes and return the corresponding Shape.
    static Kalburator::Shape::Shape shapeFromVCard(const QByteArray &vcardBytes);

    /// Perform a synchronous PROPFIND Depth:1 on an addressbook URL.
    /// Returns a map from href (absolute path) to ETag.
    QMap<QString, QString> propfindDepth1(const QUrl &addressbookUrl);

    /// Perform a synchronous GET for a single vCard resource.
    /// Returns empty bytes on error.
    QByteArray getVCard(const QUrl &absoluteUrl);

    /// Build an absolute URL from a server-relative href.
    QUrl absoluteUrl(const QString &href) const;

    /// Credentials embedded in a URL for QNetworkRequest basic-auth.
    QUrl credentialsUrl(const QUrl &base) const;

    QUrl    m_serverRoot;
    QString m_username;
    QString m_password;

    /// Registered addressbook collections: id -> absolute URL
    QMap<QString, QUrl> m_addressbookUrls;

    /// Per-record handle cache (populated by loadRecords / loadRecord)
    QHash<QString /*recordId*/, RecordHandle> m_handles;
};

} // namespace Kalburator::Sync

#endif // REMOTECONTACTSBACKEND_H
