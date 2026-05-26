#include "fakecarddavserver.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QVariant>

namespace {

constexpr const char kBufProperty[] = "fakecarddav-buf";

// Look for "\r\n\r\n" header terminator and parse Content-Length to
// determine whether the full request body has arrived. Returns -1 if
// the headers haven't all arrived yet, otherwise the total request
// size in bytes (headers + body).
int requestSizeIfComplete(const QByteArray &buf)
{
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return -1;
    }
    const int bodyStart = headerEnd + 4;

    // Find Content-Length header (case-insensitive).
    int contentLength = 0;
    const QByteArray headers = buf.left(headerEnd);
    const QList<QByteArray> headerLines = headers.split('\n');
    for (const QByteArray &raw : headerLines) {
        QByteArray line = raw;
        if (line.endsWith('\r')) line.chop(1);
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        QByteArray name  = line.left(colon).trimmed().toLower();
        QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "content-length") {
            bool ok = false;
            contentLength = value.toInt(&ok);
            if (!ok) contentLength = 0;
            break;
        }
    }

    if (buf.size() >= bodyStart + contentLength) {
        return bodyStart + contentLength;
    }
    return -1;
}

} // namespace

FakeCardDavServer::FakeCardDavServer(QObject *parent)
    : QTcpServer(parent)
{
    m_addressbooks = {
        { QStringLiteral("personal"), QStringLiteral("Personal") }
    };
}

FakeCardDavServer::~FakeCardDavServer() = default;

bool FakeCardDavServer::startListening()
{
    return listen(QHostAddress::LocalHost, 0);
}

QUrl FakeCardDavServer::baseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(serverPort()));
}

void FakeCardDavServer::setAddressbooks(const QList<QPair<QString, QString>> &books)
{
    m_addressbooks = books;
}

void FakeCardDavServer::setSeedRecords(const QString &collectionId,
                                       const QList<QByteArray> &vcards)
{
    QHash<QString, VCardRecord> &col = m_store[collectionId];
    for (const QByteArray &vcard : vcards) {
        // Extract UID from vCard lines.
        QString uid;
        const QList<QByteArray> lines = vcard.split('\n');
        for (const QByteArray &line : lines) {
            QByteArray trimmed = line;
            if (trimmed.endsWith('\r')) trimmed.chop(1);
            if (trimmed.startsWith("UID:")) {
                uid = QString::fromUtf8(trimmed.mid(4).trimmed());
                break;
            }
        }
        if (uid.isEmpty()) continue;
        VCardRecord rec;
        rec.data = vcard;
        rec.etag = makeEtag(vcard);
        col.insert(uid, rec);
    }
}

bool FakeCardDavServer::hasContact(const QString &collectionId,
                                   const QString &uid) const
{
    auto it = m_store.constFind(collectionId);
    if (it == m_store.constEnd()) return false;
    return it->contains(uid);
}

QList<QByteArray> FakeCardDavServer::storedRecords(const QString &collectionId) const
{
    QList<QByteArray> result;
    auto it = m_store.constFind(collectionId);
    if (it == m_store.constEnd()) return result;
    for (auto recIt = it->constBegin(); recIt != it->constEnd(); ++recIt) {
        result.append(recIt.value().data);
    }
    return result;
}

void FakeCardDavServer::incomingConnection(qintptr socketDescriptor)
{
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        delete socket;
        return;
    }

    socket->setProperty(kBufProperty, QByteArray());

    QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray buf = socket->property(kBufProperty).toByteArray();
        buf.append(socket->readAll());
        const int total = requestSizeIfComplete(buf);
        if (total < 0) {
            socket->setProperty(kBufProperty, buf);
            return;
        }
        // Hand off only the first request.
        const QByteArray request = buf.left(total);
        socket->setProperty(kBufProperty, QByteArray());
        if (m_responseDelayMs > 0) {
            // Defer response to simulate a slow server. The delay lets the
            // test thread (which is blocked in QEventLoop::exec()) process
            // a cancel() call before the reply arrives.
            QTimer::singleShot(m_responseDelayMs, this, [this, socket, request]() {
                if (socket->isOpen())
                    handleRequest(socket, request);
            });
        } else {
            handleRequest(socket, request);
        }
    });

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);
}

// static
QByteArray FakeCardDavServer::extractHeader(const QByteArray &rawHeaders,
                                             const QByteArray &name)
{
    const QByteArray nameLower = name.toLower();
    const QList<QByteArray> lines = rawHeaders.split('\n');
    for (const QByteArray &raw : lines) {
        QByteArray line = raw;
        if (line.endsWith('\r')) line.chop(1);
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        if (line.left(colon).trimmed().toLower() == nameLower) {
            return line.mid(colon + 1).trimmed();
        }
    }
    return QByteArray();
}

