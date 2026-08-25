#pragma once

// B2C P0 — Google OAuth transport, extracted from the googlecli lab
// (tools/googlecli/googleauth). Protocol logic only: tokens, cache,
// loopback code flow, refresh grant. Lab-path discovery (googledir(),
// credential scraping) and error printing stay in the tool.
//
// The browser launcher is injectable (the library never shells out);
// endpoints are injectable so tests can mock the token grants.
// Epoch-seconds expiry only — no wall-time QDateTime construction.

#include <QString>
#include <QUrl>
#include <functional>

namespace Kalburator::Google {

struct Tokens {
    QString accessToken;
    QString refreshToken;
    qint64 expiresAtSecs = 0;
    QString grantedScopes;   // verbatim scope string from the token response

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

struct ClientCredentials {
    QString clientId;
    QString clientSecret;
    bool valid() const { return !clientId.isEmpty() && !clientSecret.isEmpty(); }
};

/// OAuth 2.0 installed-application flow with loopback redirect:
/// listen on 127.0.0.1:<ephemeral port>, open the user's browser at the
/// authorization endpoint, capture the ?code=... redirect, exchange it
/// for tokens. Blocking by design (interactive flow); 5-minute timeout;
/// single-request latch (see implementation notes).
class LoopbackCodeFlow {
public:
    LoopbackCodeFlow(ClientCredentials creds, QString scopes);

    void setAuthEndpoint(const QString &url);
    void setTokenEndpoint(const QString &url);

    /// Browser-launch hook. Default: none (URL is still printed).
    /// The CLI installs an xdg-open launcher here.
    void setBrowserLauncher(std::function<void(const QUrl &)> launcher);

    bool runInteractive(Tokens &out) const;

private:
    ClientCredentials m_creds;
    QString m_scopes;
    QString m_authEndpoint =
        QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
    QString m_tokenEndpoint =
        QStringLiteral("https://oauth2.googleapis.com/token");
    std::function<void(const QUrl &)> m_launcher;
};

Tokens refreshTokens(const ClientCredentials &creds, const Tokens &old);

} // namespace Kalburator::Google
