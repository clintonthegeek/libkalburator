#include "fakecaldavserver.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QVariant>

namespace {

constexpr const char kBufProperty[] = "fakecaldav-buf";

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

FakeCalDavServer::FakeCalDavServer(QObject *parent)
    : QTcpServer(parent)
{
    m_calendars = {
        { QStringLiteral("Personal"),
          QStringLiteral("/calendars/testuser/personal/") }
    };
}

FakeCalDavServer::~FakeCalDavServer() = default;

bool FakeCalDavServer::startListening()
{
    return listen(QHostAddress::LocalHost, 0);
}

QUrl FakeCalDavServer::baseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(serverPort()));
}

void FakeCalDavServer::setCalendars(const QList<QPair<QString, QString>> &cals)
{
    m_calendars = cals;
}

void FakeCalDavServer::setSeedEvents(const QString &collectionHref,
                                     const QList<QByteArray> &events)
{
    QHash<QString, IcsRecord> &col = m_store[collectionHref];
    for (const QByteArray &ics : events) {
        const QString uid = uidFromIcs(ics);
        if (uid.isEmpty()) continue;
        IcsRecord rec;
        rec.data = ics;
        rec.etag = makeEtag(ics);
        col.insert(uid, rec);
    }
}

bool FakeCalDavServer::hasEvent(const QString &collectionHref,
                                const QString &uid) const
{
    auto it = m_store.constFind(collectionHref);
    if (it == m_store.constEnd()) return false;
    return it->contains(uid);
}

QList<QByteArray> FakeCalDavServer::storedEvents(
    const QString &collectionHref) const
{
    QList<QByteArray> result;
    auto it = m_store.constFind(collectionHref);
    if (it == m_store.constEnd()) return result;
    for (const auto &rec : *it) {
        result.append(rec.data);
    }
    return result;
}

void FakeCalDavServer::incomingConnection(qintptr socketDescriptor)
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
        const QByteArray request = buf.left(total);
        socket->setProperty(kBufProperty, QByteArray());
        handleRequest(socket, request);
    });

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);
}

void FakeCalDavServer::writeResponse(QTcpSocket *socket,
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
            : "text/calendar; charset=utf-8";
        resp += "Content-Type: " + contentType + "\r\n";
        resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    resp += "Connection: close\r\n\r\n";
    resp += body;
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

void FakeCalDavServer::handleRequest(QTcpSocket *socket,
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
    const QString path = QString::fromUtf8(parts.at(1));

    const int headerEnd = fullRequest.indexOf("\r\n\r\n");
    const QByteArray body = (headerEnd > 0)
        ? fullRequest.mid(headerEnd + 4)
        : QByteArray();

    if (method == "PROPFIND") {
        QString xml;
        if (path == QStringLiteral("/") || path.isEmpty()) {
            xml = xmlForPrincipal();
        } else if (path == QStringLiteral("/principals/users/testuser/")) {
            xml = xmlForHome();
        } else if (path == QStringLiteral("/calendars/testuser/")) {
            xml = xmlForCalendars();
        } else {
            // Depth:0 PROPFIND on a collection (e.g. for CTag) — return 404
            // since our fake server does not implement CS:getctag. The backend
            // skips the CTag optimisation when the PROPFIND fails, which is
            // fine for tests that only need the first sync.
            writeResponse(socket, 404, "Not Found", QByteArray());
            return;
        }
        writeResponse(socket, 207, "Multi-Status", xml.toUtf8());

    } else if (method == "REPORT") {
        handleReport(socket, path, body);

    } else if (method == "PUT") {
        handlePut(socket, path, body);

    } else {
        writeResponse(socket, 405, "Method Not Allowed", QByteArray());
    }
}

void FakeCalDavServer::handleReport(QTcpSocket *socket,
                                    const QString &path,
                                    const QByteArray &body)
{
    // Find the collection href this REPORT targets.
    // path must be one of the known collection hrefs.
    QString collectionHref;
    for (const auto &cal : m_calendars) {
        if (path == cal.second) {
            collectionHref = cal.second;
            break;
        }
    }
    if (collectionHref.isEmpty()) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }

    // Distinguish calendar-query (ETag list) from calendar-multiget (full data)
    // by looking for the report type string in the request body.
    if (body.contains("calendar-multiget")) {
        const QList<QString> hrefs = parseHrefsFromBody(body);
        writeResponse(socket, 207, "Multi-Status",
                      xmlForCalendarMultiget(collectionHref, hrefs));
    } else {
        // calendar-query or anything else — return ETag list for all events.
        writeResponse(socket, 207, "Multi-Status",
                      xmlForCalendarQuery(collectionHref));
    }
}

