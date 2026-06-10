#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QColor>
#include <QDateTime>

#include "../../src/typesupport/backendconfiguration.h"

using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::DiscoveredCapabilities;
using Kalburator::Sync::PerCalendarCapabilities;

class TstBackendConfigurationJson : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip_minimalFields();
    void roundTrip_allScalarFields();
    void roundTrip_connectionParams();
    void roundTrip_discoveredCapabilities_allFields();
    void roundTrip_perCalendarCapabilities_allFields();
    void roundTrip_disabled();
    void isValid_requiresIdAndType();
    void resolvedDisplayName_fallbacks();
    void friendlyTypeName_knownTypes();
    void fromJson_ignoresUnknownFields();
};

void TstBackendConfigurationJson::roundTrip_minimalFields()
{
    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("test-id");
    cfg.type = QStringLiteral("caldav");

    const QJsonObject json = cfg.toJson();
    const BackendConfiguration rt = BackendConfiguration::fromJson(json);

    QCOMPARE(rt.id,   cfg.id);
    QCOMPARE(rt.type, cfg.type);
    QVERIFY(rt.displayName.isEmpty());
    QVERIFY(rt.enabled);
    QVERIFY(rt.connectionParams.isEmpty());
    QVERIFY(!rt.discoveredCapabilities.isValid());
}

void TstBackendConfigurationJson::roundTrip_allScalarFields()
{
    BackendConfiguration cfg;
    cfg.id          = QStringLiteral("my-backend");
    cfg.type        = QStringLiteral("local");
    cfg.displayName = QStringLiteral("My Local Storage");
    cfg.enabled     = true;

    const BackendConfiguration rt = BackendConfiguration::fromJson(cfg.toJson());

    QCOMPARE(rt.id,          cfg.id);
    QCOMPARE(rt.type,        cfg.type);
    QCOMPARE(rt.displayName, cfg.displayName);
    QVERIFY(rt.enabled);
}

void TstBackendConfigurationJson::roundTrip_connectionParams()
{
    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("dav-1");
    cfg.type = QStringLiteral("caldav");
    cfg.connectionParams[QStringLiteral("url")]      = QStringLiteral("https://cloud.example.com/dav/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");
    cfg.connectionParams[QStringLiteral("password")] = QStringLiteral("hunter2");
    cfg.connectionParams[QStringLiteral("rootPath")] = QStringLiteral("/principals/users/alice/");

    const BackendConfiguration rt = BackendConfiguration::fromJson(cfg.toJson());

    QCOMPARE(rt.url(),      QStringLiteral("https://cloud.example.com/dav/"));
    QCOMPARE(rt.username(), QStringLiteral("alice"));
    QCOMPARE(rt.password(), QStringLiteral("hunter2"));
    QCOMPARE(rt.rootPath(), QStringLiteral("/principals/users/alice/"));
}

void TstBackendConfigurationJson::roundTrip_discoveredCapabilities_allFields()
{
    const QDateTime discovered = QDateTime::fromString(
        QStringLiteral("2026-06-10T12:00:00Z"), Qt::ISODate);

    DiscoveredCapabilities caps;
    caps.discoveredAt               = discovered;
    caps.serverProduct              = QStringLiteral("Nextcloud");
    caps.serverVersion              = QStringLiteral("29.0.0");
    caps.supportsCalendarCreation   = false;
    caps.supportedComponentTypes    = {QStringLiteral("VEVENT"), QStringLiteral("VTODO")};
    caps.maxResourceSize            = 102400;
    caps.hasDateBook                = true;
    caps.hasToDo                    = true;

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("c1");
    cfg.type = QStringLiteral("caldav");
    cfg.discoveredCapabilities = caps;

    const BackendConfiguration rt = BackendConfiguration::fromJson(cfg.toJson());
    const DiscoveredCapabilities &rc = rt.discoveredCapabilities;

    QVERIFY(rc.isValid());
    QCOMPARE(rc.discoveredAt,             discovered);
    QCOMPARE(rc.serverProduct,            QStringLiteral("Nextcloud"));
    QCOMPARE(rc.serverVersion,            QStringLiteral("29.0.0"));
    QVERIFY(!rc.supportsCalendarCreation);
    QCOMPARE(rc.supportedComponentTypes,  (QStringList{QStringLiteral("VEVENT"), QStringLiteral("VTODO")}));
    QCOMPARE(rc.maxResourceSize,          102400);
    QVERIFY(rc.hasDateBook);
    QVERIFY(rc.hasToDo);
}

