// tst_akonadi_scoped_collection.cpp
//
// Fix A (2026-06-12 Akonadi scoped-backend handoff): a per-collection scoped
// AkonadiBackend / AkonadiContactsBackend (created via createBackend with
// "akonadiCollectionId") never runs loadCalendars(), so m_collections stayed
// empty and EVERY fetch/read/write fast-failed with "Unknown calendar" — the
// scoped read path never actually worked.
//
// ensureScopedCollection() now seeds the one scoped collection lazily from its
// id. This unit-level guard pins the defect WITHOUT needing seeded data or a
// running resource: a scoped backend's fetchItems() for its OWN scoped id must
// NOT return an immediately-Failed op with "Unknown calendar". (The full
// reads-N-incidences integration test runs on-device against a live Akonadi —
// acceptance criterion 2 in the handoff.)

#include <QtTest>

#include "akonadibackend.h"
#include "akonadicontactsbackend.h"
#include "syncoperation.h"

using namespace Kalburator::Sync;

namespace {
// AkonadiProvider emits "akonadi-<id>" for EVERY collection regardless of type
// (src/sync/akonadiprovider.cpp ~137) — there is no per-type prefix. These ids
// MUST be the scheme the provider actually produces (the id WP persists into a
// route), not a backend-self-invented scheme. The id value is otherwise
// irrelevant to these guards — we assert only the SYNCHRONOUS return state,
// which for the buggy code is Failed("Unknown …") before any server I/O.
constexpr auto kCalendarScopedId = "akonadi-1";
constexpr auto kContactsScopedId = "akonadi-1";   // provider scheme, NOT "akonadi-contacts-1"
}

class TstAkonadiScopedCollection : public QObject
{
    Q_OBJECT
private slots:
    void calendar_scopedFetch_doesNotFastFailUnknownCalendar();
    void contacts_scopedFetch_doesNotFastFailUnknownCollection();
    void contacts_resolvesProviderEmittedIdScheme();
};

void TstAkonadiScopedCollection::calendar_scopedFetch_doesNotFastFailUnknownCalendar()
{
    QVariantMap cfg;
    cfg.insert(QStringLiteral("akonadiCollectionId"),
               QString::fromLatin1(kCalendarScopedId));
    std::unique_ptr<AkonadiBackend> backend(
        static_cast<AkonadiBackend *>(AkonadiBackend::create(cfg, nullptr)));
    QVERIFY(backend);

    FetchOperation *op = backend->fetchItems(QString::fromLatin1(kCalendarScopedId));
    QVERIFY(op);

    // Pre-fix: the scoped collection is unresolved, so fetchItems returns an
    // immediately-Failed op carrying "Unknown calendar". Post-fix:
    // ensureScopedCollection seeds the collection and the op reaches Running
    // (an ItemFetchJob is started) — never a synchronous "Unknown calendar".
    QVERIFY2(!(op->state() == SyncOperation::Failed
               && op->errorString().contains(QStringLiteral("Unknown calendar"))),
             qPrintable(QStringLiteral("scoped fetchItems fast-failed: state=%1 err=%2")
                            .arg(int(op->state())).arg(op->errorString())));
}

void TstAkonadiScopedCollection::contacts_scopedFetch_doesNotFastFailUnknownCollection()
{
    QVariantMap cfg;
    cfg.insert(QStringLiteral("akonadiCollectionId"),
               QString::fromLatin1(kContactsScopedId));
    std::unique_ptr<AkonadiContactsBackend> backend(
        static_cast<AkonadiContactsBackend *>(
            AkonadiContactsBackend::create(cfg, nullptr)));
    QVERIFY(backend);

    FetchOperation *op = backend->fetchItems(QString::fromLatin1(kContactsScopedId));
    QVERIFY(op);

    QVERIFY2(!(op->state() == SyncOperation::Failed
               && op->errorString().contains(QStringLiteral("Unknown collection"))),
             qPrintable(QStringLiteral("scoped contacts fetchItems fast-failed: state=%1 err=%2")
                            .arg(int(op->state())).arg(op->errorString())));
}

void TstAkonadiScopedCollection::contacts_resolvesProviderEmittedIdScheme()
{
    // Pin provider<->backend id-scheme agreement. Build the collection id EXACTLY
    // the way AkonadiProvider::collectionFetchResult does for a contacts
    // collection (src/sync/akonadiprovider.cpp ~137: "akonadi-%1".arg(col.id()))
    // — there is no per-type prefix — and feed it through the same createBackend
    // path the provider uses. The scoped contacts backend must resolve it, i.e.
    // fetchItems must not fast-fail "Unknown collection". A backend that only
    // parsed "akonadi-contacts-<id>" (its old self-documented scheme, which
    // nothing produces) fails this.
    const QString providerEmittedId = QStringLiteral("akonadi-%1").arg(184);

    QVariantMap cfg;
    cfg.insert(QStringLiteral("akonadiCollectionId"), providerEmittedId);
    std::unique_ptr<AkonadiContactsBackend> backend(
        static_cast<AkonadiContactsBackend *>(
            AkonadiContactsBackend::create(cfg, nullptr)));
    QVERIFY(backend);

    FetchOperation *op = backend->fetchItems(providerEmittedId);
    QVERIFY(op);

    QVERIFY2(!(op->state() == SyncOperation::Failed
               && op->errorString().contains(QStringLiteral("Unknown collection"))),
             qPrintable(QStringLiteral("contacts backend did not resolve provider id %1: state=%2 err=%3")
                            .arg(providerEmittedId).arg(int(op->state())).arg(op->errorString())));
}

QTEST_GUILESS_MAIN(TstAkonadiScopedCollection)
#include "tst_akonadi_scoped_collection.moc"
