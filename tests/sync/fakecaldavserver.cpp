#include "fakecaldavserver.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDomDocument>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
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
    m_requestPaths.clear();
    m_multigetReportCount = 0;
    m_syncCollectionReportCount = 0;
    m_serialQueue.clear();   // O45
    m_serialBusy = false;    // O45
    if (!listen(QHostAddress::LocalHost, 0))
        return false;
    m_lastBoundPort = serverPort();
    return true;
}

bool FakeCalDavServer::reviveOnSamePort()
{
    if (isListening())
        return true; // never died — nothing to revive
    m_writesSinceRevive = 0;
    if (!listen(QHostAddress::LocalHost, m_lastBoundPort))
        return false;
    m_lastBoundPort = serverPort();
    return true;
}

void FakeCalDavServer::maybeDieAfterWrite()
{
    if (m_dieAfterNWrites <= 0)
        return;
    ++m_writesSinceRevive;
    if (m_writesSinceRevive >= m_dieAfterNWrites) {
        // Stop accepting new connections — subsequent connect() attempts
        // fail with ECONNREFUSED, exactly like a client reaching for a
        // process that has been SIGKILLed. Does not affect the response
        // already in flight on the current (already-accepted) socket.
        close();
    }
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
        // VP.c-step-1b: the store is keyed by RESOURCE FILE NAME. The default
        // resource for a seeded event lives at "<uid>.ics" (fileName == uid).
        const QString fileName = uid;
        const bool isNew = !col.contains(fileName);
        col.insert(fileName, rec);
        if (isNew) {
            m_uidToFileNames[collectionHref][uid].append(fileName);
        }
        // E7/O36: every seed (initial population AND a later re-seed used to
        // simulate "another client edited this item") is a real mutation
        // from REPORT sync-collection's point of view.
        logChange(collectionHref, uid, /*deleted=*/false);
    }
}

void FakeCalDavServer::logChange(const QString &collectionHref, const QString &uid, bool deleted)
{
    m_changeLog[collectionHref].append({uid, deleted});
}

bool FakeCalDavServer::hasEvent(const QString &collectionHref,
                                const QString &uid) const
{
    // A UID exists if ANY resource in the collection carries it (a master or
    // one of its detached exceptions).
    return m_uidToFileNames.value(collectionHref).contains(uid);
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

void FakeCalDavServer::removeEvent(const QString &collectionHref, const QString &uid)
{
    // "Another client deleted the event" — removes EVERY resource carrying
    // the uid (a master and any detached exceptions share it).
    auto it = m_store.find(collectionHref);
    if (it == m_store.end()) return;
    auto uidFilesIt = m_uidToFileNames.find(collectionHref);
    if (uidFilesIt == m_uidToFileNames.end()) return;
    const QList<QString> files = uidFilesIt->value(uid);
    if (files.isEmpty()) return;
    for (const QString &fileName : files) {
        it->remove(fileName);
    }
    uidFilesIt->remove(uid);
    logChange(collectionHref, uid, /*deleted=*/true);
}

void FakeCalDavServer::removeEventAt(const QString &collectionHref,
                                     const QString &fileName)
{
    // VP.c (W1 matrix): "reabsorb" — a server-side change that drops ONE
    // resource (e.g. a detached exception) while the UID's other resources
    // (the master) stay. Mirrors removeEvent() but keyed by resource file
    // name, not by UID, so master + exception survive independently.
    auto it = m_store.find(collectionHref);
    if (it == m_store.end()) return;
    if (!it->contains(fileName)) return;
    const QString uid = uidForFileName(collectionHref, fileName);
    it->remove(fileName);
    if (!uid.isEmpty()) {
        auto &files = m_uidToFileNames[collectionHref][uid];
        files.removeAll(fileName);
        if (files.isEmpty()) m_uidToFileNames[collectionHref].remove(uid);
        logChange(collectionHref, uid, /*deleted=*/true);
    }
}

void FakeCalDavServer::setSeedEventAt(const QString &collectionHref,
                                      const QString &fileName,
                                      const QByteArray &ics)
{
    const QString uid = uidFromIcs(ics);
    if (uid.isEmpty()) return;
    IcsRecord rec;
    rec.data = ics;
    rec.etag = makeEtag(ics);
    // VP.c-step-1b: keyed by the resource's file name; a UID may have
    // several resources (a master plus its detached exceptions).
    const bool isNew = !m_store[collectionHref].contains(fileName);
    m_store[collectionHref].insert(fileName, rec);
    if (isNew) {
        m_uidToFileNames[collectionHref][uid].append(fileName);
    }
    // E7/O36: every seed (initial population AND a later re-seed used to
    // simulate "another client edited this item") is a real mutation
    // from REPORT sync-collection's point of view.
    logChange(collectionHref, uid, /*deleted=*/false);
}

QString FakeCalDavServer::hrefForUid(const QString &collectionHref,
                                     const QString &uid) const
{
    const QList<QString> files =
        m_uidToFileNames.value(collectionHref).value(uid);
    const QString fileName = files.isEmpty() ? uid : files.first();
    return collectionHref + fileName + QStringLiteral(".ics");
}

QString FakeCalDavServer::uidForFileName(const QString &collectionHref,
                                         const QString &fileName) const
{
    const auto colIt = m_uidToFileNames.constFind(collectionHref);
    if (colIt == m_uidToFileNames.constEnd()) return QString();
    for (auto it = colIt->constBegin(); it != colIt->constEnd(); ++it) {
        if (it.value().contains(fileName)) return it.key();
    }
    return QString();
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
        if (m_dropRequests) {
            // Read the request and go silent — no response, socket stays
            // open. Deliberately not handleRequest() nor a disconnect.
            return;
        }

        // O45: serialized mode — FIFO across all connections, one request
        // (including its delay) served at a time; models a single-threaded
        // server whose burst DRAIN RATE, not per-request latency, is the
        // bottleneck.
        if (m_serializeResponses) {
            m_serialQueue.append({QPointer<QTcpSocket>(socket), request});
            processSerialQueue();
            return;
        }

        const int delayMs = delayForRequest(request);
        if (delayMs > 0) {
            QTimer::singleShot(delayMs, this, [this, socket, request]() {
                handleRequest(socket, request);
            });
        } else {
            handleRequest(socket, request);
        }
    });

    QObject::connect(socket, &QTcpSocket::disconnected,
                     socket, &QObject::deleteLater);
}

