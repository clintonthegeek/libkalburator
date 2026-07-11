#include <QTest>
#include <QSignalSpy>
#include "akonadiprovider.h"
#include "akonadibackendcontribution.h"
#include "akonadicontactsbackend.h"

using namespace Kalburator::Sync;

namespace {

// Phase 1: AkonadiProvider emits one spec per collection permanently
// (domainId == collection id — no Task 2 collapse for Akonadi), so a
// known-collection lookup against createBackends() is equivalent to the
// old createBackend(collectionId).
std::unique_ptr<IBlobBackend>
backendForCollection(IProvider &provider, const QString &collectionId)
{
    auto specs = provider.createBackends();
    for (auto &spec : specs) {
        if (spec.domainId == collectionId) return std::move(spec.backend);
    }
    return nullptr;
}

} // anonymous namespace

class TstAkonadiProvider : public QObject {
    Q_OBJECT
private slots:
    void identity_isStable() {
        AkonadiProvider p;
        QVERIFY(!p.id().isEmpty());
        QCOMPARE(p.kind(), QStringLiteral("akonadi"));
        QVERIFY(!p.displayName().isEmpty());
        QCOMPARE(p.isConnected(), false);
        QVERIFY(p.collections().isEmpty());
    }

    void createBackends_beforeConnect_returnsEmpty() {
        AkonadiProvider p;
        QVERIFY(p.createBackends().empty());
    }

    void contribution_exposes_akonadiBackendType() {
        Kalburator::Sync::AkonadiBackendContribution c;
        QCOMPARE(c.backendType(), QStringLiteral("akonadi"));
        QVERIFY(!c.nativeShapes().isEmpty());
        auto p = c.createProvider(nullptr);
        QVERIFY(p);
        QCOMPARE(p->kind(), QStringLiteral("akonadi"));
    }

    void connect_live_enumeratesCollections() {
        if (qgetenv("KALBURATOR_AKONADI_LIVE_TEST").isEmpty()) {
            QSKIP("Set KALBURATOR_AKONADI_LIVE_TEST=1 to run against live Akonadi.");
        }
        AkonadiProvider p;
        auto f = p.connect();
        QVERIFY(QTest::qWaitFor([&]{ return f.isFinished(); }, 10000));
        QVERIFY(f.result());
        QVERIFY(p.isConnected());
        Q_UNUSED(p.collections()); // must not crash
    }

    void connect_offline_resolvesWithoutHanging() {
        // Without a live Akonadi, connect() must finish (resolve false or true)
        // within 5 seconds and not hang. Only validates timing.
        AkonadiProvider p;
        QSignalSpy errSpy(&p, &IProvider::error);
        auto f = p.connect();
        // Note: if Akonadi IS running, this resolves true — that's fine.
        QVERIFY(QTest::qWaitFor([&]{ return f.isFinished(); }, 5000));
    }

    void createBackend_calendarCollection_returnsBackend() {
        if (qgetenv("KALBURATOR_AKONADI_LIVE_TEST").isEmpty()) {
            QSKIP("Set KALBURATOR_AKONADI_LIVE_TEST=1.");
        }
        AkonadiProvider p;
        auto f = p.connect();
        QVERIFY(QTest::qWaitFor([&]{ return f.isFinished(); }, 10000));
        QVERIFY(f.result());

        QString calCollId;
        for (const auto &c : p.collections()) {
            if (c.type == QStringLiteral("calendar")) {
                calCollId = c.id;
                break;
            }
        }
        if (calCollId.isEmpty()) QSKIP("No calendar collection in Akonadi.");

        auto backend = backendForCollection(p, calCollId);
        QVERIFY(backend != nullptr);
    }

    void createBackend_contactsCollection_returnsContactsBackend() {
        if (qgetenv("KALBURATOR_AKONADI_LIVE_TEST").isEmpty()) {
            QSKIP("Set KALBURATOR_AKONADI_LIVE_TEST=1.");
        }
        AkonadiProvider p;
        auto f = p.connect();
        QVERIFY(QTest::qWaitFor([&]{ return f.isFinished(); }, 10000));
        QVERIFY(f.result());

        QString contactsCollId;
        for (const auto &c : p.collections()) {
            if (c.type == QStringLiteral("contacts")) {
                contactsCollId = c.id;
                break;
            }
        }
        if (contactsCollId.isEmpty()) QSKIP("No contacts collection in Akonadi.");

        auto backend = backendForCollection(p, contactsCollId);
        QVERIFY(backend != nullptr);
        auto *contactsBackend = dynamic_cast<AkonadiContactsBackend *>(backend.get());
        QVERIFY(contactsBackend != nullptr);
        QCOMPARE(contactsBackend->backendType(), QStringLiteral("akonadi-contacts"));
    }
};

QTEST_MAIN(TstAkonadiProvider)
#include "tst_akonadiprovider.moc"
