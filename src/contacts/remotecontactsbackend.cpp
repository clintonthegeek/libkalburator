#include "remotecontactsbackend.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QXmlStreamReader>

namespace Kalburator::Sync {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

RemoteContactsBackend::RemoteContactsBackend(const QUrl    &serverRoot,
                                             const QString &username,
                                             const QString &password,
                                             QObject       *parent)
    : Kalburator::Sync::SyncBackendBase(parent)
    , m_serverRoot(serverRoot)
    , m_username(username)
    , m_password(password)
{
    // Ensure the server root has no trailing credentials baked in; we inject
    // them per-request via the Authorization header.
    m_serverRoot.setUserName(QString());
    m_serverRoot.setPassword(QString());
    qDebug() << "RemoteContactsBackend: server root" << m_serverRoot.toString();
}

RemoteContactsBackend::~RemoteContactsBackend() = default;

// RemoteContactsBackend::create(QVariantMap) factory was deleted in
// fanout-collapse Task 3.1 (spec §B): it had zero callers and the unlock
// is symbolic-only (no test/library consumer uses the legacy
// serverRoot/username/password config-map shape — providers always build
// the backend directly from their connect-time discovery).

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

void RemoteContactsBackend::cancel()
{
    m_cancelled = true;
    // Abort the in-flight reply (if any) so the blocking QEventLoop in the
    // active helper returns immediately. The helper will see an error from the
    // aborted reply and return an empty result; the caller then checks
    // isCancelled() to distinguish cancellation from a real error.
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

void RemoteContactsBackend::resetCancelled()
{
    m_cancelled = false;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void RemoteContactsBackend::registerAddressbookUrl(const QString &addressbookId,
                                                   const QUrl    &absoluteUrl)
{
    m_addressbookUrls.insert(addressbookId, absoluteUrl);
    qDebug() << "RemoteContactsBackend: registered addressbook"
             << addressbookId << "->" << absoluteUrl.toString();
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

QString RemoteContactsBackend::backendId() const
{
    static const QString typeName = QStringLiteral("carddav-contacts");
    const QByteArray h = QCryptographicHash::hash(
        (typeName + QLatin1Char(':') + m_serverRoot.toString()).toUtf8(),
        QCryptographicHash::Sha256);
    return typeName + QLatin1Char(':') + QString::fromLatin1(h.toHex().left(16));
}

QString RemoteContactsBackend::displayName() const
{
    return QStringLiteral("RemoteContactsBackend(%1)").arg(m_serverRoot.toString());
}

bool RemoteContactsBackend::isAvailable() const
{
    return m_serverRoot.isValid() && !m_serverRoot.isEmpty();
}

// ---------------------------------------------------------------------------
// Collections
// ---------------------------------------------------------------------------

QList<CollectionInfo> RemoteContactsBackend::availableCollections()
{
    QList<CollectionInfo> result;
    for (auto it = m_addressbookUrls.constBegin(); it != m_addressbookUrls.constEnd(); ++it) {
        CollectionInfo info;
        info.id   = it.key();
        info.name = it.key();
        info.path = it.value().toString();
        info.type = QStringLiteral("contacts");
        result.append(info);
    }
    return result;
}

CollectionInfo RemoteContactsBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.name = collectionId;
    info.type = QStringLiteral("contacts");
    if (m_addressbookUrls.contains(collectionId)) {
        info.path = m_addressbookUrls.value(collectionId).toString();
    }
    return info;
}

// ---------------------------------------------------------------------------
// Helper: shape detection from raw vCard bytes
// ---------------------------------------------------------------------------

// static
Kalburator::Shape::Shape RemoteContactsBackend::shapeFromVCard(const QByteArray &vcardBytes)
{
    // Handle empty bytes gracefully.
    if (vcardBytes.isEmpty()) {
        qWarning() << "RemoteContactsBackend::shapeFromVCard: empty vCard bytes, "
                   << "defaulting to vcard4";
        return Kalburator::Shape::Shape{
            Kalburator::Shape::DomainId{QStringLiteral("contacts")},
            Kalburator::Shape::EncodingId{QStringLiteral("vcard4")} };
    }

    // Scan for BEGIN:VCARD then inspect the next VERSION: line.
    // Robust against both LF (\n) and CRLF (\r\n) line endings.
    bool foundBegin = false;
    const QList<QByteArray> lines = vcardBytes.split('\n');
    for (const QByteArray &raw : lines) {
        QByteArray line = raw;
        // Strip CRLF or LF line ending
        if (line.endsWith('\r')) line.chop(1);
        line = line.trimmed();

        if (!foundBegin) {
            if (line.toUpper() == "BEGIN:VCARD")
                foundBegin = true;
            continue;
        }

        // First line after BEGIN:VCARD — check for VERSION:
        if (line.toUpper().startsWith("VERSION:")) {
            const QByteArray version = line.mid(8).trimmed();

            // vCard 3.0 → transcoded by engine
            if (version == "3.0") {
                return Kalburator::Shape::Shape{
                    Kalburator::Shape::DomainId{QStringLiteral("contacts")},
                    Kalburator::Shape::EncodingId{QStringLiteral("vcard3")} };
            }

            // vCard 2.1 → best-effort transcode as vcard3 (with warning)
            if (version == "2.1") {
                qWarning() << "RemoteContactsBackend::shapeFromVCard: vCard 2.1 detected, "
                           << "tagging as vcard3 (best-effort transcode)";
                return Kalburator::Shape::Shape{
                    Kalburator::Shape::DomainId{QStringLiteral("contacts")},
                    Kalburator::Shape::EncodingId{QStringLiteral("vcard3")} };
            }

            // 4.0 or anything else → vcard4
            return Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("contacts")},
                Kalburator::Shape::EncodingId{QStringLiteral("vcard4")} };
        }

        // VERSION: not the next line — stop scanning (malformed but don't crash)
        break;
    }

    // No VERSION: line found — assume latest (vcard4)
    qWarning() << "RemoteContactsBackend::shapeFromVCard: no VERSION: line found, "
               << "assuming vcard4";
    return Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("contacts")},
        Kalburator::Shape::EncodingId{QStringLiteral("vcard4")} };
}

