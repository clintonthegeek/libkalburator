#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDateTime>

#include "baselinestore.h"

using Kalburator::Storage::BaselineStore;

class TstBaselineStoreMappingMetadata : public QObject {
    Q_OBJECT
private slots:
    void roundTripTimestamp();
    void absentReturnsInvalid();
    void overwriteUpdates();
    void distinctMappings();
};

void TstBaselineStoreMappingMetadata::roundTripTimestamp() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(store.isOpen());
    const QDateTime t = QDateTime::fromSecsSinceEpoch(1700000000);
    QVERIFY(store.setLastSyncTime("m1", t));
    QCOMPARE(store.lastSyncTime("m1"), t);
}

void TstBaselineStoreMappingMetadata::absentReturnsInvalid() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(!store.lastSyncTime("nonexistent").isValid());
}

void TstBaselineStoreMappingMetadata::overwriteUpdates() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(100));
    store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(200));
    QCOMPARE(store.lastSyncTime("m1"), QDateTime::fromSecsSinceEpoch(200));
}

void TstBaselineStoreMappingMetadata::distinctMappings() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(100));
    store.setLastSyncTime("m2", QDateTime::fromSecsSinceEpoch(200));
    QCOMPARE(store.lastSyncTime("m1"), QDateTime::fromSecsSinceEpoch(100));
    QCOMPARE(store.lastSyncTime("m2"), QDateTime::fromSecsSinceEpoch(200));
}

QTEST_MAIN(TstBaselineStoreMappingMetadata)
#include "tst_baseline_store_mapping_metadata.moc"
