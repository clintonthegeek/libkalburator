#pragma once

// B2C P0 — Microsoft Graph OAuth transport, extracted from the GraphCLI
// lab (tools/graphcli/graphauth). Protocol logic only: tokens, cache,
// device-code flow, refresh grant. Lab-path discovery (msgraphDir(),
// client-id scraping) and consent presentation stay in the tool.
//
// runInteractive() is blocking by design (device-code is inherently an
// interactive console flow for now); the authority base URL is injectable
// so tests can point the flow at a mock. Epoch-seconds expiry only — no
// wall-time QDateTime construction (house O60 rule).

#include <QString>

namespace Kalburator::Graph {

struct Tokens {
    QString accessToken;
    QString refreshToken;
    qint64 expiresAtSecs = 0;

    bool hasLiveAccessToken() const;
};

class TokenStore {
public:
    explicit TokenStore(QString cachePath);

    Tokens load() const;
    void save(const Tokens &tokens) const;
    void clear() const;

private:
    QString m_path;
};

class DeviceCodeFlow {
public:
    explicit DeviceCodeFlow(QString clientId, QString scopes,
                            bool promptConsent = false);

    /// Override the v2.0 authority base (default: login.microsoftonline.com/
    /// common/oauth2/v2.0). Injection point for mock-server tests.
    void setAuthorityBase(const QString &authorityBase);

    bool runInteractive(Tokens &out) const;

private:
    QString m_clientId;
    QString m_scopes;
    bool m_promptConsent;
    QString m_authorityBase =
        QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0");
};

Tokens refreshTokens(const QString &clientId, const QString &scopes,
                     const Tokens &old,
                     const QString &authorityBase =
                         QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0"));

} // namespace Kalburator::Graph