// E5.3: per-method delay override, checked before the uniform
// m_responseDelayMs — lets a test isolate a slow write from a fast
// read/classify phase (setResponseDelayMs() alone delays every method
// identically). Method is the first whitespace-separated token of the
// request line; cheap to peek here without disturbing handleRequest()'s
// own (identical) parse. (Extracted into a helper at O45 so the serialized
// queue path computes the same delay.)
int FakeCalDavServer::delayForRequest(const QByteArray &request) const
{
    int delayMs = m_responseDelayMs;
    if (!m_perMethodDelayMs.isEmpty()) {
        const int firstNewline = request.indexOf("\r\n");
        if (firstNewline > 0) {
            const QByteArray requestLine = request.left(firstNewline);
            const int firstSpace = requestLine.indexOf(' ');
            if (firstSpace > 0) {
                const QByteArray method = requestLine.left(firstSpace);
                const auto it = m_perMethodDelayMs.constFind(method);
                if (it != m_perMethodDelayMs.constEnd()) {
                    delayMs = it.value();
                }
            }
        }
    }
    return delayMs;
}

// O45: serve the serialized queue one request at a time. The delay elapses
// BEFORE each response is written and the next request is only dequeued
// after that, so the queue drains at one request per delay — the Nth queued
// request completes N x delay after it arrived, regardless of how many
// connections the client opened.
void FakeCalDavServer::processSerialQueue()
{
    if (m_serialBusy || m_serialQueue.isEmpty()) {
        return;
    }
    m_serialBusy = true;
    const auto entry = m_serialQueue.takeFirst();
    const int delayMs = qMax(0, delayForRequest(entry.second));
    QTimer::singleShot(delayMs, this, [this, entry]() {
        if (entry.first) {
            handleRequest(entry.first, entry.second);
        }
        m_serialBusy = false;
        processSerialQueue();
    });
}

