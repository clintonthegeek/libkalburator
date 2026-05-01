#include <QHash>
#include <QStringList>
#include <QTest>

#include "propertycatalogue.h"

using namespace Kalburator::Shape;

class TestPropertyCatalogue : public QObject {
    Q_OBJECT
private slots:
    void emptyCatalogue() {
        PropertyCatalogue c;
        QVERIFY(c.properties().isEmpty());
        QVERIFY(!c.hasProperty(PropertyId{QStringLiteral("anything")}));
        QVERIFY(c.find(PropertyId{QStringLiteral("anything")}) == nullptr);
        QVERIFY(c.sqlColumnDdl().isEmpty());
    }

    void addAndFind() {
        PropertyCatalogue c;
        c.addProperty({ PropertyId{QStringLiteral("uid")},
                        PropertyKind::String,
                        QStringLiteral("UID"),
                        /*optional*/ false });
        QVERIFY(c.hasProperty(PropertyId{QStringLiteral("uid")}));
        const auto* p = c.find(PropertyId{QStringLiteral("uid")});
        QVERIFY(p != nullptr);
        QCOMPARE(p->id.toString(), QStringLiteral("uid"));
        QCOMPARE(p->kind, PropertyKind::String);
        QCOMPARE(p->displayName, QStringLiteral("UID"));
        QVERIFY(!p->optional);
    }

    void orderPreserved() {
        PropertyCatalogue c;
        c.addProperty({ PropertyId{QStringLiteral("a")}, PropertyKind::String, {}, true });
        c.addProperty({ PropertyId{QStringLiteral("b")}, PropertyKind::Integer, {}, true });
        c.addProperty({ PropertyId{QStringLiteral("c")}, PropertyKind::Boolean, {}, true });
        QCOMPARE(c.properties().size(), 3);
        QCOMPARE(c.properties().at(0).id.toString(), QStringLiteral("a"));
        QCOMPARE(c.properties().at(1).id.toString(), QStringLiteral("b"));
        QCOMPARE(c.properties().at(2).id.toString(), QStringLiteral("c"));
    }

    void idempotentAddReplacesDescriptor() {
        PropertyCatalogue c;
        c.addProperty({ PropertyId{QStringLiteral("uid")}, PropertyKind::String, {}, true });
        c.addProperty({ PropertyId{QStringLiteral("uid")}, PropertyKind::String, QStringLiteral("UID"), false });
        QCOMPARE(c.properties().size(), 1);
        const auto* p = c.find(PropertyId{QStringLiteral("uid")});
        QCOMPARE(p->displayName, QStringLiteral("UID"));
        QVERIFY(!p->optional);
    }

    void sqlColumnDdlMapsKinds() {
        PropertyCatalogue c;
        c.addProperty({ PropertyId{QStringLiteral("name")},     PropertyKind::String,     {}, false });
        c.addProperty({ PropertyId{QStringLiteral("count")},    PropertyKind::Integer,    {}, true });
        c.addProperty({ PropertyId{QStringLiteral("flag")},     PropertyKind::Boolean,    {}, true });
        c.addProperty({ PropertyId{QStringLiteral("when")},     PropertyKind::DateTime,   {}, true });
        c.addProperty({ PropertyId{QStringLiteral("dur")},      PropertyKind::Duration,   {}, true });
        c.addProperty({ PropertyId{QStringLiteral("payload")},  PropertyKind::Bytes,      {}, true });
        c.addProperty({ PropertyId{QStringLiteral("tags")},     PropertyKind::StringList, {}, true });
        c.addProperty({ PropertyId{QStringLiteral("meta")},     PropertyKind::Json,       {}, true });
        QStringList ddl = c.sqlColumnDdl();
        QCOMPARE(ddl.size(), 8);
        QCOMPARE(ddl.at(0), QStringLiteral("name TEXT NOT NULL"));
        QCOMPARE(ddl.at(1), QStringLiteral("count INTEGER"));
        QCOMPARE(ddl.at(2), QStringLiteral("flag INTEGER"));
        QCOMPARE(ddl.at(3), QStringLiteral("when TEXT"));
        QCOMPARE(ddl.at(4), QStringLiteral("dur TEXT"));
        QCOMPARE(ddl.at(5), QStringLiteral("payload BLOB"));
        QCOMPARE(ddl.at(6), QStringLiteral("tags TEXT"));
        QCOMPARE(ddl.at(7), QStringLiteral("meta TEXT"));
    }

    void propertyIdHashable() {
        QHash<PropertyId, int> h;
        h.insert(PropertyId{QStringLiteral("uid")}, 1);
        QCOMPARE(h.value(PropertyId{QStringLiteral("uid")}), 1);
    }
};

QTEST_GUILESS_MAIN(TestPropertyCatalogue)
#include "tst_property_catalogue.moc"