// static
QString FakeCardDavServer::makeEtag(const QByteArray &data)
{
    const QByteArray hash =
        QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().left(12);
    return QStringLiteral("\"%1\"").arg(QString::fromLatin1(hash));
}

// static
QString FakeCardDavServer::uidFromPath(const QString &path)
{
    // Expect "/addressbooks/<user>/<book>/<uid>.vcf"
    if (!path.endsWith(QStringLiteral(".vcf")))
        return QString();
    const int lastSlash = path.lastIndexOf('/');
    if (lastSlash < 0)
        return QString();
    QString filename = path.mid(lastSlash + 1);
    filename.chop(4); // remove ".vcf"
    return filename;
}

void FakeCardDavServer::writeResponse(QTcpSocket *socket,
                                      int statusCode,
                                      const QByteArray &reasonPhrase,
                                      const QByteArray &body,
                                      const QByteArray &extraHeaders)
{
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + reasonPhrase + "\r\n";
    if (statusCode == 401) {
        resp += "WWW-Authenticate: Basic realm=\"fake\"\r\n";
    }
    if (!extraHeaders.isEmpty()) {
        resp += extraHeaders;
    }
    if (body.isEmpty()) {
        resp += "Content-Length: 0\r\n";
    } else {
        const QByteArray contentType = (statusCode == 207)
            ? "application/xml; charset=utf-8"
            : "text/vcard; charset=utf-8";
        resp += "Content-Type: " + contentType + "\r\n";
        resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    resp += "Connection: close\r\n\r\n";
    resp += body;
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

void FakeCardDavServer::handleRequest(QTcpSocket *socket,
                                      const QByteArray &fullRequest)
{
    if (m_return401) {
        writeResponse(socket, 401, "Unauthorized", QByteArray());
        return;
    }
    if (m_return500) {
        writeResponse(socket, 500, "Internal Server Error", QByteArray());
        return;
    }

    const int firstNewline = fullRequest.indexOf("\r\n");
    if (firstNewline <= 0) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }
    const QByteArray requestLine = fullRequest.left(firstNewline);
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    const QByteArray method = parts.at(0);
    const QString rawPath = QString::fromUtf8(parts.at(1));

    // RFC 6764 well-known bootstrap for NextCloud-style deployments.
    if (!m_contextPath.isEmpty()) {
        if (rawPath == QStringLiteral("/.well-known/carddav")) {
            const QByteArray loc = (m_contextPath + QStringLiteral("/")).toUtf8();
            writeResponse(socket, 301, "Moved Permanently", QByteArray(),
                          "Location: " + loc + "\r\n");
            return;
        }
        if (method == "PROPFIND"
            && (rawPath == QStringLiteral("/") || rawPath.isEmpty())) {
            // The bare root is the web UI, not a DAV collection.
            writeResponse(socket, 405, "Method Not Allowed", QByteArray());
            return;
        }
    }

    // Strip the context prefix so the routing below is path-shape agnostic.
    QString path = rawPath;
    if (!m_contextPath.isEmpty() && path.startsWith(m_contextPath)) {
        path = path.mid(m_contextPath.length());
        if (path.isEmpty())
            path = QStringLiteral("/");
    }

    // Split headers from body.
    const int headerEnd = fullRequest.indexOf("\r\n\r\n");
    const QByteArray rawHeaders = (headerEnd > 0)
        ? fullRequest.left(headerEnd)
        : fullRequest;
    const QByteArray body = (headerEnd > 0)
        ? fullRequest.mid(headerEnd + 4)
        : QByteArray();

    if (method == "PROPFIND") {
        QString xml;
        if (path == QStringLiteral("/") || path.isEmpty()) {
            xml = xmlForPrincipal();
        } else if (path.startsWith(QStringLiteral("/principals/users/"))
                   && path.endsWith('/')) {
            xml = xmlForHome();
        } else if (path.startsWith(QStringLiteral("/addressbooks/"))
                   && path.count('/') == 3) {
            // "/addressbooks/<user>/" — one trailing slash, 3 slashes total
            xml = xmlForAddressbooks();
        } else if (path.startsWith(QStringLiteral("/addressbooks/"))
                   && path.count('/') == 4) {
            // "/addressbooks/<user>/<book>/" — Depth:1 listing of vCards
            const QStringList segs = path.split('/', Qt::SkipEmptyParts);
            // segs: ["addressbooks", "<user>", "<book>"]
            const QString collId = (segs.size() >= 3) ? segs.at(2) : QString();
            xml = xmlForCards(collId);
        } else {
            writeResponse(socket, 404, "Not Found", QByteArray());
            return;
        }
        writeResponse(socket, 207, "Multi-Status", xml.toUtf8());

    } else if (method == "GET") {
        handleGet(socket, path);

    } else if (method == "PUT") {
        const QByteArray ifMatch    = extractHeader(rawHeaders, "If-Match");
        const QByteArray ifNoneMatch = extractHeader(rawHeaders, "If-None-Match");
        handlePut(socket, path, body, ifMatch, ifNoneMatch);

    } else if (method == "DELETE") {
        const QByteArray ifMatch = extractHeader(rawHeaders, "If-Match");
        handleDelete(socket, path, ifMatch);

    } else {
        writeResponse(socket, 405, "Method Not Allowed", QByteArray());
    }
}

QString FakeCardDavServer::xmlForPrincipal() const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\">\n"
        "  <d:response>\n"
        "    <d:href>/</d:href>\n"
        "    <d:propstat>\n"
        "      <d:prop>\n"
        "        <d:current-user-principal>\n"
        "          <d:href>%1/principals/users/testuser/</d:href>\n"
        "        </d:current-user-principal>\n"
        "      </d:prop>\n"
        "      <d:status>HTTP/1.1 200 OK</d:status>\n"
        "    </d:propstat>\n"
        "  </d:response>\n"
        "</d:multistatus>\n").arg(m_contextPath);
}

