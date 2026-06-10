#include "fakecaldavserver.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDomDocument>
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
    m_requestCounts.clear();
    return listen(QHostAddress::LocalHost, 0);
}

int FakeCalDavServer::requestCount(const QByteArray &method) const
{
    return m_requestCounts.value(method);
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

    // Count well-formed requests per method so tests can assert request shape
    // (e.g. "the primed loadCalendars path issues zero additional PROPFINDs").
    ++m_requestCounts[method];

    const int headerEnd = fullRequest.indexOf("\r\n\r\n");
    const QByteArray body = (headerEnd > 0)
        ? fullRequest.mid(headerEnd + 4)
        : QByteArray();

    // RFC 6764 well-known bootstrap for NextCloud-style deployments.
    if (!m_contextPath.isEmpty()) {
        if (path == QStringLiteral("/.well-known/caldav")) {
            const QByteArray loc = (m_contextPath + QStringLiteral("/")).toUtf8();
            writeResponse(socket, 301, "Moved Permanently", QByteArray(),
                          "Location: " + loc + "\r\n");
            return;
        }
        if (method == "PROPFIND"
            && (path == QStringLiteral("/") || path.isEmpty())) {
            // The bare root is the web UI, not a DAV collection.
            writeResponse(socket, 405, "Method Not Allowed", QByteArray());
            return;
        }
    }

    // Paths the DAV walk targets, shifted under the (possibly empty) context path.
    const QString principalRoot = m_contextPath + QStringLiteral("/");
    const QString principalPath  =
        m_contextPath + QStringLiteral("/principals/users/testuser/");
    const QString calendarsPath  =
        m_contextPath + QStringLiteral("/calendars/testuser/");

    if (method == "PROPFIND") {
        QString xml;
        if (path == principalRoot || (m_contextPath.isEmpty() && path.isEmpty())) {
            xml = xmlForPrincipal();
        } else if (path == principalPath) {
            xml = xmlForHome();
        } else if (path == calendarsPath) {
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

    } else if (method == "DELETE") {
        handleDelete(socket, path);

    } else if (method == "MKCALENDAR") {
        handleMkCalendar(socket, path);

    } else if (method == "PROPPATCH") {
        handleProppatch(socket, path);

    } else {
        writeResponse(socket, 405, "Method Not Allowed", QByteArray());
    }
}

bool FakeCalDavServer::isKnownCollection(const QString &href) const
{
    if (m_createdCollections.contains(href)) return true;
    for (const auto &cal : m_calendars) {
        if (cal.second == href) return true;
    }
    return false;
}

void FakeCalDavServer::handleMkCalendar(QTcpSocket *socket, const QString &path)
{
    // RFC 4791 §5.3.1: MKCALENDAR on an existing collection is an error;
    // real servers answer 405 (Radicale) or 409. The backend treats both as
    // "already exists" and proceeds idempotently.
    if (isKnownCollection(path)) {
        writeResponse(socket, 405, "Method Not Allowed", QByteArray());
        return;
    }
    m_createdCollections.insert(path);
    writeResponse(socket, 201, "Created", QByteArray());
}

void FakeCalDavServer::handleProppatch(QTcpSocket *socket, const QString &path)
{
    if (!isKnownCollection(path)) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }
    // Minimal RFC 4918 §9.2 success: 207 with a 200-status propstat. The
    // backend only checks the HTTP status (207/200/204), not the body props.
    const QByteArray body = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\">\n"
        "  <d:response>\n"
        "    <d:href>") + path.toUtf8() + QByteArrayLiteral("</d:href>\n"
        "    <d:propstat>\n"
        "      <d:prop/>\n"
        "      <d:status>HTTP/1.1 200 OK</d:status>\n"
        "    </d:propstat>\n"
        "  </d:response>\n"
        "</d:multistatus>\n");
    writeResponse(socket, 207, "Multi-Status", body);
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

void FakeCalDavServer::handleDelete(QTcpSocket *socket, const QString &path)
{
    if (!path.endsWith(QStringLiteral(".ics"))) {
        // Collection DELETE (RFC 4791 calendar removal).
        if (m_createdCollections.remove(path)) {
            m_store.remove(path);
            writeResponse(socket, 204, "No Content", QByteArray());
            return;
        }
        for (int i = 0; i < m_calendars.size(); ++i) {
            if (m_calendars.at(i).second == path) {
                m_store.remove(path);
                m_calendars.removeAt(i);
                writeResponse(socket, 204, "No Content", QByteArray());
                return;
            }
        }
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }

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

    auto colIt = m_store.find(collectionHref);
    if (colIt == m_store.end() || !colIt->contains(uid)) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }

    colIt->remove(uid);
    writeResponse(socket, 204, "No Content", QByteArray());
}

