#pragma once

#include <QString>

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

/// OAuth 2.0 installed-application flow with loopback redirect
/// (docs/google_rest.md §3): listen on 127.0.0.1:<ephemeral port>, open the
/// user's browser at Google's authorization endpoint, capture the
/// ?code=... redirect, exchange it for tokens.
class LoopbackCodeFlow {
public:
    LoopbackCodeFlow(ClientCredentials creds, QString scopes);

    bool runInteractive(Tokens &out) const;

private:
    ClientCredentials m_creds;
    QString m_scopes;
};

Tokens refreshTokens(const ClientCredentials &creds, const Tokens &old);

QString googledir();
ClientCredentials readClientCredentials(const QString &dir);
void printGoogleError(const QString &label, int status, const QByteArray &body);
