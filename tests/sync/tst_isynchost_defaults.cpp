// Plan 8 step 1 — pins for the new ISyncHost registry-backed defaults.
//
// The RFC (docs/2026-06-10-plan8-isynchost-runsyncfuture-consumer-wave-rfc.md,
// PlanStan-acked) makes backendById()/backends() non-pure: a host that does
// NOT override them gets BackendRegistry-backed defaults with a
// dynamic_cast<SyncBackend*> — so a non-calendar (base-only) instance is a
// clean nullptr instead of the unchecked-static_cast UB the old pure
// interface forced on every implementor. These tests pin:
//
//   - no registry injected  -> safe emptiness (nullptr / {})
//   - calendar-typed backend (MockBackend)    -> found
//   - base-only backend (RawFilesBackend)     -> clean nullptr / omitted
//   - nullptr re-injection  -> back to emptiness

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "backendregistry.h"
#include "isynchost.h"
#include "mockbackend.h"
#include "rawfilesbackend.h"
#include "syncbackend.h"

using namespace Kalburator::Sync;
using Kalburator::Sinks::RawFilesBackend;

namespace {

// A host that overrides ONLY the remaining pure virtual — everything else
// runs on the new defaults. This not compiling would itself be the
// source-compatibility regression.
class DefaultsOnlyHost : public ISyncHost
{
public:
    ISyncConfigStore* configStore() override { return nullptr; }
};

} // namespace

class TstISyncHostDefaults : public QObject
{
    Q_OBJECT

private slots:
    void defaults_without_registry_are_empty();
    void backendById_finds_calendar_backend_via_registry();
    void backendById_base_only_backend_is_clean_nullptr();
    void backends_walks_registry_omitting_non_calendar();
    void setBackendRegistry_nullptr_resets_to_empty();
};

void TstISyncHostDefaults::defaults_without_registry_are_empty()
{
    DefaultsOnlyHost host;
    QCOMPARE(host.backendById(QStringLiteral("anything")), nullptr);
    QVERIFY(host.backends().isEmpty());
}

void TstISyncHostDefaults::backendById_finds_calendar_backend_via_registry()
{
    BackendRegistry registry;
    MockBackend mock(QStringLiteral("mock-1"));
    registry.registerBackendInstance(QStringLiteral("mock-1"), &mock);

    DefaultsOnlyHost host;
    host.setBackendRegistry(&registry);

    SyncBackend *found = host.backendById(QStringLiteral("mock-1"));
    QCOMPARE(found, static_cast<SyncBackend*>(&mock));
    QCOMPARE(host.backendById(QStringLiteral("unknown-id")), nullptr);
}

void TstISyncHostDefaults::backendById_base_only_backend_is_clean_nullptr()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BackendRegistry registry;
    RawFilesBackend baseOnly(dir.path());
    registry.registerBackendInstance(QStringLiteral("base-1"), &baseOnly);

    DefaultsOnlyHost host;
    host.setBackendRegistry(&registry);

    // The retired hazard: the old pure interface forced hosts to bridge the
    // neutral registry with an unchecked static_cast — fetching a base-only
    // backend that way was UB. The default's dynamic_cast misses cleanly.
    QCOMPARE(host.backendById(QStringLiteral("base-1")), nullptr);
}

void TstISyncHostDefaults::backends_walks_registry_omitting_non_calendar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BackendRegistry registry;
    MockBackend mock(QStringLiteral("mock-1"));
    RawFilesBackend baseOnly(dir.path());
    registry.registerBackendInstance(QStringLiteral("mock-1"), &mock);
    registry.registerBackendInstance(QStringLiteral("base-1"), &baseOnly);

    DefaultsOnlyHost host;
    host.setBackendRegistry(&registry);

    const QHash<QString, SyncBackend*> all = host.backends();
    QCOMPARE(all.size(), 1);
    QVERIFY(all.contains(QStringLiteral("mock-1")));
    QCOMPARE(all.value(QStringLiteral("mock-1")), static_cast<SyncBackend*>(&mock));
    QVERIFY(!all.contains(QStringLiteral("base-1")));  // omitted, not nullptr
}

void TstISyncHostDefaults::setBackendRegistry_nullptr_resets_to_empty()
{
    BackendRegistry registry;
    MockBackend mock(QStringLiteral("mock-1"));
    registry.registerBackendInstance(QStringLiteral("mock-1"), &mock);

    DefaultsOnlyHost host;
    host.setBackendRegistry(&registry);
    QVERIFY(host.backendById(QStringLiteral("mock-1")) != nullptr);

    host.setBackendRegistry(nullptr);
    QCOMPARE(host.backendById(QStringLiteral("mock-1")), nullptr);
    QVERIFY(host.backends().isEmpty());
}

QTEST_GUILESS_MAIN(TstISyncHostDefaults)
#include "tst_isynchost_defaults.moc"