// ---------------------------------------------------------------------------
// Helper: build BackendRecord from vCard bytes
// ---------------------------------------------------------------------------

// static
BackendRecord RemoteContactsBackend::recordFromVCard(const QString    &recordId,
                                                     const QByteArray &vcardBytes)
{
    BackendRecord rec;
    rec.id   = recordId;
    rec.type = QStringLiteral("contacts");
    rec.data = vcardBytes;
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(vcardBytes, QCryptographicHash::Sha256).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    rec.isDeleted    = false;

    // Shape is stored in BackendRecord::type as domain + encoding pair;
    // the engine reads nativeShapes() on the backend.  We tag the "type"
    // field with the encoding so the engine can inspect it if needed.
    const auto shape = shapeFromVCard(vcardBytes);
    rec.type = QStringLiteral("contacts/%1").arg(shape.encoding.toString());
    return rec;
}

// ---------------------------------------------------------------------------
// Helper: URL construction
// ---------------------------------------------------------------------------

QUrl RemoteContactsBackend::absoluteUrl(const QString &href) const
{
    // href may be absolute (http://...) or server-relative (/path/to/item.vcf)
    if (href.startsWith(QStringLiteral("http://")) ||
        href.startsWith(QStringLiteral("https://"))) {
        return QUrl(href);
    }
    QUrl base = m_serverRoot;
    base.setPath(href);
    return base;
}

QUrl RemoteContactsBackend::credentialsUrl(const QUrl &base) const
{
    QUrl u = base;
    u.setUserName(m_username);
    u.setPassword(m_password);
    return u;
}

// ---------------------------------------------------------------------------
// PROPFIND Depth:1 — list hrefs + ETags in an addressbook
// ---------------------------------------------------------------------------

