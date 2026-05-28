/// Tests for RecordFilter + FilteredCollectionBackend
/// (consumer RFC 2026-05-28).

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "recordfilter.h"
#include "filteredcollectionbackend.h"
#include "backendregistry.h"
#include "syncbackend.h"

using Kalburator::Shape::PropertyId;
using Kalburator::Shape::RecordFilter;
using Kalburator::Sinks::FilteredCollectionBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::SyncBackend;
using Kalburator::Shape::Shape;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace {

QByteArray canonJson(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QJsonObject withCategories(const QStringList& cats)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    QJsonArray arr;
    for (const auto& c : cats) arr.append(c);
    obj.insert(QStringLiteral("categories"), arr);
    return obj;
}

const Shape kCalendarCanonShape{ DomainId{"calendar"}, EncodingId{"canon"} };

/// Minimal in-memory SyncBackend used as a filtered-view parent in tests.
/// Holds a single collection of BackendRecords keyed by record id; ops
/// (load/create/update/delete) record their effects in the in-memory map.
class FakeParentBackend : public SyncBackend {
    Q_OBJECT
public:
    explicit FakeParentBackend(QString backendId,
                               QString collectionId,
                               Shape shape,
                               QObject* parent = nullptr)
        : SyncBackend(parent)
        , m_backendId(std::move(backendId))
        , m_colId(std::move(collectionId))
        , m_shape(shape) {}

    QString backendId()    const override { return m_backendId; }
    QString backendType()  const override { return QStringLiteral("fake-parent"); }
    QString displayName()  const override { return QStringLiteral("Fake Parent"); }
    QString resourceId()   const override { return QStringLiteral("fake://") + m_backendId; }
    bool    isAvailable()  const override { return true; }

    QList<Shape> nativeShapes() const override { return { m_shape }; }
    Shape shapeFor(const QString&) const override { return m_shape; }

    QList<CollectionInfo> availableCollections() override {
        CollectionInfo ci;
        ci.id = m_colId;
        ci.name = m_colName;
        ci.readOnly = m_readOnly;
        return { ci };
    }
    CollectionInfo collectionInfo(const QString& id) override {
        if (id != m_colId) return {};
        CollectionInfo ci;
        ci.id = m_colId;
        ci.name = m_colName;
        ci.readOnly = m_readOnly;
        return ci;
    }

    bool discoveredWritable(const QString& id) const override {
        return id == m_colId ? !m_readOnly : false;
    }

    QList<BackendRecord> loadRecords(const QString& id) override {
        if (id != m_colId) return {};
        return m_records.values();
    }
    std::optional<BackendRecord> loadRecord(const QString& recordId) override {
        if (!m_records.contains(recordId)) return std::nullopt;
        return m_records.value(recordId);
    }
    QString createRecord(const QString& id, const BackendRecord& r) override {
        if (id != m_colId) return {};
        BackendRecord copy = r;
        if (copy.id.isEmpty()) copy.id = QStringLiteral("auto-%1").arg(++m_autoId);
        m_records.insert(copy.id, copy);
        m_lastWritten = copy;
        return copy.id;
    }
    bool updateRecord(const BackendRecord& r) override {
        if (!m_records.contains(r.id)) return false;
        m_records.insert(r.id, r);
        m_lastWritten = r;
        return true;
    }
    bool deleteRecord(const QString& recordId) override {
        return m_records.remove(recordId) > 0;
    }

    void setRecord(const BackendRecord& r) { m_records.insert(r.id, r); }
    void setReadOnly(bool ro) { m_readOnly = ro; }
    void setColName(QString n) { m_colName = std::move(n); }

    const BackendRecord& lastWritten() const { return m_lastWritten; }
    int recordCount() const { return m_records.size(); }

private:
    QString m_backendId;
    QString m_colId;
    QString m_colName = QStringLiteral("Parent Collection");
    Shape   m_shape;
    bool    m_readOnly = false;
    int     m_autoId = 0;
    QHash<QString, BackendRecord> m_records;
    BackendRecord m_lastWritten;
};

BackendRecord makeJsonRecord(const QString& id, const QJsonObject& obj)
{
    BackendRecord r;
    r.id = id;
    r.displayName = id;
    r.type = QStringLiteral("event");
    r.data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return r;
}

} // namespace