QString FakeCalDavServer::xmlForPrincipal() const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\">\n"
        "  <d:response>\n"
        "    <d:href>%1/</d:href>\n"
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

QString FakeCalDavServer::xmlForHome() const
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\" xmlns:cal=\"urn:ietf:params:xml:ns:caldav\">\n"
        "  <d:response>\n"
        "    <d:href>%1/principals/users/testuser/</d:href>\n"
        "    <d:propstat>\n"
        "      <d:prop>\n"
        "        <cal:calendar-home-set>\n"
        "          <d:href>%1/calendars/testuser/</d:href>\n"
        "        </cal:calendar-home-set>\n"
        "      </d:prop>\n"
        "      <d:status>HTTP/1.1 200 OK</d:status>\n"
        "    </d:propstat>\n"
        "  </d:response>\n"
        "</d:multistatus>\n").arg(m_contextPath);
}

QString FakeCalDavServer::xmlForCalendars() const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\" "
                          "xmlns:cal=\"urn:ietf:params:xml:ns:caldav\">\n");
    for (const auto &cal : m_calendars) {
        xml += QStringLiteral("  <d:response>\n");
        xml += QStringLiteral("    <d:href>%1%2</d:href>\n")
                   .arg(m_contextPath, cal.second);
        xml += QStringLiteral("    <d:propstat>\n");
        xml += QStringLiteral("      <d:prop>\n");
        xml += QStringLiteral(
            "        <d:resourcetype><d:collection/><cal:calendar/></d:resourcetype>\n");
        xml += QStringLiteral(
            "        <d:displayname>%1</d:displayname>\n").arg(cal.first);
        const QStringList comps = m_componentsByHref.value(
            cal.second, { QStringLiteral("VEVENT") });
        xml += QStringLiteral("        <cal:supported-calendar-component-set>");
        for (const QString &comp : comps)
            xml += QStringLiteral("<cal:comp name=\"%1\"/>").arg(comp);
        xml += QStringLiteral("</cal:supported-calendar-component-set>\n");
        if (m_readOnlyHrefs.contains(cal.second)) {
            // Advertise only the <read/> privilege so discovery reports
            // writable=false (no write/write-content/bind/unbind).
            xml += QStringLiteral(
                "        <d:current-user-privilege-set>"
                "<d:privilege><d:read/></d:privilege>"
                "</d:current-user-privilege-set>\n");
        }
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

        // Real CalDAV servers always use path-only (absolute path) hrefs in
        // multistatus responses, never full URLs with scheme+host. KDAV's
        // DavItemsFetchJob checks that the response href matches the requested
        // href by resolving both relative to the base URL — if we echo back a
        // full URL the comparison fails and the item is silently dropped.
        // Normalize to path-only here regardless of what the client sent.
        const QUrl hrefUrl(href);
        const QString responsePath = hrefUrl.isRelative() ? href : hrefUrl.path();

        xml += QStringLiteral("  <d:response>\n");
        xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(responsePath);
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
    // Use QDomDocument for robust namespace-aware parsing.
    // KDAV sends the multiget REPORT body with default-namespace declarations like
    //   <href xmlns="DAV:">...</href>
    // rather than prefixed declarations like <D:href>. A raw-text search for
    // ":href>" misses those. QDomDocument with UseNamespaceProcessing resolves
    // both forms correctly.
    QDomDocument doc;
    doc.setContent(body, QDomDocument::ParseOption::UseNamespaceProcessing);
    const QDomElement root = doc.documentElement();

    QList<QString> hrefs;
    QDomNodeList hrefElements = root.elementsByTagNameNS(
        QStringLiteral("DAV:"), QStringLiteral("href"));
    for (int i = 0; i < hrefElements.size(); ++i) {
        const QString href = hrefElements.at(i).toElement().text().trimmed();
        if (!href.isEmpty()) {
            hrefs.append(href);
        }
    }
    return hrefs;
}