QMap<QString, QString> RemoteContactsBackend::propfindDepth1(const QUrl &addressbookUrl)
{
    if (m_cancelled) return {};

    // Build request with credentials via Authorization header
    QUrl reqUrl = addressbookUrl;
    reqUrl.setUserName(QString());
    reqUrl.setPassword(QString());

    QNetworkRequest request(reqUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/xml; charset=utf-8"));
    request.setRawHeader("Depth", "1");
    if (!m_username.isEmpty() || !m_password.isEmpty()) {
        const QByteArray creds =
            (m_username + QLatin1Char(':') + m_password).toUtf8().toBase64();
        request.setRawHeader("Authorization", "Basic " + creds);
    }

    const QByteArray body =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:propfind xmlns:D=\"DAV:\">"
        "  <D:prop>"
        "    <D:getetag/>"
        "  </D:prop>"
        "</D:propfind>";

    QNetworkAccessManager nam;
    QEventLoop loop;
    QMap<QString, QString> result; // href -> etag

    QNetworkReply *reply = nam.sendCustomRequest(request, "PROPFIND", body);
    m_currentReply = reply;
    QObject::connect(reply, &QNetworkReply::finished, &loop, [this, reply, &loop, &result]() {
        m_currentReply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status == 401 || status >= 500) {
            if (reply->error() != QNetworkReply::OperationCanceledError) {
                qWarning() << "RemoteContactsBackend::propfindDepth1: HTTP"
                           << status << reply->errorString();
            }
            reply->deleteLater();
            loop.quit();
            return;
        }

        const QByteArray data = reply->readAll();
        reply->deleteLater();

        // Parse the multistatus XML for <D:href> / <D:getetag> pairs.
        QXmlStreamReader xml(data);
        QString currentHref;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const QString localName = xml.name().toString();
                if (localName == QStringLiteral("href")) {
                    currentHref = xml.readElementText().trimmed();
                } else if (localName == QStringLiteral("getetag")) {
                    const QString etag = xml.readElementText().trimmed();
                    if (!currentHref.isEmpty() && currentHref.endsWith(QStringLiteral(".vcf"))) {
                        result.insert(currentHref, etag);
                    }
                    currentHref.clear();
                }
            }
        }
        loop.quit();
    });
    loop.exec();
    return result;
}

// ---------------------------------------------------------------------------
// GET a single vCard
// ---------------------------------------------------------------------------

