#include <QtTest>

#include "credentialredaction.h"

using Kalburator::Sync::redactCredentials;

class TestCredentialRedaction : public QObject
{
    Q_OBJECT
private slots:
    void removesUrlPassword()
    {
        const QString safe = redactCredentials(QUrl(QStringLiteral(
            "https://alice:s3cret@example.test/dav")));
        QVERIFY(!safe.contains(QStringLiteral("s3cret")));
        QVERIFY(safe.contains(QStringLiteral("alice@")));
    }

    void removesCredentialsFromDiagnosticText()
    {
        const QString safe = redactCredentials(QStringLiteral(
            "request failed: https://alice:s3cret@example.test/dav"));
        QVERIFY(!safe.contains(QStringLiteral("s3cret")));
        QVERIFY(safe.contains(QStringLiteral("<redacted>")));
    }
};

QTEST_GUILESS_MAIN(TestCredentialRedaction)
#include "tst_credentialredaction.moc"
