/// Tests for RecordFilter + FilteredCollectionBackend
/// (consumer RFC 2026-05-28).

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "recordfilter.h"

using Kalburator::Shape::PropertyId;
using Kalburator::Shape::RecordFilter;

namespace {

QByteArray canonJson(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QJsonObject withCategories(QStringList cats)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"), QStringLiteral("u1"));
    QJsonArray arr;
    for (const auto& c : cats) arr.append(c);
    obj.insert(QStringLiteral("categories"), arr);
    return obj;
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

QTEST_MAIN(TestFilteredCollectionBackend)
#include "tst_filtered_collection_backend.moc"