class TestFilteredCollectionBackend : public QObject
{
    Q_OBJECT
private slots:
    // ---- RecordFilter (Task 1) -------------------------------------------
    void filter_contains_matchingArrayElement_returnsTrue();
    void filter_contains_nonMatchingArray_returnsFalse();
    void filter_contains_caseSensitive_doesNotMatchDifferentCase();
    void filter_contains_propertyAbsent_returnsFalse();
    void filter_contains_propertyNotAnArray_returnsFalse();
    void filter_equals_matchingScalar_returnsTrue();
    void filter_equals_nonMatching_returnsFalse();
    void filter_unparseableBytes_returnsFalse();
    void filter_emptyPropertyId_returnsFalse();
    void filter_documentOverload_rootArrayDoc_returnsFalse();

    // ---- Identity / shape / collectionInfo (Task 2) ----------------------
    void identity_backendType_isFilteredView();
    void identity_displayName_overrideWins();
    void identity_displayName_composedDefault_includesParentNameAndFilter();
    void shape_delegatesToParentsShapeForParentColId();
    void availableCollections_returnsOneVirtualEntry();
    void collectionInfo_unknownId_returnsDefault();
    void collectionInfo_inheritsReadOnlyFromParent();

    // ---- Read filtering (Task 3) -----------------------------------------
    void loadRecords_returnsOnlyMatchingRecords();
    void loadRecords_excludesRecordsWithoutFilterProperty();
    void loadRecords_excludesNonJsonPayloads();
    void loadRecord_matching_returnsRecord();
    void loadRecord_nonMatching_returnsNullopt();
    void loadRecord_unknownId_returnsNullopt();

    // ---- Write stamping (Task 4) -----------------------------------------
    void createRecord_contains_appendsFilterValueIfAbsent();
    void createRecord_contains_noDuplicateIfAlreadyPresent();
    void createRecord_contains_preservesExistingOrder();
    void createRecord_contains_preservesOtherCategoryValues();
    void createRecord_contains_payloadHasNoCategoriesField_addsArrayWithFilterValue();
    void createRecord_equals_alwaysOverwritesFilterProperty();
    void updateRecord_contains_stampsAndUpdatesParent();
    void updateRecord_equals_overwritesFilterProperty();
    void createRecord_unknownCollectionId_returnsEmpty();
    void createRecord_nonJsonPayload_passesThroughUnchanged();
};

void TestFilteredCollectionBackend::filter_contains_matchingArrayElement_returnsTrue()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    const QJsonObject rec = withCategories({"Personal", "Work"});
    QVERIFY(f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_nonMatchingArray_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    const QJsonObject rec = withCategories({"Personal", "Family"});
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_caseSensitive_doesNotMatchDifferentCase()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    const QJsonObject rec = withCategories({"work"});
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_propertyAbsent_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_contains_propertyNotAnArray_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    rec.insert(QStringLiteral("categories"), QStringLiteral("Work"));  // scalar
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_equals_matchingScalar_returnsTrue()
{
    RecordFilter f{ PropertyId{"status"}, RecordFilter::Op::Equals,
                    QStringLiteral("Done") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    rec.insert(QStringLiteral("status"), QStringLiteral("Done"));
    QVERIFY(f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_equals_nonMatching_returnsFalse()
{
    RecordFilter f{ PropertyId{"status"}, RecordFilter::Op::Equals,
                    QStringLiteral("Done") };
    QJsonObject rec;
    rec.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    rec.insert(QStringLiteral("status"), QStringLiteral("InProgress"));
    QVERIFY(!f.matches(canonJson(rec)));
}

void TestFilteredCollectionBackend::filter_unparseableBytes_returnsFalse()
{
    RecordFilter f{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QVERIFY(!f.matches(QByteArray("not json at all")));
    QVERIFY(!f.matches(QByteArray()));
}

void TestFilteredCollectionBackend::filter_emptyPropertyId_returnsFalse()
{
    RecordFilter f{ PropertyId{}, RecordFilter::Op::Contains,
                    QStringLiteral("Work") };
    QVERIFY(!f.matches(canonJson(withCategories({"Work"}))));
}

void TestFilteredCollectionBackend::filter_documentOverload_rootArrayDoc_returnsFalse()
{
    RecordFilter f{ PropertyId{"uid"}, RecordFilter::Op::Equals,
                    QStringLiteral("u1") };
    QJsonDocument arrayDoc(QJsonArray{ QStringLiteral("a") });
    QVERIFY(!f.matches(arrayDoc));
}

void TestFilteredCollectionBackend::identity_backendType_isFilteredView()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QCOMPARE(v.backendType(), QStringLiteral("filtered-view"));
}

void TestFilteredCollectionBackend::identity_displayName_overrideWins()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") },
                                /*registry=*/nullptr,
                                QStringLiteral("Work Route"));
    QCOMPARE(v.displayName(), QStringLiteral("Work Route"));
}

void TestFilteredCollectionBackend::identity_displayName_composedDefault_includesParentNameAndFilter()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setColName(QStringLiteral("Calendar"));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const QString name = v.displayName();
    QVERIFY2(name.contains(QStringLiteral("Calendar")), qPrintable(name));
    QVERIFY2(name.contains(QStringLiteral("categories")), qPrintable(name));
    QVERIFY2(name.contains(QStringLiteral("Work")), qPrintable(name));
}

void TestFilteredCollectionBackend::shape_delegatesToParentsShapeForParentColId()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QCOMPARE(v.shapeFor("v1"), kCalendarCanonShape);
    QCOMPARE(v.nativeShapes().size(), 1);
    QCOMPARE(v.nativeShapes().first(), kCalendarCanonShape);
}

