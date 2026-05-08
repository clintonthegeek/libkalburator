#include <QTest>

#include "vcarddiffer.h"
#include "vcardmerger.h"
#include "canonicalrecord.h"
#include "conflictpolicy.h"

#include <KContacts/VCardConverter>
#include <KContacts/Addressee>

using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Contacts::IRecordDifferVCard;
using Kalburator::Contacts::IRecordMergerVCard;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::AutoResolveStrategy;

namespace {

const Shape kShape{ DomainId{"contacts"}, EncodingId{"vcard4"} };

QByteArray makeVCard(const QString &uid, const QString &fullName,
                     const QString &org = {})
{
    KContacts::Addressee addr;
    addr.setUid(uid);
    addr.setFormattedName(fullName);
    if (!org.isEmpty())
        addr.setOrganization(org);
    KContacts::VCardConverter conv;
    return conv.createVCard(addr, KContacts::VCardConverter::v3_0);
}

CanonicalRecord makeRecord(const QByteArray &data)
{
    CanonicalRecord rec;
    rec.shape    = kShape;
    rec.recordId = QStringLiteral("r1");
    rec.data     = data;
    return rec;
}

} // namespace

class TestVCardDiffer : public QObject {
    Q_OBJECT
private slots:
    void equalRecordsEmptyDiff()
    {
        IRecordDifferVCard differ;
        const auto data = makeVCard(QStringLiteral("u1"), QStringLiteral("Alice"));
        QVERIFY(differ.diff(makeRecord(data), makeRecord(data)).isEmpty());
        QVERIFY(differ.equal(makeRecord(data), makeRecord(data)));
    }

    void changedNameIsDetected()
    {
        IRecordDifferVCard differ;
        const auto a = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Alice")));
        const auto b = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Alice Smith")));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"fn"}));
        QVERIFY(!differ.equal(a, b));
    }

    void changedOrgIsDetected()
    {
        IRecordDifferVCard differ;
        const auto a = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Bob"), QStringLiteral("Acme")));
        const auto b = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Bob"), QStringLiteral("BigCo")));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"org"}));
    }

    void mergerEmitsV4_0()
    {
        CanonicalRecord src;
        src.shape = kShape;
        src.data  = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Alice\r\nEND:VCARD\r\n";
        CanonicalRecord tgt = src;
        CanonicalRecord base;
        base.shape = src.shape;

        IRecordMergerVCard merger;
        ConflictPolicy policy;
        policy.autoResolve = AutoResolveStrategy::SourceAlwaysWins;

        const auto out = merger.merge(src, tgt, base, policy);
        QVERIFY2(out.data.contains("VERSION:4.0"),
                 qPrintable("Expected VERSION:4.0 in merger output, got:\n"
                            + QString::fromUtf8(out.data)));
    }
};

QTEST_GUILESS_MAIN(TestVCardDiffer)
#include "tst_vcard_differ.moc"