void FakeCalDavServer::writeResponse(QTcpSocket *socket,
                                     int statusCode,
                                     const QByteArray &reasonPhrase,
                                     const QByteArray &body,
                                     const QByteArray &extraHeaders)
{
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + reasonPhrase + "\r\n";
    if (!m_serverHeader.isEmpty()) {
        resp += "Server: " + m_serverHeader + "\r\n";
    }
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
    m_requestPaths[method].append(path);  // O54: assert WHERE a write landed

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
        } else if (isKnownCollection(path) && body.contains("supported-report-set")) {
            // E7/O36: capability-detection PROPFIND. Checked before the
            // getctag branch below since both are Depth:0 PROPFINDs on the
            // same collection href, distinguished only by requested prop.
            writeResponse(socket, 207, "Multi-Status",
                          xmlForSupportedReportSet(path));
            return;
        } else if (isKnownCollection(path) && m_ctagByHref.contains(path)) {
            // Depth:0 CS:getctag PROPFIND on a known calendar collection —
            // supports N5's CTag-match/serve-path tests. A collection with
            // no ctag set via setCollectionCtag() still 404s (matches the
            // prior behavior tests not needing this may rely on).
            xml = xmlForCtag(path);
        } else {
            // Depth:0 PROPFIND on a collection this fake doesn't have a
            // CTag configured for — return 404. The backend skips the CTag
            // optimisation when the PROPFIND fails, which is fine for tests
            // that only need the first sync.
            writeResponse(socket, 404, "Not Found", QByteArray());
            return;
        }
        writeResponse(socket, 207, "Multi-Status", xml.toUtf8());

    } else if (method == "REPORT") {
        handleReport(socket, path, body);

    } else if (method == "GET") {
        // Item-level fetch (RemoteCalendarBackend::getRawIcs / loadRecord):
        // serve the stored iCal body for "/<collection>/<fileName>".
        const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
        const QString fileName = uidFromPath(path);
        if (lastSlash > 0 && !fileName.isEmpty()) {
            const QString colHref = path.left(lastSlash + 1);
            auto colIt = m_store.constFind(colHref);
            if (colIt != m_store.constEnd()) {
                auto recIt = colIt->constFind(fileName);
                if (recIt != colIt->constEnd()) {
                    writeResponse(socket, 200, "OK", recIt.value().data,
                                  "ETag: " + makeEtag(recIt.value().data).toUtf8() + "\r\n");
                    return;
                }
            }
        }
        writeResponse(socket, 404, "Not Found", QByteArray());

    } else if (method == "PUT") {
        const QByteArray headers = (headerEnd > 0) ? fullRequest.left(headerEnd) : QByteArray();
        handlePut(socket, path, body, headers);

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

    // E7/O36: RFC 6578 sync-collection, checked first — its body never
    // contains "calendar-multiget".
    if (body.contains("sync-collection")) {
        handleSyncCollectionReport(socket, collectionHref, body);
        return;
    }

    // Distinguish calendar-query (ETag list) from calendar-multiget (full data)
    // by looking for the report type string in the request body.
    if (body.contains("calendar-multiget")) {
        ++m_multigetReportCount;
        if (m_failNthMultigetReport > 0 && m_multigetReportCount == m_failNthMultigetReport) {
            writeResponse(socket, 500, "Internal Server Error", QByteArray());
            return;
        }
        const QList<QString> hrefs = parseHrefsFromBody(body);
        writeResponse(socket, 207, "Multi-Status",
                      xmlForCalendarMultiget(collectionHref, hrefs));
    } else {
        // calendar-query or anything else — return ETag list for all events.
        writeResponse(socket, 207, "Multi-Status",
                      xmlForCalendarQuery(collectionHref));
    }
}

void FakeCalDavServer::handleSyncCollectionReport(QTcpSocket *socket,
                                                  const QString &collectionHref,
                                                  const QByteArray &body)
{
    ++m_syncCollectionReportCount;

    if (!m_supportsSyncCollection) {
        // RFC 6578 §3.1: a REPORT the collection doesn't support is a 403
        // Forbidden with a DAV:supported-report precondition — the backend
        // should never send this (capability was detected false), but stay
        // correct if it somehow does.
        writeResponse(socket, 403, "Forbidden", QByteArray());
        return;
    }

    const QString tokenStr = parseSyncTokenFromBody(body);
    const int currentSize = m_changeLog.value(collectionHref).size();

    if (!tokenStr.isEmpty() && m_invalidateSyncTokens) {
        writeResponse(socket, 410, "Gone", QByteArray());
        return;
    }

    bool ok = true;
    const int clientToken = tokenStr.isEmpty() ? 0 : tokenStr.toInt(&ok);
    if (!ok || clientToken < 0 || clientToken > currentSize) {
        // Unparseable or out-of-range (stale beyond what our journal can
        // still answer) — RFC 6578 §3.3 token invalidation.
        writeResponse(socket, 410, "Gone", QByteArray());
        return;
    }

    writeResponse(socket, 207, "Multi-Status",
                  xmlForSyncCollection(collectionHref, clientToken));
}