QString FakeCardDavServer::xmlForHome() const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\""
        " xmlns:card=\"urn:ietf:params:xml:ns:carddav\">\n"
        "  <d:response>\n"
        "    <d:href>%1/principals/users/testuser/</d:href>\n"
        "    <d:propstat>\n"
        "      <d:prop>\n"
        "        <card:addressbook-home-set>\n"
        "          <d:href>%1/addressbooks/testuser/</d:href>\n"
        "        </card:addressbook-home-set>\n"
        "      </d:prop>\n"
        "      <d:status>HTTP/1.1 200 OK</d:status>\n"
        "    </d:propstat>\n"
        "  </d:response>\n"
        "</d:multistatus>\n").arg(m_contextPath);
}

QString FakeCardDavServer::xmlForAddressbooks() const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\""
                          " xmlns:card=\"urn:ietf:params:xml:ns:carddav\">\n");
    for (const auto &book : m_addressbooks) {
        const QString &collId      = book.first;
        const QString &displayName = book.second;
        const QString href =
            QStringLiteral("%1/addressbooks/testuser/%2/").arg(m_contextPath, collId);
        xml += QStringLiteral("  <d:response>\n");
        xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(href);
        xml += QStringLiteral("    <d:propstat>\n");
        xml += QStringLiteral("      <d:prop>\n");
        xml += QStringLiteral(
            "        <d:resourcetype>"
            "<d:collection/><card:addressbook/>"
            "</d:resourcetype>\n");
        xml += QStringLiteral(
            "        <d:displayname>%1</d:displayname>\n").arg(displayName);
        xml += QStringLiteral("      </d:prop>\n");
        xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
        xml += QStringLiteral("    </d:propstat>\n");
        xml += QStringLiteral("  </d:response>\n");
    }
    xml += QStringLiteral("</d:multistatus>\n");
    return xml;
}

QString FakeCardDavServer::xmlForCards(const QString &collectionId) const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\""
                          " xmlns:card=\"urn:ietf:params:xml:ns:carddav\">\n");

    auto storeIt = m_store.find(collectionId);
    if (storeIt != m_store.end()) {
        for (auto it = storeIt->constBegin(); it != storeIt->constEnd(); ++it) {
            const QString href =
                QStringLiteral("/addressbooks/testuser/%1/%2.vcf")
                    .arg(collectionId, it.key());
            xml += QStringLiteral("  <d:response>\n");
            xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(href);
            xml += QStringLiteral("    <d:propstat>\n");
            xml += QStringLiteral("      <d:prop>\n");
            xml += QStringLiteral("        <d:getetag>%1</d:getetag>\n").arg(it.value().etag);
            xml += QStringLiteral("      </d:prop>\n");
            xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
            xml += QStringLiteral("    </d:propstat>\n");
            xml += QStringLiteral("  </d:response>\n");
        }
    }

    xml += QStringLiteral("</d:multistatus>\n");
    return xml;
}

