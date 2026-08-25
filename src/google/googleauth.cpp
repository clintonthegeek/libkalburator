#include "googleauth.h"
#include "blockinghttp.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace Kalburator::Google {

bool Tokens::hasLiveAccessToken() const
{
    return !accessToken.isEmpty()
        && expiresAtSecs > QDateTime::currentSecsSinceEpoch() + 300;
}

TokenStore::TokenStore(QString cachePath)
    : m_path(std::move(cachePath))
{
}

Tokens TokenStore::load() const
{
    Tokens tokens;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return tokens;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    tokens.accessToken = obj.value("access_token").toString();
    tokens.refreshToken = obj.value("refresh_token").toString();
    tokens.expiresAtSecs = static_cast<qint64>(obj.value("expires_at").toDouble());
    tokens.grantedScopes = obj.value("granted_scopes").toString();
    return tokens;
}

void TokenStore::save(const Tokens &tokens) const
{
    QJsonObject obj;
    obj.insert("access_token", tokens.accessToken);
    if (!tokens.refreshToken.isEmpty())
        obj.insert("refresh_token", tokens.refreshToken);
    obj.insert("expires_at", double(tokens.expiresAtSecs));
    if (!tokens.grantedScopes.isEmpty())
        obj.insert("granted_scopes", tokens.grantedScopes);
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream(stderr) << "Cannot write token cache " << m_path << ": "
                            << file.errorString() << '\n';
        return;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
}

void TokenStore::clear() const
{
    QFile::remove(m_path);
}