void FakeCalDavServer::handlePut(QTcpSocket *socket,
                                 const QString &path,
                                 const QByteArray &body)
{
    if (!path.endsWith(QStringLiteral(".ics"))) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    // Derive collection href by stripping the filename.
    const int lastSlash = path.lastIndexOf('/');
    if (lastSlash <= 0) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }
    const QString collectionHref = path.left(lastSlash + 1);
    const QString uid = uidFromPath(path);
    if (uid.isEmpty()) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    QHash<QString, IcsRecord> &col = m_store[collectionHref];
    const bool isNew = !col.contains(uid);

    IcsRecord rec;
    rec.data = body;
    // Salt with a counter so repeated PUTs produce distinct ETags.
    static int s_counter = 0;
    rec.etag = makeEtag(body + QByteArray::number(++s_counter));
    col.insert(uid, rec);

    const QByteArray etagHeader =
        ("ETag: " + rec.etag.toUtf8() + "\r\n");
    if (isNew) {
        writeResponse(socket, 201, "Created", QByteArray(), etagHeader);
    } else {
        writeResponse(socket, 204, "No Content", QByteArray(), etagHeader);
    }
}

QString FakeCalDavServer::xmlForPrincipal() const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\">\n"
        "  <d:response>\n"
        "    <d:href>/</d:href>\n"
        "    <d:propstat>\n"
        "      <d:prop>\n"
        "        <d:current-user-principal>\n"
        "          <d:href>/principals/users/testuser/</d:href>\n"
        "        </d:current-user-principal>\n"
        "      </d:prop>\n"
        "      <d:status>HTTP/1.1 200 OK</d:status>\n"
        "    </d:propstat>\n"
        "  </d:response>\n"
        "</d:multistatus>\n");
}

QString FakeCalDavServer::xmlForHome() const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\" xmlns:cal=\"urn:ietf:params:xml:ns:caldav\">\n"
        "  <d:response>\n"
        "    <d:href>/principals/users/testuser/</d:href>\n"
        "    <d:propstat>\n"
        "      <d:prop>\n"
        "        <cal:calendar-home-set>\n"
        "          <d:href>/calendars/testuser/</d:href>\n"
        "        </cal:calendar-home-set>\n"
        "      </d:prop>\n"
        "      <d:status>HTTP/1.1 200 OK</d:status>\n"
        "    </d:propstat>\n"
        "  </d:response>\n"
        "</d:multistatus>\n");
}

QString FakeCalDavServer::xmlForCalendars() const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\" "
                          "xmlns:cal=\"urn:ietf:params:xml:ns:caldav\">\n");
    for (const auto &cal : m_calendars) {
        xml += QStringLiteral("  <d:response>\n");
        xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(cal.second);
        xml += QStringLiteral("    <d:propstat>\n");
        xml += QStringLiteral("      <d:prop>\n");
        xml += QStringLiteral(
            "        <d:resourcetype><d:collection/><cal:calendar/></d:resourcetype>\n");
        xml += QStringLiteral(
            "        <d:displayname>%1</d:displayname>\n").arg(cal.first);
        xml += QStringLiteral(
            "        <cal:supported-calendar-component-set>"
            "<cal:comp name=\"VEVENT\"/>"
            "</cal:supported-calendar-component-set>\n");
        xml += QStringLiteral("      </d:prop>\n");
        xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
        xml += QStringLiteral("    </d:propstat>\n");
        xml += QStringLiteral("  </d:response>\n");
    }
    xml += QStringLiteral("</d:multistatus>\n");
    return xml;
}