void TestFilteredCollectionBackend::availableCollections_returnsOneVirtualEntry()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto cols = v.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.first().id, QStringLiteral("v1"));
}

void TestFilteredCollectionBackend::collectionInfo_unknownId_returnsDefault()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto info = v.collectionInfo("other");
    QCOMPARE(info.id, QString());
}

void TestFilteredCollectionBackend::collectionInfo_inheritsReadOnlyFromParent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setReadOnly(true);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(v.collectionInfo("v1").readOnly);
}

void TestFilteredCollectionBackend::loadRecords_returnsOnlyMatchingRecords()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Personal"})));
    parent.setRecord(makeJsonRecord("r2", withCategories({"Work"})));
    parent.setRecord(makeJsonRecord("r3", withCategories({"Work", "Personal"})));

    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto recs = v.loadRecords("v1");
    QCOMPARE(recs.size(), 2);
    QSet<QString> ids;
    for (const auto& r : recs) ids.insert(r.id);
    QVERIFY(ids.contains("r2"));
    QVERIFY(ids.contains("r3"));
}

void TestFilteredCollectionBackend::loadRecords_excludesRecordsWithoutFilterProperty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    QJsonObject noCats;
    noCats.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    parent.setRecord(makeJsonRecord("r1", noCats));
    parent.setRecord(makeJsonRecord("r2", withCategories({"Work"})));

    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto recs = v.loadRecords("v1");
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs.first().id, QStringLiteral("r2"));
}

void TestFilteredCollectionBackend::loadRecords_excludesNonJsonPayloads()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    BackendRecord bad;
    bad.id = "bad";
    bad.data = QByteArray("not json at all");
    parent.setRecord(bad);
    parent.setRecord(makeJsonRecord("good", withCategories({"Work"})));

    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto recs = v.loadRecords("v1");
    QCOMPARE(recs.size(), 1);
    QCOMPARE(recs.first().id, QStringLiteral("good"));
}

void TestFilteredCollectionBackend::loadRecord_matching_returnsRecord()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Work"})));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto rec = v.loadRecord("r1");
    QVERIFY(rec.has_value());
    QCOMPARE(rec->id, QStringLiteral("r1"));
}

void TestFilteredCollectionBackend::loadRecord_nonMatching_returnsNullopt()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Personal"})));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const auto rec = v.loadRecord("r1");
    QVERIFY(!rec.has_value());
}

void TestFilteredCollectionBackend::loadRecord_unknownId_returnsNullopt()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QVERIFY(!v.loadRecord("nope").has_value());
}

void TestFilteredCollectionBackend::createRecord_contains_appendsFilterValueIfAbsent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    BackendRecord r = makeJsonRecord("r1", withCategories({"Personal"}));
    const QString id = v.createRecord("v1", r);
    QCOMPARE(id, QStringLiteral("r1"));
    const auto written = QJsonDocument::fromJson(parent.lastWritten().data).object();
    const auto cats = written.value("categories").toArray();
    QCOMPARE(cats.size(), 2);
    QCOMPARE(cats.at(0).toString(), QStringLiteral("Personal"));
    QCOMPARE(cats.at(1).toString(), QStringLiteral("Work"));
}