QByteArray RemoteContactsBackend::getVCard(const QUrl &absoluteItemUrl)
{
    if (m_cancelled) return {};

    QUrl reqUrl = absoluteItemUrl;
    reqUrl.setUserName(QString());
    reqUrl.setPassword(QString());

    QNetworkRequest request(reqUrl);
    request.setRawHeader("Accept", "text/vcard; version=4.0");
    if (!m_username.isEmpty() || !m_password.isEmpty()) {
        const QByteArray creds =
            (m_username + QLatin1Char(':') + m_password).toUtf8().toBase64();
        request.setRawHeader("Authorization", "Basic " + creds);
    }

    QNetworkAccessManager nam;
    QEventLoop loop;
    QByteArray vcardBytes;

    QNetworkReply *reply = nam.get(request);
    m_currentReply = reply;
    QObject::connect(reply, &QNetworkReply::finished, &loop, [this, reply, &loop, &vcardBytes]() {
        m_currentReply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status == 401 || status >= 500) {
            if (reply->error() != QNetworkReply::OperationCanceledError) {
                qWarning() << "RemoteContactsBackend::getVCard: HTTP"
                           << status << reply->errorString();
            }
        } else {
            vcardBytes = reply->readAll();
        }
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();
    return vcardBytes;
}

// ---------------------------------------------------------------------------
// loadRecords — PROPFIND Depth:1 + GET each .vcf
// ---------------------------------------------------------------------------

QList<BackendRecord> RemoteContactsBackend::loadRecords(const QString &collectionId)
{
    if (m_cancelled) return {};

    if (!m_addressbookUrls.contains(collectionId)) {
        qWarning() << "RemoteContactsBackend::loadRecords: unknown collection"
                   << collectionId;
        return {};
    }

    const QUrl addressbookUrl = m_addressbookUrls.value(collectionId);
    const QMap<QString, QString> hrefEtags = propfindDepth1(addressbookUrl);

    if (hrefEtags.isEmpty()) {
        // Either empty addressbook, error, or cancelled — all produce an empty list.
        return {};
    }

    QList<BackendRecord> result;
    result.reserve(hrefEtags.size());

    for (auto it = hrefEtags.constBegin(); it != hrefEtags.constEnd(); ++it) {
        if (m_cancelled) return {};

        const QString &href = it.key();
        const QString &etag = it.value();

        // Derive uid from href: basename without ".vcf"
        const int lastSlash = href.lastIndexOf(QLatin1Char('/'));
        QString filename = (lastSlash >= 0) ? href.mid(lastSlash + 1) : href;
        if (filename.endsWith(QStringLiteral(".vcf")))
            filename.chop(4);
        const QString uid = filename;
        const QString recordId = collectionId + QLatin1Char(':') + uid;

        const QUrl itemUrl = absoluteUrl(href);
        const QByteArray vcardBytes = getVCard(itemUrl);
        if (vcardBytes.isEmpty()) {
            qWarning() << "RemoteContactsBackend::loadRecords: empty body for" << href;
            continue;
        }

        BackendRecord rec = recordFromVCard(recordId, vcardBytes);

        // Store handle for loadRecord() re-use
        RecordHandle handle;
        handle.href = itemUrl;
        handle.etag = etag;
        m_handles.insert(recordId, handle);

        result.append(rec);
    }

    return result;
}

// ---------------------------------------------------------------------------
// loadRecord — single GET for a known record
// ---------------------------------------------------------------------------

std::optional<BackendRecord> RemoteContactsBackend::loadRecord(const QString &recordId)
{
    // Try cached handle first
    if (m_handles.contains(recordId)) {
        const RecordHandle &h = m_handles.value(recordId);
        const QByteArray vcardBytes = getVCard(h.href);
        if (!vcardBytes.isEmpty())
            return recordFromVCard(recordId, vcardBytes);
        return std::nullopt;
    }

    // No cached handle — derive URL from collectionId + uid
    // recordId format: "<collectionId>:<uid>"
    const int sep = recordId.indexOf(QLatin1Char(':'));
    if (sep < 0) {
        qWarning() << "RemoteContactsBackend::loadRecord: malformed recordId" << recordId;
        return std::nullopt;
    }
    const QString collectionId = recordId.left(sep);
    const QString uid          = recordId.mid(sep + 1);

    if (!m_addressbookUrls.contains(collectionId)) {
        qWarning() << "RemoteContactsBackend::loadRecord: unknown collection" << collectionId;
        return std::nullopt;
    }

    QUrl itemUrl = m_addressbookUrls.value(collectionId);
    QString path = itemUrl.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    path += uid + QStringLiteral(".vcf");
    itemUrl.setPath(path);

    const QByteArray vcardBytes = getVCard(itemUrl);
    if (vcardBytes.isEmpty())
        return std::nullopt;

    BackendRecord rec = recordFromVCard(recordId, vcardBytes);

    RecordHandle handle;
    handle.href = itemUrl;
    handle.etag = QString(); // not known without PROPFIND
    m_handles.insert(recordId, handle);

    return rec;
}

// ---------------------------------------------------------------------------
// Helper: extract UID from raw vCard bytes
// ---------------------------------------------------------------------------

// static
QString RemoteContactsBackend::extractUid(const QByteArray &vcardBytes)
{
    const QList<QByteArray> lines = vcardBytes.split('\n');
    for (const QByteArray &raw : lines) {
        QByteArray line = raw;
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith("UID:") || line.startsWith("UID;")) {
            // UID: value (may have parameters like UID;VALUE=text:...)
            const int colon = line.indexOf(':');
            if (colon >= 0)
                return QString::fromUtf8(line.mid(colon + 1).trimmed());
        }
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Helper: PUT a vCard to the server
//
// Returns the HTTP status code. On success (200/201/204), *outEtag is set to
// the ETag returned by the server (may be empty if the server omits it).
// ---------------------------------------------------------------------------

int RemoteContactsBackend::putVCard(const QUrl      &absoluteItemUrl,
                                    const QByteArray &vcardBytes,
                                    const QByteArray &ifMatch,
                                    const QByteArray &ifNoneMatch,
                                    QString          *outEtag)
{
    if (m_cancelled) return 0;

    QUrl reqUrl = absoluteItemUrl;
    reqUrl.setUserName(QString());
    reqUrl.setPassword(QString());

    QNetworkRequest request(reqUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("text/vcard; charset=utf-8"));
    if (!m_username.isEmpty() || !m_password.isEmpty()) {
        const QByteArray creds =
            (m_username + QLatin1Char(':') + m_password).toUtf8().toBase64();
        request.setRawHeader("Authorization", "Basic " + creds);
    }
    if (!ifMatch.isEmpty())
        request.setRawHeader("If-Match", ifMatch);
    if (!ifNoneMatch.isEmpty())
        request.setRawHeader("If-None-Match", ifNoneMatch);

    QNetworkAccessManager nam;
    QEventLoop loop;
    int statusCode = 0;
    QString responseEtag;

    QNetworkReply *reply = nam.put(request, vcardBytes);
    m_currentReply = reply;
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [this, reply, &loop, &responseEtag, &statusCode]() {
        m_currentReply = nullptr;
        statusCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 201 || statusCode == 200 || statusCode == 204) {
            responseEtag = QString::fromUtf8(reply->rawHeader("ETag"));
        } else if (reply->error() != QNetworkReply::OperationCanceledError) {
            qWarning() << "RemoteContactsBackend::putVCard: HTTP" << statusCode
                       << reply->errorString();
        }
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();

    if (outEtag)
        *outEtag = responseEtag;
    return statusCode;
}

// ---------------------------------------------------------------------------
// Helper: DELETE a vCard from the server
// ---------------------------------------------------------------------------

int RemoteContactsBackend::deleteVCard(const QUrl      &absoluteItemUrl,
                                       const QByteArray &ifMatch)
{
    if (m_cancelled) return 0;

    QUrl reqUrl = absoluteItemUrl;
    reqUrl.setUserName(QString());
    reqUrl.setPassword(QString());

    QNetworkRequest request(reqUrl);
    if (!m_username.isEmpty() || !m_password.isEmpty()) {
        const QByteArray creds =
            (m_username + QLatin1Char(':') + m_password).toUtf8().toBase64();
        request.setRawHeader("Authorization", "Basic " + creds);
    }
    if (!ifMatch.isEmpty())
        request.setRawHeader("If-Match", ifMatch);

    QNetworkAccessManager nam;
    QEventLoop loop;
    int statusCode = 0;

    QNetworkReply *reply = nam.deleteResource(request);
    m_currentReply = reply;
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [this, reply, &loop, &statusCode]() {
        m_currentReply = nullptr;
        statusCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode != 204 && statusCode != 200) {
            if (reply->error() != QNetworkReply::OperationCanceledError) {
                qWarning() << "RemoteContactsBackend::deleteVCard: HTTP" << statusCode
                           << reply->errorString();
            }
        }
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();
    return statusCode;
}

// ---------------------------------------------------------------------------
// createRecord — PUT to <collection>/<uid>.vcf with If-None-Match: *
// ---------------------------------------------------------------------------

QString RemoteContactsBackend::createRecord(const QString     &collectionId,
                                             const BackendRecord &record)
{
    if (m_cancelled) return {};

    if (!m_addressbookUrls.contains(collectionId)) {
        qWarning() << "RemoteContactsBackend::createRecord: unknown collection"
                   << collectionId;
        return {};
    }
    if (record.data.isEmpty()) {
        qWarning() << "RemoteContactsBackend::createRecord: empty vCard bytes";
        return {};
    }

    // Extract UID from vCard, or generate one.
    QString uid = extractUid(record.data);
    if (uid.isEmpty()) {
        uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        qDebug() << "RemoteContactsBackend::createRecord: generated UID" << uid;
    }

    // Build the item URL: <addressbook>/<uid>.vcf
    QUrl itemUrl = m_addressbookUrls.value(collectionId);
    QString path = itemUrl.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    path += uid + QStringLiteral(".vcf");
    itemUrl.setPath(path);

    QString newEtag;
    const int status = putVCard(itemUrl, record.data,
                                QByteArray()  /*ifMatch — none*/,
                                QByteArray("*") /*ifNoneMatch — must not exist*/,
                                &newEtag);

    if (status != 201 && status != 200 && status != 204) {
        qWarning() << "RemoteContactsBackend::createRecord: PUT failed HTTP" << status;
        return {};
    }

    const QString recordId = collectionId + QLatin1Char(':') + uid;

    // Cache the handle for subsequent updateRecord / deleteRecord calls.
    RecordHandle handle;
    handle.href = itemUrl;
    handle.etag = newEtag;
    m_handles.insert(recordId, handle);

    return recordId;
}

// ---------------------------------------------------------------------------
// updateRecord — PUT to existing href with If-Match: <stored-etag>
// ---------------------------------------------------------------------------

bool RemoteContactsBackend::updateRecord(const BackendRecord &record)
{
    if (m_cancelled) return false;

    if (record.id.isEmpty() || record.data.isEmpty()) {
        qWarning() << "RemoteContactsBackend::updateRecord: empty id or data";
        return false;
    }

    // We need a handle (href + etag) from a prior loadRecords / createRecord.
    if (!m_handles.contains(record.id)) {
        qWarning() << "RemoteContactsBackend::updateRecord: no handle for"
                   << record.id << "— call loadRecords first";
        return false;
    }

    const RecordHandle h = m_handles.value(record.id);

    QString newEtag;
    const int status = putVCard(h.href, record.data,
                                h.etag.toUtf8() /*ifMatch*/,
                                QByteArray()    /*ifNoneMatch — none*/,
                                &newEtag);

    if (status == 204 || status == 200 || status == 201) {
        // Update cached ETag so the next updateRecord uses the fresh one.
        RecordHandle updated = h;
        updated.etag = newEtag;
        m_handles.insert(record.id, updated);
        return true;
    }

    qWarning() << "RemoteContactsBackend::updateRecord: PUT failed HTTP"
               << status << "for" << record.id;
    return false;
}

// ---------------------------------------------------------------------------
// deleteRecord — DELETE with If-Match: <stored-etag>
// ---------------------------------------------------------------------------

bool RemoteContactsBackend::deleteRecord(const QString &recordId)
{
    if (m_cancelled) return false;

    if (recordId.isEmpty()) {
        qWarning() << "RemoteContactsBackend::deleteRecord: empty recordId";
        return false;
    }

    if (!m_handles.contains(recordId)) {
        qWarning() << "RemoteContactsBackend::deleteRecord: no handle for"
                   << recordId << "— call loadRecords first";
        return false;
    }

    const RecordHandle h = m_handles.value(recordId);
    const int status = deleteVCard(h.href, h.etag.toUtf8());

    if (status == 204 || status == 200) {
        m_handles.remove(recordId);
        return true;
    }

    qWarning() << "RemoteContactsBackend::deleteRecord: DELETE failed HTTP"
               << status << "for" << recordId;
    return false;
}

// ---------------------------------------------------------------------------
// modifiedSince — full load + filter (no CTag optimisation in Task 5)
// ---------------------------------------------------------------------------

QList<BackendRecord> RemoteContactsBackend::modifiedSince(const QString  &collectionId,
                                                          const QDateTime &since)
{
    const QList<BackendRecord> all = loadRecords(collectionId);
    if (!since.isValid())
        return all;

    QList<BackendRecord> result;
    for (const auto &rec : all) {
        if (rec.lastModified > since)
            result.append(rec);
    }
    return result;
}

} // namespace Kalburator::Sync
