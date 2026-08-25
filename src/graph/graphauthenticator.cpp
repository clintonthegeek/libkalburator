#include "graphauthenticator.h"
#include "blockinghttp.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

namespace Kalburator::Graph {

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

DeviceCodeFlow::DeviceCodeFlow(QString clientId, QString scopes,
                               bool promptConsent)
    : m_clientId(std::move(clientId))
    , m_scopes(std::move(scopes))
    , m_promptConsent(promptConsent)
{
}

void DeviceCodeFlow::setAuthorityBase(const QString &authorityBase)
{
    m_authorityBase = authorityBase;
}

bool DeviceCodeFlow::runInteractive(Tokens &out) const
{
    QUrlQuery start;
    start.addQueryItem("client_id", m_clientId);
    start.addQueryItem("scope", m_scopes);
    if (m_promptConsent)
        start.addQueryItem("prompt", "consent");

    const Net::HttpResponse startResp = Net::httpRequest(
        QUrl(m_authorityBase + "/devicecode"),
        "POST",
        {{"Content-Type", "application/x-www-form-urlencoded"}},
        formBody(start));

    const QJsonObject startObj = QJsonDocument::fromJson(startResp.body).object();
    if (!startResp.ok() || startObj.isEmpty()) {
        QTextStream(stderr) << "device code request failed";
        if (startResp.status > 0)
            QTextStream(stderr) << " (HTTP " << startResp.status << ')';
        QTextStream(stderr) << '\n' << QString::fromUtf8(startResp.body) << '\n';
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

        const Net::HttpResponse resp = Net::httpRequest(
            QUrl(m_authorityBase + "/token"),
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
    return false;
}

Tokens refreshTokens(const QString &clientId, const QString &scopes,
                     const Tokens &old, const QString &authorityBase)
{
    Tokens out;
    if (old.refreshToken.isEmpty())
        return out;

    QUrlQuery body;
    body.addQueryItem("grant_type", "refresh_token");
    body.addQueryItem("client_id", clientId);
    body.addQueryItem("refresh_token", old.refreshToken);
    body.addQueryItem("scope", scopes);

    const Net::HttpResponse resp = Net::httpRequest(
        QUrl(authorityBase + "/token"),
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

} // namespace Kalburator::Graph
