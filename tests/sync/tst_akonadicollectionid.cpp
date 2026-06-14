// tst_akonadicollectionid.cpp
//
// Pins the single source of truth for the Akonadi collection-id scheme shared by
// AkonadiProvider (producer) and the Akonadi calendar/contacts backends
// (consumers). Before this helper existed the scheme was duplicated three ways
// and the contacts backend drifted to "akonadi-contacts-<id>" while the provider
// emitted "akonadi-<id>" (2026-06-14 prefix-mismatch handoff). One round-trip
// helper makes the agreement structural.
//
// Pure QString/qlonglong — no Akonadi dependency — so this runs in the default
// (Akonadi-OFF) profile.

#include <QtTest>

#include "akonadicollectionid.h"

using namespace Kalburator::Sync;

class TstAkonadiCollectionId : public QObject
{
    Q_OBJECT
private slots:
    void toString_usesGenericAkonadiPrefix();
    void roundTrip_preservesId();
    void fromString_rejectsForeignSchemes();
    void fromString_rejectsPerTypePrefix();
};

void TstAkonadiCollectionId::toString_usesGenericAkonadiPrefix()
{
    // The provider's scheme: "akonadi-<id>", no per-type discriminator.
    QCOMPARE(akonadiCollectionIdToString(184), QStringLiteral("akonadi-184"));
    QCOMPARE(akonadiCollectionIdToString(0), QStringLiteral("akonadi-0"));
}

void TstAkonadiCollectionId::roundTrip_preservesId()
{
    for (qlonglong id : {0LL, 1LL, 54LL, 184LL, 9007199254740993LL}) {
        QCOMPARE(akonadiCollectionIdFromString(akonadiCollectionIdToString(id)), id);
    }
}

void TstAkonadiCollectionId::fromString_rejectsForeignSchemes()
{
    // Non-akonadi ids and malformed suffixes return -1.
    QCOMPARE(akonadiCollectionIdFromString(QStringLiteral("caldav-3")), -1LL);
    QCOMPARE(akonadiCollectionIdFromString(QStringLiteral("akonadi-")), -1LL);
    QCOMPARE(akonadiCollectionIdFromString(QStringLiteral("akonadi-xyz")), -1LL);
    QCOMPARE(akonadiCollectionIdFromString(QString()), -1LL);
}

void TstAkonadiCollectionId::fromString_rejectsPerTypePrefix()
{
    // The old self-invented contacts scheme must NOT parse as a bare numeric id
    // (its "contacts-184" suffix is non-numeric), which is exactly why a backend
    // keyed on "akonadi-contacts-" could never resolve provider ids.
    QCOMPARE(akonadiCollectionIdFromString(QStringLiteral("akonadi-contacts-184")), -1LL);
}

QTEST_GUILESS_MAIN(TstAkonadiCollectionId)
#include "tst_akonadicollectionid.moc"
