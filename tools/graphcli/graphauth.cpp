#include "graphauth.h"
#include "graphclient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

static const char *kAuthorityBase = "https://login.microsoftonline.com/common/oauth2/v2.0";

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

static QByteArray formBody(const QUrlQuery &query)
{
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

DeviceCodeFlow::DeviceCodeFlow(QString clientId, QString scopes, bool promptConsent)
    : m_clientId(std::move(clientId))
    , m_scopes(std::move(scopes))
    , m_promptConsent(promptConsent)
{
}

bool DeviceCodeFlow::runInteractive(Tokens &out) const
{
    QUrlQuery start;
    start.addQueryItem("client_id", m_clientId);
    start.addQueryItem("scope", m_scopes);
    if (m_promptConsent)
        start.addQueryItem("prompt", "consent");

    const HttpResponse startResp = httpRequest(
        QUrl(QString::fromUtf8(kAuthorityBase) + "/devicecode"),
        "POST",
        {{"Content-Type", "application/x-www-form-urlencoded"}},
        formBody(start));

    const QJsonObject startObj = QJsonDocument::fromJson(startResp.body).object();
    if (!startResp.ok() || startObj.isEmpty()) {
        printGraphError("device code request", startResp.status, startResp.body);
        return false;
    }

    QTextStream console(stdout);
    console << "\nTo sign in, open this URL in a browser:\n\n  "
        << startObj.value("verification_uri").toString() << "\n\nand enter the code: "
        << startObj.value("user_code").toString() << "\n\nWaiting for authorization"
        << " (expires in " << startObj.value("expires_in").toInt() / 60 << " min)...\n"
        << Qt::flush;

    QString deviceCode = startObj.value("device_code").toString();
    int interval = qMax(5, startObj.value("interval").toInt(5));
    const qint64 deadline = QDateTime::currentSecsSinceEpoch()
        + startObj.value("expires_in").toInt(900);

    while (QDateTime::currentSecsSinceEpoch() < deadline) {
        QThread::sleep(interval);

        QUrlQuery poll;
        poll.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
        poll.addQueryItem("client_id", m_clientId);
        poll.addQueryItem("device_code", deviceCode);

        const HttpResponse resp = httpRequest(
            QUrl(QString::fromUtf8(kAuthorityBase) + "/token"),
            "POST",
            {{"Content-Type", "application/x-www-form-urlencoded"}},
            formBody(poll));

        const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();

        if (obj.contains("error")) {
            const QString err = obj.value("error").toString();
            if (err == "authorization_pending")
                continue;
            if (err == "slow_down") {
                interval += 5;
                continue;
            }
            QTextStream(stderr) << "Authentication failed: " << err << " — "
                                << obj.value("error_description").toString() << '\n';
            return false;
        }

        out.accessToken = obj.value("access_token").toString();
        out.refreshToken = obj.value("refresh_token").toString();
        out.expiresAtSecs = QDateTime::currentSecsSinceEpoch()
            + obj.value("expires_in").toInt(3600);
        return !out.accessToken.isEmpty();
    }

    QTextStream(stderr) << "Device code expired before authorization completed.\n";
    return false;}

Tokens refreshTokens(const QString &clientId, const QString &scopes, const Tokens &old)
{
    Tokens out;
    if (old.refreshToken.isEmpty())
        return out;

    QUrlQuery body;
    body.addQueryItem("grant_type", "refresh_token");
    body.addQueryItem("client_id", clientId);
    body.addQueryItem("refresh_token", old.refreshToken);
    body.addQueryItem("scope", scopes);

    const HttpResponse resp = httpRequest(
        QUrl(QString::fromUtf8(kAuthorityBase) + "/token"),
        "POST",
        {{"Content-Type", "application/x-www-form-urlencoded"}},
        formBody(body));

    const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();
    out.accessToken = obj.value("access_token").toString();
    out.refreshToken = obj.value("refresh_token").toString();
    if (out.refreshToken.isEmpty())
        out.refreshToken = old.refreshToken;
    out.expiresAtSecs = QDateTime::currentSecsSinceEpoch()
        + obj.value("expires_in").toInt(3600);
    return out;
}

QString msgraphDir()
{
    const QString envDir = qEnvironmentVariable("KALBURATOR_MSGRAPH_DIR");
    if (!envDir.isEmpty())
        return envDir;

    QDir dir = QDir::current();
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath("msgraph");
        if (QFileInfo::exists(candidate + "/GraphCLIinfo.md"))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return QDir::current().filePath("msgraph");
}

QString readClientId(const QString &dir)
{
    const QString envId = qEnvironmentVariable("KALBURATOR_GRAPH_CLIENT_ID");
    if (!envId.isEmpty())
        return envId;

    QFile file(dir + "/GraphCLIinfo.md");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "Cannot read " << dir
                            << "/GraphCLIinfo.md (set KALBURATOR_GRAPH_CLIENT_ID or KALBURATOR_MSGRAPH_DIR)\n";
        return {};
    }
    static const QRegularExpression re("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");
    const QString text = QString::fromUtf8(file.readAll());
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        QTextStream(stderr) << "No Application ID found in GraphCLIinfo.md\n";
        return {};
    }
    return match.captured();
}

void printGraphError(const QString &label, int status, const QByteArray &body)
{
    QTextStream(stderr) << label << " failed";
    if (status > 0)
        QTextStream(stderr) << " (HTTP " << status << ')';
    QTextStream(stderr) << '\n';
    if (!body.isEmpty())
        QTextStream(stderr) << QString::fromUtf8(body) << '\n';
}
