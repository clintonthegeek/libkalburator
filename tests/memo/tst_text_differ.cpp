#include <QTest>

#include "textdiffer.h"
#include "canonicalrecord.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Memo::TextDiffer;

namespace {

const Shape kShape{ DomainId{"memo"}, EncodingId{"text"} };

QByteArray makeMemoData(const QString &body, const QStringList &cats = {})
{
    QJsonObject obj;
    obj[QStringLiteral("body")]         = body;
    obj[QStringLiteral("categories")]   = QJsonArray::fromStringList(cats);
    obj[QStringLiteral("lastModified")] = QStringLiteral("2026-01-01T00:00:00Z");
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

CanonicalRecord makeRecord(const QByteArray &data)
{
    CanonicalRecord rec;
    rec.shape    = kShape;
    rec.recordId = QStringLiteral("m1");
    rec.data     = data;
    return rec;
}

} // namespace

class TestTextDiffer : public QObject {
    Q_OBJECT
private slots:
    void identicalDataIsEqual()
    {
        TextDiffer differ;
        const auto data = makeMemoData(QStringLiteral("hello world"));
        QVERIFY(differ.diff(makeRecord(data), makeRecord(data)).isEmpty());
        QVERIFY(differ.equal(makeRecord(data), makeRecord(data)));
    }

    void changedBodyIsDetected()
    {
        TextDiffer differ;
        const auto a = makeRecord(makeMemoData(QStringLiteral("original")));
        const auto b = makeRecord(makeMemoData(QStringLiteral("updated")));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"body"}));
        QVERIFY(!differ.equal(a, b));
    }

    void changedCategoriesIsDetected()
    {
        TextDiffer differ;
        const auto a = makeRecord(makeMemoData(QStringLiteral("text"), { QStringLiteral("work") }));
        const auto b = makeRecord(makeMemoData(QStringLiteral("text"), { QStringLiteral("personal") }));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"categories"}));
    }

    void unchangedFieldsNotInDiff()
    {
        TextDiffer differ;
        const auto a = makeRecord(makeMemoData(QStringLiteral("same"), { QStringLiteral("cat") }));
        const auto b = makeRecord(makeMemoData(QStringLiteral("same"), { QStringLiteral("cat") }));
        QVERIFY(differ.diff(a, b).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTextDiffer)
#include "tst_text_differ.moc"