static QByteArray formBody(const QUrlQuery &query)
{
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

static Tokens tokenResponseToObject(const Net::HttpResponse &resp, bool &ok)
{
    Tokens out;
    const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();
    ok = false;
    if (!resp.ok() || obj.isEmpty()) {
        QTextStream(stderr) << "token endpoint failed: HTTP " << resp.status
                            << '\n' << QString::fromUtf8(resp.body) << '\n';
        return out;
    }
    if (obj.contains("error")) {
        QTextStream(stderr) << "token error: " << obj.value("error").toString()
                            << " — " << obj.value("error_description").toString() << '\n';
        return out;
    }
    out.accessToken = obj.value("access_token").toString();
    out.refreshToken = obj.value("refresh_token").toString();   // absent on refresh grant
    out.grantedScopes = obj.value("scope").toString();
    out.expiresAtSecs = QDateTime::currentSecsSinceEpoch()
        + obj.value("expires_in").toInt(3600);
    ok = !out.accessToken.isEmpty();
    return out;
}

LoopbackCodeFlow::LoopbackCodeFlow(ClientCredentials creds, QString scopes)
    : m_creds(std::move(creds))
    , m_scopes(std::move(scopes))
{
}

void LoopbackCodeFlow::setAuthEndpoint(const QString &url)
{
    m_authEndpoint = url;
}

void LoopbackCodeFlow::setTokenEndpoint(const QString &url)
{
    m_tokenEndpoint = url;
}

void LoopbackCodeFlow::setBrowserLauncher(
    std::function<void(const QUrl &)> launcher)
{
    m_launcher = std::move(launcher);
}

bool LoopbackCodeFlow::runInteractive(Tokens &out) const
{
    // 1. Ephemeral loopback listener.
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) {
        QTextStream(stderr) << "Cannot listen on 127.0.0.1: "
                            << server.errorString() << '\n';
        return false;
    }
    const quint16 port = server.serverPort();
    const QString redirectUri = QStringLiteral("http://127.0.0.1:%1/").arg(port);

    // 2. Authorization URL (access_type=offline + prompt=consent so a
    // refresh token is always minted).
    QUrlQuery authQuery;
    authQuery.addQueryItem("client_id", m_creds.clientId);
    authQuery.addQueryItem("redirect_uri", redirectUri);
    authQuery.addQueryItem("response_type", "code");
    authQuery.addQueryItem("scope", m_scopes);
    authQuery.addQueryItem("access_type", "offline");
    authQuery.addQueryItem("prompt", "consent");
    QUrl authUrl(m_authEndpoint);
    authUrl.setQuery(authQuery);

    QTextStream console(stdout);
    console << "\nOpening your browser for authorization.\n"
            << "If it does not open, visit this URL manually:\n\n  "
            << authUrl.toString(QUrl::FullyEncoded) << "\n\n"
            << "Waiting for the OAuth redirect on " << redirectUri << " ...\n"
            << Qt::flush;

    if (m_launcher)
        m_launcher(authUrl);

    // 3. Capture exactly one GET on the loopback socket.
    //
    // Concurrency notes (the original version crashed here): browsers open
    // several short-lived connections and half-deliver requests, so every
    // socket access is guarded through QPointer, request bytes are
    // accumulated across readyRead deliveries, and handling latches off
    // after the first callback request.
    QString authCode;
    QString denialError;
    bool handled = false;
    QHash<QTcpSocket *, QByteArray> buffers;

    QEventLoop loop;
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
        while (QTcpSocket *sock = server.nextPendingConnection()) {
            const QPointer<QTcpSocket> guard(sock);
            buffers.insert(sock, {});
            QObject::connect(sock, &QTcpSocket::readyRead, &loop,
                             [&, guard]() mutable {
                if (handled || guard.isNull())
                    return;
                QByteArray &buf = buffers[guard.data()];
                buf += guard->readAll();

                const int lineEnd = buf.indexOf("\r\n");
                if (lineEnd < 0)
                    return;   // request line incomplete
                const QString requestLine =
                    QString::fromUtf8(buf.left(lineEnd));
                if (!requestLine.startsWith(QLatin1String("GET ")))
                    return;   // e.g. favicon noise; ignore politely

                static const QRegularExpression codeRe("[?&]code=([^&\\s]+)");
                static const QRegularExpression errRe("[?&]error=([^&\\s]+)");
                const auto codeMatch = codeRe.match(requestLine);
                const auto errMatch = errRe.match(requestLine);
                if (!codeMatch.hasMatch() && !errMatch.hasMatch())
                    return;

                handled = true;
                if (codeMatch.hasMatch())
                    authCode = QUrl::fromPercentEncoding(
                        codeMatch.captured(1).toUtf8());
                else
                    denialError = QUrl::fromPercentEncoding(
                        errMatch.captured(1).toUtf8());

                const QByteArray body =
                    "<html><body><h2>kalburator</h2><p>Authorization received. "
                    "You may close this tab.</p></body></html>";
                guard->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                             "Connection: close\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n"
                             + body);
                guard->flush();
                guard->disconnectFromHost();
                loop.quit();
            });
            // No per-socket cleanup needed: entries are tiny and short-lived,
            // and stale-buffer lookups are guarded by the QPointer above.
        }
    });
    QTimer::singleShot(5 * 60 * 1000, &loop, [&] {
        QTextStream(stderr) << "\nTimed out waiting for the OAuth redirect.\n";
        loop.quit();
    });
    loop.exec();
    server.close();

    if (!handled) {
        if (!denialError.isEmpty())
            QTextStream(stderr) << "Authorization denied: " << denialError << '\n';
        else if (authCode.isEmpty())
            QTextStream(stderr) << "No authorization code captured.\n";
        return false;
    }
    if (!denialError.isEmpty()) {
        QTextStream(stderr) << "Authorization denied: " << denialError << '\n';
        return false;
    }
    if (authCode.isEmpty()) {
        QTextStream(stderr) << "No authorization code captured.\n";
        return false;
    }

    // 4. Code → tokens.
    QUrlQuery exchange;
    exchange.addQueryItem("code", authCode);
    exchange.addQueryItem("client_id", m_creds.clientId);
    exchange.addQueryItem("client_secret", m_creds.clientSecret);
    exchange.addQueryItem("redirect_uri", redirectUri);
    exchange.addQueryItem("grant_type", "authorization_code");
    bool ok = false;
    out = tokenResponseToObject(Net::httpRequest(QUrl(m_tokenEndpoint),
                                                 "POST",
                                                 {{"Content-Type", "application/x-www-form-urlencoded"}},
                                                 formBody(exchange)), ok);
    return ok;
}

Tokens refreshTokens(const ClientCredentials &creds, const Tokens &old)
{
    QUrlQuery query;
    query.addQueryItem("refresh_token", old.refreshToken);
    query.addQueryItem("client_id", creds.clientId);
    query.addQueryItem("client_secret", creds.clientSecret);
    query.addQueryItem("grant_type", "refresh_token");
    bool ok = false;
    Tokens fresh = tokenResponseToObject(Net::httpRequest(
                                             QUrl(QStringLiteral("https://oauth2.googleapis.com/token")),
                                             "POST",
                                             {{"Content-Type", "application/x-www-form-urlencoded"}},
                                             formBody(query)), ok);
    // The refresh grant does not rotate the refresh token.
    if (fresh.refreshToken.isEmpty())
        fresh.refreshToken = old.refreshToken;
    return fresh;
}

} // namespace Kalburator::Google
