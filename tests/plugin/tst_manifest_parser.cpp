#include <QtTest/QtTest>
#include "manifest.h"

using namespace Kalburator;

class TestManifestParser : public QObject {
    Q_OBJECT
private slots:
    void parsesAllFields() {
        const QJsonObject json{
            {"id", "com.example.docs-to-go"},
            {"version", "1.2.0"},
            {"displayName", "Documents to Go"},
            {"kalburatorPluginVersion", "1.0"},
            {"definesDomains", QJsonArray{"office.document", "office.spreadsheet"}},
            {"requiresDomains", QJsonArray{}}
        };
        QString err;
        const auto m = PluginManifest::fromJson(json, &err);
        QVERIFY2(m.has_value(), qPrintable(err));
        QCOMPARE(m->id, QStringLiteral("com.example.docs-to-go"));
        QCOMPARE(m->version, QStringLiteral("1.2.0"));
        QCOMPARE(m->definesDomains.size(), 2);
        QCOMPARE(m->definesDomains.first(), QStringLiteral("office.document"));
        QVERIFY(m->requiresDomains.isEmpty());
    }

    void missingIdRejected() {
        QString err;
        const auto m = PluginManifest::fromJson(QJsonObject{{"version", "1.0"}}, &err);
        QVERIFY(!m.has_value());
        QVERIFY(err.contains(QStringLiteral("id")));
    }

    void requiresDomainsDefaultsToEmpty() {
        QString err;
        const auto m = PluginManifest::fromJson(QJsonObject{
            {"id", "x"}, {"version", "1.0"}, {"kalburatorPluginVersion", "1.0"}
        }, &err);
        QVERIFY2(m.has_value(), qPrintable(err));
        QVERIFY(m->requiresDomains.isEmpty());
        QVERIFY(m->definesDomains.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestManifestParser)
#include "tst_manifest_parser.moc"