void TstBackendConfigurationJson::roundTrip_perCalendarCapabilities_allFields()
{
    PerCalendarCapabilities pcaps;
    pcaps.supportsVEvent    = false;
    pcaps.supportsVTodo     = true;
    pcaps.supportsVJournal  = true;
    pcaps.writable          = false;
    pcaps.serverColor       = QColor(Qt::red);
    pcaps.serverDisplayName = QStringLiteral("Tasks Only");
    pcaps.maxResourceSize   = 65536;

    DiscoveredCapabilities caps;
    caps.discoveredAt = QDateTime::fromString(QStringLiteral("2026-06-10T00:00:00Z"), Qt::ISODate);
    caps.perCalendarCapabilities[QStringLiteral("/cal/tasks/")] = pcaps;

    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("c2");
    cfg.type = QStringLiteral("caldav");
    cfg.discoveredCapabilities = caps;

    const BackendConfiguration rt = BackendConfiguration::fromJson(cfg.toJson());
    QVERIFY(rt.discoveredCapabilities.perCalendarCapabilities.contains(QStringLiteral("/cal/tasks/")));

    const PerCalendarCapabilities &rpc
        = rt.discoveredCapabilities.perCalendarCapabilities[QStringLiteral("/cal/tasks/")];
    QVERIFY(!rpc.supportsVEvent);
    QVERIFY(rpc.supportsVTodo);
    QVERIFY(rpc.supportsVJournal);
    QVERIFY(!rpc.writable);
    QCOMPARE(rpc.serverDisplayName, QStringLiteral("Tasks Only"));
    QCOMPARE(rpc.maxResourceSize,   65536);
    // Color round-trips through hex string
    QVERIFY(rpc.serverColor.isValid());
    QCOMPARE(rpc.serverColor.name(QColor::HexArgb), pcaps.serverColor.name(QColor::HexArgb));
}

void TstBackendConfigurationJson::roundTrip_disabled()
{
    BackendConfiguration cfg;
    cfg.id      = QStringLiteral("d1");
    cfg.type    = QStringLiteral("local");
    cfg.enabled = false;

    const BackendConfiguration rt = BackendConfiguration::fromJson(cfg.toJson());
    QVERIFY(!rt.enabled);
}

void TstBackendConfigurationJson::isValid_requiresIdAndType()
{
    BackendConfiguration cfg;
    QVERIFY(!cfg.isValid());
    cfg.id = QStringLiteral("x");
    QVERIFY(!cfg.isValid());
    cfg.type = QStringLiteral("local");
    QVERIFY(cfg.isValid());
}

void TstBackendConfigurationJson::resolvedDisplayName_fallbacks()
{
    BackendConfiguration cfg;
    cfg.id   = QStringLiteral("myid");
    cfg.type = QStringLiteral("caldav");

    // No displayName → "CalDAV (myid)"
    QCOMPARE(cfg.resolvedDisplayName(), QStringLiteral("CalDAV (myid)"));

    // With explicit displayName → use it
    cfg.displayName = QStringLiteral("My Nextcloud");
    QCOMPARE(cfg.resolvedDisplayName(), QStringLiteral("My Nextcloud"));
}

void TstBackendConfigurationJson::friendlyTypeName_knownTypes()
{
    QCOMPARE(BackendConfiguration::friendlyTypeName(QStringLiteral("caldav")),       QStringLiteral("CalDAV"));
    QCOMPARE(BackendConfiguration::friendlyTypeName(QStringLiteral("local")),        QStringLiteral("Local Storage"));
    QCOMPARE(BackendConfiguration::friendlyTypeName(QStringLiteral("orgmode")),      QStringLiteral("Org Mode"));
    QCOMPARE(BackendConfiguration::friendlyTypeName(QStringLiteral("akonadi")),      QStringLiteral("Akonadi"));
    QCOMPARE(BackendConfiguration::friendlyTypeName(QStringLiteral("subscription")), QStringLiteral("Subscription"));
    QVERIFY(!BackendConfiguration::friendlyTypeName(QString()).isEmpty() == false); // empty → empty
}

void TstBackendConfigurationJson::fromJson_ignoresUnknownFields()
{
    QJsonObject json;
    json[QStringLiteral("id")]           = QStringLiteral("z");
    json[QStringLiteral("type")]         = QStringLiteral("local");
    json[QStringLiteral("unknownField")] = QStringLiteral("should-be-in-connectionParams");

    const BackendConfiguration cfg = BackendConfiguration::fromJson(json);
    QVERIFY(cfg.isValid());
    // Unknown fields land in connectionParams (passthrough)
    QVERIFY(cfg.connectionParams.contains(QStringLiteral("unknownField")));
}

QTEST_GUILESS_MAIN(TstBackendConfigurationJson)
#include "tst_backendconfiguration_json.moc"
