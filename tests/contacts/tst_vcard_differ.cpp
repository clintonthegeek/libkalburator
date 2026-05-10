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
using Kalburator::Contacts::RecordDifferVCard;
using Kalburator::Contacts::RecordMergerVCard;
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
        RecordDifferVCard differ;
        const auto data = makeVCard(QStringLiteral("u1"), QStringLiteral("Alice"));
        QVERIFY(differ.diff(makeRecord(data), makeRecord(data)).isEmpty());
        QVERIFY(differ.equal(makeRecord(data), makeRecord(data)));
    }

    void changedNameIsDetected()
    {
        RecordDifferVCard differ;
        const auto a = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Alice")));
        const auto b = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Alice Smith")));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"fn"}));
        QVERIFY(!differ.equal(a, b));
    }

    void changedOrgIsDetected()
    {
        RecordDifferVCard differ;
        const auto a = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Bob"), QStringLiteral("Acme")));
        const auto b = makeRecord(makeVCard(QStringLiteral("u1"), QStringLiteral("Bob"), QStringLiteral("BigCo")));
        QVERIFY(differ.diff(a, b).contains(PropertyId{"org"}));
    }

    void diffDetectsGenderChange()
    {
        RecordDifferVCard differ;
        CanonicalRecord src;
        src.shape = kShape;
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Bob\r\nGENDER:M\r\nEND:VCARD\r\n";
        CanonicalRecord base = src;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Bob\r\nGENDER:F\r\nEND:VCARD\r\n";

        const auto changed = differ.diff(src, base);
        QVERIFY(changed.contains(PropertyId{"gender"}));
    }

    void diffDetectsLangChange()
    {
        RecordDifferVCard differ;
        CanonicalRecord src;
        src.shape = kShape;
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Bob\r\nLANG:en\r\nEND:VCARD\r\n";
        CanonicalRecord base = src;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Bob\r\nLANG:fr\r\nEND:VCARD\r\n";

        const auto changed = differ.diff(src, base);
        QVERIFY(changed.contains(PropertyId{"lang"}));
    }

    void diffDetectsAnniversaryChange()
    {
        RecordDifferVCard differ;
        CanonicalRecord src;
        src.shape = kShape;
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Bob\r\nANNIVERSARY:20100615\r\nEND:VCARD\r\n";
        CanonicalRecord base = src;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Bob\r\nANNIVERSARY:20200716\r\nEND:VCARD\r\n";

        const auto changed = differ.diff(src, base);
        QVERIFY(changed.contains(PropertyId{"anniversary"}));
    }

    void diffDetectsKindChange()
    {
        RecordDifferVCard differ;
        CanonicalRecord src;
        src.shape = kShape;
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Acme\r\nKIND:org\r\nEND:VCARD\r\n";
        CanonicalRecord base = src;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Acme\r\nKIND:individual\r\nEND:VCARD\r\n";

        const auto changed = differ.diff(src, base);
        QVERIFY(changed.contains(PropertyId{"kind"}));
    }

    // These tests exercise the merger property-pickers. The interesting case
    // is target-only-changed: the merger starts from `src`, so it must copy
    // tgt's v4 property into `merged`. Without a picker for the field, the
    // change is silently dropped (regression we're guarding against).
    void mergerResolvesGenderConflict()
    {
        CanonicalRecord base, src, tgt;
        base.shape = src.shape = tgt.shape = kShape;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nGENDER:M\r\nEND:VCARD\r\n";
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nGENDER:M\r\nEND:VCARD\r\n"; // src unchanged
        tgt.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nGENDER:F\r\nEND:VCARD\r\n"; // tgt changed

        RecordMergerVCard merger;
        ConflictPolicy policy;
        policy.autoResolve = AutoResolveStrategy::SourceAlwaysWins;

        const auto out = merger.merge(src, tgt, base, policy);
        QVERIFY2(out.data.contains("GENDER:F"),
                 qPrintable("merger should have propagated target's GENDER:F; got:\n"
                            + QString::fromUtf8(out.data)));
    }

    void mergerResolvesAnniversaryConflict()
    {
        CanonicalRecord base, src, tgt;
        base.shape = src.shape = tgt.shape = kShape;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nANNIVERSARY:20100615\r\nEND:VCARD\r\n";
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nANNIVERSARY:20100615\r\nEND:VCARD\r\n"; // src unchanged
        tgt.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nANNIVERSARY:20200716\r\nEND:VCARD\r\n"; // tgt changed

        RecordMergerVCard merger;
        ConflictPolicy policy;
        policy.autoResolve = AutoResolveStrategy::SourceAlwaysWins;

        const auto out = merger.merge(src, tgt, base, policy);
        QVERIFY2(out.data.contains("20200716"),
                 qPrintable("merger should have propagated target's anniversary; got:\n"
                            + QString::fromUtf8(out.data)));
    }

    void mergerResolvesKindConflict()
    {
        CanonicalRecord base, src, tgt;
        base.shape = src.shape = tgt.shape = kShape;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Acme\r\nKIND:individual\r\nEND:VCARD\r\n";
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Acme\r\nKIND:individual\r\nEND:VCARD\r\n"; // src unchanged
        tgt.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Acme\r\nKIND:org\r\nEND:VCARD\r\n";        // tgt changed

        RecordMergerVCard merger;
        ConflictPolicy policy;
        policy.autoResolve = AutoResolveStrategy::SourceAlwaysWins;

        const auto out = merger.merge(src, tgt, base, policy);
        QVERIFY2(out.data.contains("KIND:org"),
                 qPrintable("merger should have propagated target's KIND:org; got:\n"
                            + QString::fromUtf8(out.data)));
    }

    void mergerResolvesLangConflict()
    {
        CanonicalRecord base, src, tgt;
        base.shape = src.shape = tgt.shape = kShape;
        base.data = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nLANG:en\r\nEND:VCARD\r\n";
        src.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nLANG:en\r\nEND:VCARD\r\n"; // src unchanged
        tgt.data  = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:C\r\nLANG:fr\r\nEND:VCARD\r\n"; // tgt changed

        RecordMergerVCard merger;
        ConflictPolicy policy;
        policy.autoResolve = AutoResolveStrategy::SourceAlwaysWins;

        const auto out = merger.merge(src, tgt, base, policy);
        QVERIFY2(out.data.contains("LANG:fr"),
                 qPrintable("merger should have propagated target's LANG:fr; got:\n"
                            + QString::fromUtf8(out.data)));
    }

    void mergerEmitsV4_0()
    {
        CanonicalRecord src;
        src.shape = kShape;
        src.data  = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Alice\r\nEND:VCARD\r\n";
        CanonicalRecord tgt = src;
        CanonicalRecord base;
        base.shape = src.shape;

        RecordMergerVCard merger;
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