void FakeCalDavServer::handlePut(QTcpSocket *socket,
                                 const QString &path,
                                 const QByteArray &body,
                                 const QByteArray &headers)
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
    const QString fileName = uidFromPath(path);
    if (fileName.isEmpty()) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    // O54/VP.c-step-1b: resolve which UID THIS RESOURCE carries. The store is
    // keyed by file name, and several resources may share one UID (a master
    // plus its detached exceptions) — the resource's own href is what
    // distinguishes them. A PUT to an item whose server-assigned filename
    // differs from its UID targets that aliased href; a PUT creating a NEW
    // resource whose UID already lives at a DIFFERENT filename is the exact
    // mistake O54 found — SabreDAV answers it with 400 ("Calendar object
    // with uid already exists in this calendar collection"), not 412.
    QString uid = uidForFileName(collectionHref, fileName);

    QHash<QString, IcsRecord> &col = m_store[collectionHref];
    const bool isNew = !col.contains(fileName);

    if (!isNew) {
        // Updating an existing resource at its own href — always legal.
        // (The wrong-URL duplicate-uid mistake cannot reach here: the target
        // filename already exists, so it is this resource's own href.)
        if (uid.isEmpty()) uid = uidFromIcs(body);
    } else {
        // Creating a NEW resource: the body's UID is authoritative. O54:
        // creating a resource whose UID already lives under a DIFFERENT
        // filename is the SabreDAV uniqueness violation — UNLESS the payload
        // is a detached exception (RECURRENCE-ID present) sharing the
        // master's UID, which is legal CalDAV.
        const QString bodyUid = uidFromIcs(body);
        if (!bodyUid.isEmpty()) uid = bodyUid;
        const QList<QString> existing =
            m_uidToFileNames.value(collectionHref).value(bodyUid);
        if (!bodyUid.isEmpty() && !existing.isEmpty()
            && !body.contains("RECURRENCE-ID")) {
            writeResponse(socket, 400, "Bad Request", QByteArrayLiteral(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<d:error xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\">\n"
                "  <s:exception>Sabre\\DAV\\Exception\\BadRequest</s:exception>\n"
                "  <s:message>Calendar object with uid already exists in this "
                "calendar collection.</s:message>\n"
                "</d:error>\n"));
            return;
        }
    }

    // RFC 7232 preconditions (E4/O32 — real servers enforce these; a fake
    // that always succeeds hides the difference between "wrote" and
    // "silently clobbered a concurrent edit").
    const QByteArray ifNoneMatch = headerValue(headers, "If-None-Match");
    if (!ifNoneMatch.isEmpty() && ifNoneMatch.trimmed() == "*" && !isNew) {
        writeResponse(socket, 412, "Precondition Failed", QByteArray());
        return;
    }
    const QByteArray ifMatch = headerValue(headers, "If-Match");
    if (!ifMatch.isEmpty()) {
        if (isNew) {
            // If-Match presupposes an existing resource.
            writeResponse(socket, 412, "Precondition Failed", QByteArray());
            return;
        }
        // Compare quote-insensitively: real servers hand out quoted ETags,
        // but some client helpers strip the quotes before echoing one back
        // in If-Match (davSyncRequest does exactly that).
        const QString given = QString::fromUtf8(ifMatch.trimmed());
        const QString want = col.value(fileName).etag;
        if (given != want
            && given != want.mid(1, want.length() - 2)) {
            writeResponse(socket, 412, "Precondition Failed", QByteArray());
            return;
        }
    }

    IcsRecord rec;
    rec.data = body;
    // Salt with a counter so repeated PUTs produce distinct ETags.
    static int s_counter = 0;
    rec.etag = makeEtag(body + QByteArray::number(++s_counter));
    col.insert(fileName, rec);
    // O54/VP.c-step-1b: CalDAV keeps whatever filename the
    // creating/updating client addressed — register it for this UID (once,
    // even across re-PUTs of the same resource).
    if (!uid.isEmpty()) {
        QList<QString> &files = m_uidToFileNames[collectionHref][uid];
        if (!files.contains(fileName)) {
            files.append(fileName);
        }
    }
    logChange(collectionHref, uid, /*deleted=*/false);

    const QByteArray etagHeader =
        ("ETag: " + rec.etag.toUtf8() + "\r\n");
    if (isNew) {
        writeResponse(socket, 201, "Created", QByteArray(), etagHeader);
    } else {
        writeResponse(socket, 204, "No Content", QByteArray(), etagHeader);
    }
    maybeDieAfterWrite();
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
    // VP.c-step-1b: delete the RESOURCE at this href (file-name keyed). A
    // master and its detached exceptions are separate resources and are
    // deleted independently. UID resolution is only needed for the change
    // journal.
    const QString fileName = uidFromPath(path);
    if (fileName.isEmpty()) {
        writeResponse(socket, 400, "Bad Request", QByteArray());
        return;
    }

    auto colIt = m_store.find(collectionHref);
    if (colIt == m_store.end() || !colIt->contains(fileName)) {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }

    colIt->remove(fileName);
    const QString uid = uidForFileName(collectionHref, fileName);
    if (!uid.isEmpty()) {
        auto uidFilesIt = m_uidToFileNames.find(collectionHref);
        if (uidFilesIt != m_uidToFileNames.end()) {
            QList<QString> &files = uidFilesIt.value()[uid];
            files.removeAll(fileName);
            if (files.isEmpty()) {
                uidFilesIt.value().remove(uid);
            }
        }
        logChange(collectionHref, uid, /*deleted=*/true);
    }
    writeResponse(socket, 204, "No Content", QByteArray());
    maybeDieAfterWrite();
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
                          "xmlns:cal=\"urn:ietf:params:xml:ns:caldav\" "
                          "xmlns:cs=\"http://calendarserver.org/ns/\">\n");
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
        // VP.a (W8): explicit producer id, when configured.
        if (m_prodidByHref.contains(cal.second)) {
            xml += QStringLiteral("        <prodid>%1</prodid>\n")
                       .arg(m_prodidByHref.value(cal.second));
        }
        // VP.a (W8): advertise RFC 6578 sync-collection in the depth-1
        // calendar-list multistat when enabled, so discovery's per-calendar
        // supported-report-set parse sees it (mirrors real servers like
        // Radicale >=3 / Nextcloud which include it here).
        if (m_supportsSyncCollection) {
            xml += QStringLiteral(
                "        <d:supported-report-set>"
                "<d:supported-report><d:report><d:sync-collection/>"
                "</d:report></d:supported-report>"
                "</d:supported-report-set>\n");
        }
        // getctag, when configured via setCollectionCtag() — real servers
        // commonly include it in the depth-1 calendar-list response too, so
        // discovery (not just the later Depth:0 optimisation PROPFIND) can
        // stage a pendingCtag (N5 tests).
        if (m_ctagByHref.contains(cal.second)) {
            xml += QStringLiteral(
                "        <cs:getctag>%1</cs:getctag>\n").arg(m_ctagByHref.value(cal.second));
        }
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
            // VP.c-step-1b: one href per RESOURCE — two resources sharing a
            // UID (master + detached exception) both appear.
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

