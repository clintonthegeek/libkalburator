// tst_record_identity.cpp
//
// VP.c-step-1a (vtodo-parity) — detached-exception blob-pipeline record
// identity (Kalburator::Sync::composeRecordIdentity / decomposeRecordIdentity /
// isExceptionRecordId). Pure QString/QDateTime helper; tested once here,
// independent of any particular caller.

#include <QTest>
#include <QTimeZone>

#include "recordidentity.h"

using Kalburator::Sync::composeRecordIdentity;
using Kalburator::Sync::decomposeRecordIdentity;
using Kalburator::Sync::isExceptionRecordId;

namespace {

const char16_t kSep = u'\x01';

QDateTime utcDateTime(const QString &iso)
{
    return QDateTime::fromString(iso, Qt::ISODate);
}

} // namespace

class TestRecordIdentity : public QObject {
    Q_OBJECT
private slots:

    void bareUidPassesThrough()
    {
        const QString uid = QStringLiteral("master-uid-1");
        QCOMPARE(composeRecordIdentity(uid, QDateTime{}), uid);

        const auto d = decomposeRecordIdentity(uid);
        QCOMPARE(d.uid, uid);
        QVERIFY2(!d.recurrenceId.isValid(),
                 "bare uid must decompose with an invalid recurrenceId");
        QVERIFY2(!isExceptionRecordId(uid), "bare uid is not an exception id");
    }

    void composeUsesUtcIsoTimestampForm()
    {
        const QDateTime recId = utcDateTime(QStringLiteral("2026-06-02T09:00:00Z"));
        const QString id = composeRecordIdentity(QStringLiteral("uid-1"), recId);
        QCOMPARE(id,
                 QStringLiteral("uid-1") + QChar(kSep)
                     + QStringLiteral("2026-06-02T09:00:00Z"));
        QVERIFY2(isExceptionRecordId(id), "composite id must be flagged as exception");
    }

    void roundTripIsStable()
    {
        const QString uid = QStringLiteral("series-abc");
        const QDateTime recId = utcDateTime(QStringLiteral("2026-06-02T09:00:00Z"));
        const QString id = composeRecordIdentity(uid, recId);

        const auto d = decomposeRecordIdentity(id);
        QCOMPARE(d.uid, uid);
        QCOMPARE(d.recurrenceId.isValid(), true);
        QCOMPARE(d.recurrenceId.toUTC(), recId.toUTC());

        // Re-composition must reproduce the exact same bytes.
        QCOMPARE(composeRecordIdentity(d.uid, d.recurrenceId), id);
    }

    void sameInstantInDifferentZonesComposesSameId()
    {
        const QDateTime utc   = utcDateTime(QStringLiteral("2026-06-02T13:00:00Z"));
        // 09:00 EDT on 2026-06-02 == 13:00 UTC.
        const QDateTime ny    = utc.toTimeZone(QTimeZone(QByteArrayLiteral("America/New_York")));
        const QDateTime tokyo = utc.toTimeZone(QTimeZone(QByteArrayLiteral("Asia/Tokyo")));

        QVERIFY2(ny.isValid() && tokyo.isValid(), "zone conversions must be valid");

        const QString idUtc   = composeRecordIdentity(QStringLiteral("u"), utc);
        const QString idNy    = composeRecordIdentity(QStringLiteral("u"), ny);
        const QString idTokyo = composeRecordIdentity(QStringLiteral("u"), tokyo);

        QCOMPARE(idNy, idUtc);
        QCOMPARE(idTokyo, idUtc);
    }

    void malformedInputFailsLoud()
    {
        // Separator present but the timestamp half is garbage → no crash,
        // empty uid + invalid recurrenceId (NOT a silent master passthrough).
        {
            const auto d = decomposeRecordIdentity(
                QStringLiteral("uid-1") + QChar(kSep) + QStringLiteral("not-a-date"));
            QVERIFY2(d.uid.isEmpty() && !d.recurrenceId.isValid(),
                     "garbage timestamp half must fail loudly (empty result)");
        }
        // Empty uid half → malformed.
        {
            const auto d = decomposeRecordIdentity(
                QString(QChar(kSep)) + QStringLiteral("2026-06-02T09:00:00Z"));
            QVERIFY2(d.uid.isEmpty() && !d.recurrenceId.isValid(),
                     "empty uid half must fail loudly (empty result)");
        }
        // Separator but nothing after it → malformed.
        {
            const auto d = decomposeRecordIdentity(QStringLiteral("uid-1") + QChar(kSep));
            QVERIFY2(d.uid.isEmpty() && !d.recurrenceId.isValid(),
                     "missing timestamp half must fail loudly (empty result)");
        }
        // Empty input → empty/invalid, no crash.
        {
            const auto d = decomposeRecordIdentity(QString());
            QVERIFY(d.uid.isEmpty() && !d.recurrenceId.isValid());
            QVERIFY(!isExceptionRecordId(QString()));
        }
    }

    void separatorCannotCollideWithLegalUids()
    {
        // RFC 5545 TEXT (UID's value type) excludes CTLs — \x01 can never
        // appear in a legal UID or in an ISO-8601 timestamp. Pin that the
        // separator is what makes an id an exception id and nothing else does.
        const QString legalUid = QStringLiteral("0123456789abcdef@example.com-_.~:/?#[]@!$&'()*+,;=");
        QVERIFY2(!legalUid.contains(QChar(kSep)),
                 "test sanity: fixture uid must not contain the separator");
        QVERIFY(!isExceptionRecordId(legalUid));

        // A uid containing a literal ISO-looking tail but no separator stays
        // a master.
        const auto tricky = QStringLiteral("uid-2026-06-02T09:00:00Z");
        QVERIFY(!isExceptionRecordId(tricky));
        const auto d = decomposeRecordIdentity(tricky);
        QCOMPARE(d.uid, tricky);
        QVERIFY(!d.recurrenceId.isValid());
    }
};

QTEST_GUILESS_MAIN(TestRecordIdentity)
#include "tst_record_identity.moc"