void TestFilteredCollectionBackend::createRecord_contains_noDuplicateIfAlreadyPresent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    v.createRecord("v1", makeJsonRecord("r1", withCategories({"Work", "Personal"})));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.size(), 2);  // still 2, no dup
}

void TestFilteredCollectionBackend::createRecord_contains_preservesExistingOrder()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    v.createRecord("v1", makeJsonRecord("r1",
        withCategories({"Important", "Work", "Personal"})));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.at(0).toString(), QStringLiteral("Important"));
    QCOMPARE(cats.at(1).toString(), QStringLiteral("Work"));
    QCOMPARE(cats.at(2).toString(), QStringLiteral("Personal"));
}

void TestFilteredCollectionBackend::createRecord_contains_preservesOtherCategoryValues()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    v.createRecord("v1", makeJsonRecord("r1", withCategories({"Important"})));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.size(), 2);
    QStringList values;
    for (const auto& val : cats) values.append(val.toString());
    QVERIFY(values.contains("Important"));
    QVERIFY(values.contains("Work"));
}

void TestFilteredCollectionBackend::createRecord_contains_payloadHasNoCategoriesField_addsArrayWithFilterValue()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    QJsonObject noCats;
    noCats.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    v.createRecord("v1", makeJsonRecord("r1", noCats));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.size(), 1);
    QCOMPARE(cats.first().toString(), QStringLiteral("Work"));
}

void TestFilteredCollectionBackend::createRecord_equals_alwaysOverwritesFilterProperty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"status"},
                                              RecordFilter::Op::Equals,
                                              QStringLiteral("Done") });
    QJsonObject inObj;
    inObj.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    inObj.insert(QStringLiteral("status"), QStringLiteral("InProgress"));
    v.createRecord("v1", makeJsonRecord("r1", inObj));
    const auto written = QJsonDocument::fromJson(parent.lastWritten().data).object();
    QCOMPARE(written.value("status").toString(), QStringLiteral("Done"));
}

void TestFilteredCollectionBackend::updateRecord_contains_stampsAndUpdatesParent()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    parent.setRecord(makeJsonRecord("r1", withCategories({"Personal"})));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    BackendRecord r = makeJsonRecord("r1", withCategories({"Family"}));
    QVERIFY(v.updateRecord(r));
    const auto cats = QJsonDocument::fromJson(parent.lastWritten().data)
                          .object().value("categories").toArray();
    QCOMPARE(cats.size(), 2);  // tighten: stamping must NOT duplicate or drop entries
    QStringList values;
    for (const auto& val : cats) values.append(val.toString());
    QVERIFY(values.contains("Family"));
    QVERIFY(values.contains("Work"));
}

void TestFilteredCollectionBackend::updateRecord_equals_overwritesFilterProperty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    QJsonObject seed;
    seed.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    seed.insert(QStringLiteral("status"), QStringLiteral("Done"));
    parent.setRecord(makeJsonRecord("r1", seed));
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"status"},
                                              RecordFilter::Op::Equals,
                                              QStringLiteral("Done") });
    QJsonObject newObj;
    newObj.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    newObj.insert(QStringLiteral("status"), QStringLiteral("InProgress"));
    QVERIFY(v.updateRecord(makeJsonRecord("r1", newObj)));
    const auto written = QJsonDocument::fromJson(parent.lastWritten().data).object();
    QCOMPARE(written.value("status").toString(), QStringLiteral("Done"));
}

void TestFilteredCollectionBackend::createRecord_unknownCollectionId_returnsEmpty()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    const QString id = v.createRecord("not-v1",
        makeJsonRecord("r1", withCategories({"Work"})));
    QVERIFY(id.isEmpty());
}

void TestFilteredCollectionBackend::createRecord_nonJsonPayload_passesThroughUnchanged()
{
    FakeParentBackend parent("p1", "cal-1", kCalendarCanonShape);
    FilteredCollectionBackend v(&parent, "cal-1", "v1",
                                RecordFilter{ PropertyId{"categories"},
                                              RecordFilter::Op::Contains,
                                              QStringLiteral("Work") });
    BackendRecord r;
    r.id = QStringLiteral("r1");
    r.data = QByteArray("not json at all");
    const QString id = v.createRecord("v1", r);
    QCOMPARE(id, QStringLiteral("r1"));
    QCOMPARE(parent.lastWritten().data, QByteArray("not json at all"));
}

QTEST_MAIN(TestFilteredCollectionBackend)
#include "tst_filtered_collection_backend.moc"