QByteArray FakeCalDavServer::xmlForCtag(const QString &collectionHref) const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\""
                          " xmlns:cs=\"http://calendarserver.org/ns/\">\n");
    xml += QStringLiteral("  <d:response>\n");
    xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(collectionHref);
    xml += QStringLiteral("    <d:propstat>\n");
    xml += QStringLiteral("      <d:prop>\n");
    xml += QStringLiteral("        <cs:getctag>%1</cs:getctag>\n")
               .arg(m_ctagByHref.value(collectionHref));
    xml += QStringLiteral("      </d:prop>\n");
    xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
    xml += QStringLiteral("    </d:propstat>\n");
    xml += QStringLiteral("  </d:response>\n");
    xml += QStringLiteral("</d:multistatus>\n");
    return xml.toUtf8();
}

QByteArray FakeCalDavServer::xmlForSupportedReportSet(const QString &collectionHref) const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\">\n");
    xml += QStringLiteral("  <d:response>\n");
    xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(collectionHref);
    xml += QStringLiteral("    <d:propstat>\n");
    xml += QStringLiteral("      <d:prop>\n");
    xml += QStringLiteral("        <d:supported-report-set>\n");
    if (m_supportsSyncCollection) {
        xml += QStringLiteral(
            "          <d:supported-report><d:report><d:sync-collection/>"
            "</d:report></d:supported-report>\n");
    }
    xml += QStringLiteral("        </d:supported-report-set>\n");
    xml += QStringLiteral("      </d:prop>\n");
    xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
    xml += QStringLiteral("    </d:propstat>\n");
    xml += QStringLiteral("  </d:response>\n");
    xml += QStringLiteral("</d:multistatus>\n");
    return xml.toUtf8();
}