QByteArray FakeCalDavServer::xmlForCalendarQuery(
    const QString &collectionHref) const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\""
                          " xmlns:c=\"urn:ietf:params:xml:ns:caldav\">\n");

    auto it = m_store.constFind(collectionHref);
    if (it != m_store.constEnd()) {
        for (auto recIt = it->constBegin(); recIt != it->constEnd(); ++recIt) {
            const QString href =
                collectionHref + recIt.key() + QStringLiteral(".ics");
            xml += QStringLiteral("  <d:response>\n");
            xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(href);
            xml += QStringLiteral("    <d:propstat>\n");
            xml += QStringLiteral("      <d:prop>\n");
            xml += QStringLiteral("        <d:getetag>%1</d:getetag>\n")
                       .arg(recIt.value().etag);
            xml += QStringLiteral("      </d:prop>\n");
            xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
            xml += QStringLiteral("    </d:propstat>\n");
            xml += QStringLiteral("  </d:response>\n");
        }
    }

    xml += QStringLiteral("</d:multistatus>\n");
    return xml.toUtf8();
}

QByteArray FakeCalDavServer::xmlForCalendarMultiget(
    const QString &collectionHref,
    const QList<QString> &hrefs) const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\""
                          " xmlns:c=\"urn:ietf:params:xml:ns:caldav\">\n");

    auto storeIt = m_store.constFind(collectionHref);

    for (const QString &href : hrefs) {
        const QString uid = uidFromPath(href);
        if (uid.isEmpty()) continue;

        xml += QStringLiteral("  <d:response>\n");
        xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(href);
        xml += QStringLiteral("    <d:propstat>\n");
        xml += QStringLiteral("      <d:prop>\n");

        if (storeIt != m_store.constEnd()) {
            auto recIt = storeIt->constFind(uid);
            if (recIt != storeIt->constEnd()) {
                xml += QStringLiteral("        <d:getetag>%1</d:getetag>\n")
                           .arg(recIt->etag);
                xml += QStringLiteral("        <c:calendar-data>%1</c:calendar-data>\n")
                           .arg(QString::fromUtf8(recIt->data));
                xml += QStringLiteral("      </d:prop>\n");
                xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
            } else {
                xml += QStringLiteral("      </d:prop>\n");
                xml += QStringLiteral("      <d:status>HTTP/1.1 404 Not Found</d:status>\n");
            }
        } else {
            xml += QStringLiteral("      </d:prop>\n");
            xml += QStringLiteral("      <d:status>HTTP/1.1 404 Not Found</d:status>\n");
        }

        xml += QStringLiteral("    </d:propstat>\n");
        xml += QStringLiteral("  </d:response>\n");
    }

    xml += QStringLiteral("</d:multistatus>\n");
    return xml.toUtf8();
}

// static
QString FakeCalDavServer::uidFromIcs(const QByteArray &ics)
{
    const QList<QByteArray> lines = ics.split('\n');
    for (const QByteArray &raw : lines) {
        QByteArray line = raw;
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith("UID:")) {
            return QString::fromUtf8(line.mid(4).trimmed());
        }
    }
    return QString();
}

// static
QString FakeCalDavServer::uidFromPath(const QString &path)
{
    // Expect "/calendars/<user>/<cal>/<uid>.ics"
    if (!path.endsWith(QStringLiteral(".ics")))
        return QString();
    const int lastSlash = path.lastIndexOf('/');
    if (lastSlash < 0)
        return QString();
    QString filename = path.mid(lastSlash + 1);
    filename.chop(4); // remove ".ics"
    return filename;
}

// static
QString FakeCalDavServer::makeEtag(const QByteArray &data)
{
    const QByteArray hash =
        QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().left(12);
    return QStringLiteral("\"%1\"").arg(QString::fromLatin1(hash));
}

// static
QList<QString> FakeCalDavServer::parseHrefsFromBody(const QByteArray &body)
{
    QList<QString> hrefs;
    // Simple text scan for <D:href>...</D:href> or <d:href>...</d:href>.
    // KDAV emits namespaced hrefs; we match any variant of the tag name.
    QByteArray lower = body.toLower();
    int pos = 0;
    while (pos < body.size()) {
        // Find opening href tag (any namespace prefix)
        int tagStart = lower.indexOf(":href>", pos);
        if (tagStart < 0) break;
        const int valueStart = tagStart + 6; // length of ":href>"

        int closeTag = lower.indexOf("</", valueStart);
        if (closeTag < 0) break;

        const QByteArray hrefBytes = body.mid(valueStart, closeTag - valueStart).trimmed();
        const QString href = QString::fromUtf8(hrefBytes);
        if (!href.isEmpty()) {
            hrefs.append(href);
        }
        pos = closeTag + 2;
    }
    return hrefs;
}
