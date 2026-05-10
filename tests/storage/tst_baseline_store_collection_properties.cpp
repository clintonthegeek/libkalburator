#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QVariantMap>

#include "baselinestore.h"

using Kalburator::Storage::BaselineStore;

class TstBaselineStoreCollectionProperties : public QObject {
    Q_OBJECT
private slots:
    void roundTripMap();
    void overwriteSameKey();
    void distinctMappings();
    void removeClearsRow();
    void absentKeyReturnsEmpty();
};

void TstBaselineStoreCollectionProperties::roundTripMap() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(store.isOpen());
    QVariantMap props{
        {QStringLiteral("color"),       QStringLiteral("#ff0000")},
        {QStringLiteral("description"), QStringLiteral("My calendar")},
    };
    QVERIFY(store.setCollectionBaseline("m1", "cal1", props));
    const auto out = store.collectionBaseline("m1", "cal1");
    QCOMPARE(out, props);
}

void TstBaselineStoreCollectionProperties::overwriteSameKey() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setCollectionBaseline("m1", "cal1", {{"color", "#000"}});
    QVERIFY(store.setCollectionBaseline("m1", "cal1", {{"color", "#fff"}}));
    QCOMPARE(store.collectionBaseline("m1", "cal1").value("color").toString(),
             QStringLiteral("#fff"));
}

void TstBaselineStoreCollectionProperties::distinctMappings() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setCollectionBaseline("m1", "cal1", {{"color", "red"}});
    store.setCollectionBaseline("m2", "cal1", {{"color", "blue"}});
    QCOMPARE(store.collectionBaseline("m1", "cal1").value("color").toString(),
             QStringLiteral("red"));
    QCOMPARE(store.collectionBaseline("m2", "cal1").value("color").toString(),
             QStringLiteral("blue"));
}

void TstBaselineStoreCollectionProperties::removeClearsRow() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setCollectionBaseline("m1", "cal1", {{"color", "red"}});
    QVERIFY(store.removeCollectionBaseline("m1", "cal1"));
    QVERIFY(store.collectionBaseline("m1", "cal1").isEmpty());
}

void TstBaselineStoreCollectionProperties::absentKeyReturnsEmpty() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(store.collectionBaseline("nonexistent", "x").isEmpty());
}

QTEST_MAIN(TstBaselineStoreCollectionProperties)
#include "tst_baseline_store_collection_properties.moc"
