#include "fakecaldavserver.h"

#include <QByteArray>
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
        // Hand off only the first request — discovery uses one PROPFIND
        // per TCP connection (Connection: close in our response).
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
                                     const QByteArray &body)
{
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + reasonPhrase + "\r\n";
    if (statusCode == 401) {
        // Discovery reads via QNetworkAccessManager, which only fires
        // authenticationRequired if a WWW-Authenticate header is
        // present. Discovery does NOT connect that signal, so 401 just
        // propagates as AuthenticationRequiredError — which is what we
        // want for the negative test.
        resp += "WWW-Authenticate: Basic realm=\"fake\"\r\n";
    }
    resp += "Content-Type: application/xml; charset=utf-8\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
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

    const QByteArray path = parts.at(1);

    QString xml;
    if (path == "/" || path.isEmpty()) {
        xml = xmlForPrincipal();
    } else if (path == "/principals/users/testuser/") {
        xml = xmlForHome();
    } else if (path == "/calendars/testuser/") {
        xml = xmlForCalendars();
    } else {
        writeResponse(socket, 404, "Not Found", QByteArray());
        return;
    }

    writeResponse(socket, 207, "Multi-Status", xml.toUtf8());
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