QByteArray FakeCalDavServer::xmlForSyncCollection(const QString &collectionHref,
                                                  int clientToken) const
{
    const QList<ChangeEntry> &log = m_changeLog.value(collectionHref);

    // Dedup to the last state per uid within [clientToken, end) — a uid
    // touched more than once since the client's token reports only its
    // final state (e.g. changed-then-deleted reports as deleted only).
    QMap<QString, bool> lastState; // uid -> deleted
    for (int i = clientToken; i < log.size(); ++i) {
        lastState[log.at(i).uid] = log.at(i).deleted;
    }

    const auto storeIt = m_store.constFind(collectionHref);

    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    xml += QStringLiteral("<d:multistatus xmlns:d=\"DAV:\""
                          " xmlns:c=\"urn:ietf:params:xml:ns:caldav\">\n");

    for (auto it = lastState.constBegin(); it != lastState.constEnd(); ++it) {
        const QString href = hrefForUid(collectionHref, it.key());
        xml += QStringLiteral("  <d:response>\n");
        xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(href);
        if (it.value()) {
            // RFC 6578 §3.6: a deletion tombstone is a bare 404 response,
            // no propstat/prop.
            xml += QStringLiteral("    <d:status>HTTP/1.1 404 Not Found</d:status>\n");
        } else {
            QString etag;
            if (storeIt != m_store.constEnd()) {
                const auto recIt = storeIt->constFind(it.key());
                if (recIt != storeIt->constEnd()) etag = recIt->etag;
            }
            xml += QStringLiteral("    <d:propstat>\n");
            xml += QStringLiteral("      <d:prop><d:getetag>%1</d:getetag></d:prop>\n").arg(etag);
            xml += QStringLiteral("      <d:status>HTTP/1.1 200 OK</d:status>\n");
            xml += QStringLiteral("    </d:propstat>\n");
        }
        xml += QStringLiteral("  </d:response>\n");
    }

    // Sibling of the <d:response> elements, not nested inside one.
    xml += QStringLiteral("  <d:sync-token>%1</d:sync-token>\n").arg(log.size());
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
        const QUrl hrefUrl(href);
        const QString hrefPath = hrefUrl.isRelative() ? href : hrefUrl.path();
        // VP.c-step-1b: resolve the resource by its FILE NAME — a multiget
        // href's filename need not equal the UID, and two resources may
        // share one UID (master + detached exception), so per-UID lookup
        // would serve the wrong bytes for one of them.
        const QString fileName = uidFromPath(hrefPath);
        if (fileName.isEmpty()) continue;

        // Real CalDAV servers always use path-only (absolute path) hrefs in
        // multistatus responses, never full URLs with scheme+host. KDAV's
        // DavItemsFetchJob checks that the response href matches the requested
        // href by resolving both relative to the base URL — if we echo back a
        // full URL the comparison fails and the item is silently dropped.
        // Normalize to path-only here regardless of what the client sent.
        const QString responsePath = hrefPath;

        xml += QStringLiteral("  <d:response>\n");
        xml += QStringLiteral("    <d:href>%1</d:href>\n").arg(responsePath);
        xml += QStringLiteral("    <d:propstat>\n");
        xml += QStringLiteral("      <d:prop>\n");

        if (storeIt != m_store.constEnd()) {
            auto recIt = storeIt->constFind(fileName);
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
QByteArray FakeCalDavServer::headerValue(const QByteArray &headers, const QByteArray &name)
{
    const QList<QByteArray> lines = headers.split('\n');
    for (const QByteArray &raw : lines) {
        QByteArray line = raw;
        if (line.endsWith('\r')) line.chop(1);
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QByteArray lineName = line.left(colon).trimmed();
        if (qstricmp(lineName.constData(), name.constData()) == 0) {
            return line.mid(colon + 1).trimmed();
        }
    }
    return QByteArray();
}

// static
QString FakeCalDavServer::parseSyncTokenFromBody(const QByteArray &body)
{
    QDomDocument doc;
    doc.setContent(body, QDomDocument::ParseOption::UseNamespaceProcessing);
    const QDomElement root = doc.documentElement();
    const QDomNodeList tokenElements = root.elementsByTagNameNS(
        QStringLiteral("DAV:"), QStringLiteral("sync-token"));
    if (tokenElements.isEmpty()) return QString();
    return tokenElements.at(0).toElement().text().trimmed();
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
