#include "googleauth.h"
#include "googleclient.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

static const char *kAuthEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
static const char *kTokenEndpoint = "https://oauth2.googleapis.com/token";

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
    return tokens;
}

void TokenStore::save(const Tokens &tokens) const
{
    QJsonObject obj;
    obj.insert("access_token", tokens.accessToken);
    if (!tokens.refreshToken.isEmpty())
        obj.insert("refresh_token", tokens.refreshToken);
    obj.insert("expires_at", double(tokens.expiresAtSecs));
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

QString googledir()
{
    const QString envDir = qEnvironmentVariable("KALBURATOR_GOOGLE_DIR");
    if (!envDir.isEmpty())
        return envDir;

    QDir dir = QDir::current();
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath("google");
        if (QFileInfo::exists(candidate + "/GoogleAuthinfo.md"))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return QDir::current().filePath("google");
}

ClientCredentials readClientCredentials(const QString &dir)
{
    ClientCredentials creds;

    creds.clientId = qEnvironmentVariable("KALBURATOR_GOOGLE_CLIENT_ID");
    creds.clientSecret = qEnvironmentVariable("KALBURATOR_GOOGLE_CLIENT_SECRET");
    if (creds.valid())
        return creds;

    QFile file(dir + "/GoogleAuthinfo.md");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "Cannot read " << dir
                            << "/GoogleAuthinfo.md (set KALBURATOR_GOOGLE_CLIENT_ID"
                            << " and KALBURATOR_GOOGLE_CLIENT_SECRET, or KALBURATOR_GOOGLE_DIR)\n";
        return {};
    }
    static const QRegularExpression idRe("Client ID:\\s*([^\\s]+)");
    static const QRegularExpression secretRe("Client Secret:\\s*([^\\s]+)");
    const QString text = QString::fromUtf8(file.readAll());
    const auto idMatch = idRe.match(text);
    const auto secretMatch = secretRe.match(text);
    if (idMatch.hasMatch())
        creds.clientId = idMatch.captured(1);
    if (secretMatch.hasMatch())
        creds.clientSecret = secretMatch.captured(1);
    if (!creds.valid())
        QTextStream(stderr) << "GoogleAuthinfo.md lacks 'Client ID:' / 'Client Secret:' lines\n";
    return creds;
}

void printGoogleError(const QString &label, int status, const QByteArray &body)
{
    QTextStream(stderr) << label << " failed: HTTP " << status << "\n"
                        << QString::fromUtf8(body) << '\n';
}

static QByteArray formBody(const QUrlQuery &query)
{
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

static Tokens tokenResponseToObject(const HttpResponse &resp, bool &ok)
{
    Tokens out;
    const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();
    ok = false;
    if (!resp.ok() || obj.isEmpty()) {
        printGoogleError("token endpoint", resp.status, resp.body);
        return out;
    }
    if (obj.contains("error")) {
        QTextStream(stderr) << "token error: " << obj.value("error").toString()
                            << " — " << obj.value("error_description").toString() << '\n';
        return out;
    }
    out.accessToken = obj.value("access_token").toString();
    out.refreshToken = obj.value("refresh_token").toString();   // absent on refresh grant
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
    QUrl authUrl(QString::fromUtf8(kAuthEndpoint));
    authUrl.setQuery(authQuery);

    QTextStream console(stdout);
    console << "\nOpening your browser for authorization.\n"
            << "If it does not open, visit this URL manually:\n\n  "
            << authUrl.toString(QUrl::FullyEncoded) << "\n\n"
            << "Waiting for the OAuth redirect on " << redirectUri << " ...\n"
            << Qt::flush;
    // Best-effort browser launch without a Qt6::Gui dependency; the URL is
    // printed either way.
    QProcess::startDetached(QStringLiteral("xdg-open"),
                            {authUrl.toString(QUrl::FullyEncoded)});

    // 3. Capture exactly one GET on the loopback socket.
    QString authCode;
    QString denialError;
    QEventLoop loop;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        QTcpSocket *sock = server.nextPendingConnection();
        if (!sock)
            return;
        QObject::connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
        QObject::connect(sock, &QTcpSocket::readyRead, [&] {
            if (authCode.isEmpty() && denialError.isEmpty())
                ;   // keep parsing below
            const QByteArray request = sock->readAll();
            const QString line = QString::fromUtf8(
                request.left(request.indexOf("\r\n")));
            static const QRegularExpression codeRe("[?&]code=([^&\\s]+)");
            static const QRegularExpression errRe("[?&]error=([^&\\s]+)");
            const auto codeMatch = codeRe.match(line);
            const auto errMatch = errRe.match(line);
            if (codeMatch.hasMatch()) {
                authCode = QUrl::fromPercentEncoding(codeMatch.captured(1).toUtf8());
            } else if (errMatch.hasMatch()) {
                denialError = QUrl::fromPercentEncoding(errMatch.captured(1).toUtf8());
            } else {
                return;   // not the callback request; wait for more/further reads
            }
            const QByteArray body =
                "<html><body><h2>googlecli</h2><p>Authorization received. "
                "You may close this tab.</p></body></html>";
            sock->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                        "Connection: close\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            sock->disconnectFromHost();
            loop.quit();
        });
    });
    QTimer::singleShot(5 * 60 * 1000, &loop, [&] {
        QTextStream(stderr) << "\nTimed out waiting for the OAuth redirect.\n";
        loop.quit();
    });
    loop.exec();
    server.close();

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
    out = tokenResponseToObject(httpRequest(QUrl(QString::fromUtf8(kTokenEndpoint)),
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
    Tokens fresh = tokenResponseToObject(httpRequest(QUrl(QString::fromUtf8(kTokenEndpoint)),
                                                     "POST",
                                                     {{"Content-Type", "application/x-www-form-urlencoded"}},
                                                     formBody(query)), ok);
    // The refresh grant does not rotate the refresh token.
    if (fresh.refreshToken.isEmpty())
        fresh.refreshToken = old.refreshToken;
    return fresh;
}