void FakeCardDavServer::handleGet(QTcpSocket *socket, const QString &path)
{
    // Path: "/addressbooks/<user>/<book>/<uid>.vcf"
    const QString uid = uidFromPath(path);
    if (uid.isEmpty()) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    // Derive collectionId from the second-to-last path segment.
    // "/addressbooks/testuser/personal/abc.vcf" -> "personal"
    const QStringList segments = path.split('/', Qt::SkipEmptyParts);
    // segments: ["addressbooks", "<user>", "<book>", "<uid>.vcf"]
    if (segments.size() < 4) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }
    const QString collId = segments.at(2);

    auto storeIt = m_store.find(collId);
    if (storeIt == m_store.end()) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }
    auto recIt = storeIt->find(uid);
    if (recIt == storeIt->end()) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }

    const QByteArray etagHeader =
        ("ETag: " + recIt->etag.toUtf8() + "\r\n");
    writeResponse(socket, 200, "OK", recIt->data, etagHeader);
}

void FakeCardDavServer::handlePut(QTcpSocket *socket,
                                  const QString &path,
                                  const QByteArray &body,
                                  const QByteArray &ifMatch,
                                  const QByteArray &ifNoneMatch)
{
    const QString uid = uidFromPath(path);
    if (uid.isEmpty()) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    const QStringList segments = path.split('/', Qt::SkipEmptyParts);
    if (segments.size() < 4) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }
    const QString collId = segments.at(2);

    QHash<QString, VCardRecord> &col = m_store[collId];
    const bool isNew = !col.contains(uid);

    // If-None-Match: * — only create if resource does not already exist.
    if (!ifNoneMatch.isEmpty()) {
        if (ifNoneMatch.trimmed() == "*" && !isNew) {
            writeResponse(socket, 412, "Precondition Failed", QByteArray());
            return;
        }
    }

    // If-Match: <etag> — only update if current ETag matches.
    if (!ifMatch.isEmpty()) {
        if (isNew) {
            writeResponse(socket, 412, "Precondition Failed", QByteArray());
            return;
        }
        const QByteArray storedEtag = col.value(uid).etag.toUtf8();
        if (ifMatch.trimmed() != storedEtag) {
            writeResponse(socket, 412, "Precondition Failed", QByteArray());
            return;
        }
    }

    VCardRecord rec;
    rec.data = body;
    // Generate a unique ETag by salting with a counter so each PUT produces
    // a different ETag even for identical bodies.
    static int s_counter = 0;
    const QByteArray salted = body + QByteArray::number(++s_counter);
    rec.etag = makeEtag(salted);
    col.insert(uid, rec);

    const QByteArray etagHeader =
        ("ETag: " + rec.etag.toUtf8() + "\r\n");
    if (isNew) {
        writeResponse(socket, 201, "Created", QByteArray(), etagHeader);
    } else {
        writeResponse(socket, 204, "No Content", QByteArray(), etagHeader);
    }
}

bool FakeCardDavServer::bumpEtag(const QString &collectionId, const QString &uid)
{
    auto storeIt = m_store.find(collectionId);
    if (storeIt == m_store.end()) return false;
    auto recIt = storeIt->find(uid);
    if (recIt == storeIt->end()) return false;
    // Append a salt so the ETag changes without altering the data.
    static int s_bumpCounter = 0;
    const QByteArray salted = recIt->data + QByteArray("bump") + QByteArray::number(++s_bumpCounter);
    recIt->etag = makeEtag(salted);
    return true;
}

void FakeCardDavServer::handleDelete(QTcpSocket *socket,
                                     const QString &path,
                                     const QByteArray &ifMatch)
{
    const QString uid = uidFromPath(path);
    if (uid.isEmpty()) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    const QStringList segments = path.split('/', Qt::SkipEmptyParts);
    if (segments.size() < 4) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }
    const QString collId = segments.at(2);

    auto storeIt = m_store.find(collId);
    if (storeIt == m_store.end()) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }
    auto recIt = storeIt->find(uid);
    if (recIt == storeIt->end()) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }

    // Honor If-Match: only delete if ETag matches (or no If-Match given).
    if (!ifMatch.isEmpty() && ifMatch != recIt->etag.toUtf8()) {
        writeResponse(socket, 412, "Precondition Failed", QByteArray());
        return;
    }

    storeIt->erase(recIt);
    writeResponse(socket, 204, "No Content", QByteArray());
}
