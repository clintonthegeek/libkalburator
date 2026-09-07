#include <kalburator/sync/credentialredaction.h>

#include <QRegularExpression>

namespace Kalburator::Sync {
QString redactCredentials(const QUrl &url)
{
    QUrl safe = url;
    safe.setPassword(QString{});
    return safe.toString(QUrl::FullyEncoded);
}

QString redactCredentials(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"((https?://[^/\s:@]+):[^/@\s]+@)"),
        QRegularExpression::CaseInsensitiveOption);
    QString safe = text;
    return safe.replace(re, QStringLiteral("\\1:<redacted>@"));
}
} // namespace Kalburator::Sync
