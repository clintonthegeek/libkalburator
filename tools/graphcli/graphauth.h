#pragma once

#include <QString>

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
    DeviceCodeFlow(QString clientId, QString scopes, bool promptConsent = false);

    bool runInteractive(Tokens &out) const;

private:
    QString m_clientId;
    QString m_scopes;
    bool m_promptConsent;
};

Tokens refreshTokens(const QString &clientId, const QString &scopes, const Tokens &old);

QString msgraphDir();
QString readClientId(const QString &dir);
void printGraphError(const QString &label, int status, const QByteArray &body);
