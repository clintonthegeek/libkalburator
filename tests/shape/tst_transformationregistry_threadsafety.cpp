// Parallel-sync Task 16 — TransformationRegistry::compile() is called
// concurrently once SyncEngine runs a pool of workers (four times per
// mapping in dispatchSync, twice more in unifiedContinueAfterConflicts).
//
// compile() is const but freezes its source domain, mutating a mutable
// QSet<DomainId>. Concurrent insert can rehash and reallocate while another
// thread is walking buckets — memory corruption, not a lost update.
//
// Under a plain build this test catches gross corruption probabilistically.
// The RELIABLE detector is TSAN; see the task's TSAN step, which is the
// gate that actually proves the fix.

#include <QtTest/QtTest>
#include <QObject>
#include <QThread>
#include <QtConcurrent>
#include <QFuture>

#include "transformationregistry.h"
#include "propertycatalogue.h"
#include "pipeline.h"
#include "shape.h"
#include "transformationedge.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossKind;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::PropertyKind;
using Kalburator::Shape::Shape;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::TransformationRegistry;
using Kalburator::Shape::TransformationStage;

namespace {

class PrefixStage : public TransformationStage {
public:
    explicit PrefixStage(QByteArray p) : m_prefix(std::move(p)) {}
    QByteArray transform(const QByteArray& s) const override { return m_prefix + s; }
private:
    QByteArray m_prefix;
};

LossProfile lossyProfile(const QString& prop) {
    LossProfile p;
    p.affected.insert(PropertyId{prop}, LossKind::Dropped);
    return p;
}

PropertyCatalogue makeStubCatalogue() {
    PropertyCatalogue c;
    c.addProperty({ PropertyId{QStringLiteral("uid")}, PropertyKind::String, {}, false });
    return c;
}

}  // namespace

class TestTransformationRegistryThreadSafety : public QObject
{
    Q_OBJECT

private slots:
    void testConcurrentCompileIsSafe()
    {
        TransformationRegistry registry;
        seedRegistry(registry);

        // Eight threads hammering compile() on the same domain — every one
        // of them takes the freeze() branch, so every one writes the set.
        constexpr int kThreads = 8;
        constexpr int kIterations = 500;

        QList<QFuture<int>> futures;
        for (int t = 0; t < kThreads; ++t) {
            futures.append(QtConcurrent::run([&registry, this]() {
                int compiled = 0;
                for (int i = 0; i < kIterations; ++i) {
                    if (registry.compile(shapeA(), shapeB()).has_value())
                        ++compiled;
                }
                return compiled;
            }));
        }

        for (auto &f : futures) {
            f.waitForFinished();
            QCOMPARE(f.result(), kIterations);
        }

        // The observable postcondition: the domain is frozen exactly once
        // and consistently, whoever won the race.
        QVERIFY(registry.isFrozen(shapeA().domain));
    }

    void testConcurrentCompileAndIsFrozenIsSafe()
    {
        // Readers racing writers is the other half of the hazard: five of
        // the six m_frozenDomains accesses are reads.
        TransformationRegistry registry;
        seedRegistry(registry);

        QFuture<void> writer = QtConcurrent::run([&registry, this]() {
            for (int i = 0; i < 500; ++i)
                registry.compile(shapeA(), shapeB());
        });
        QFuture<void> reader = QtConcurrent::run([&registry, this]() {
            for (int i = 0; i < 500; ++i)
                (void)registry.isFrozen(shapeA().domain);
        });

        writer.waitForFinished();
        reader.waitForFinished();
        QVERIFY(registry.isFrozen(shapeA().domain));
    }

private:
    // Build the registry the way the existing tests/shape/ binaries do —
    // shapeA() is the domain's canonical shape, shapeB() a sibling native
    // shape reached from it by a single registered edge. compile(A, B)
    // then takes compileImpl's single-leg-from-canonical path: same
    // domain, from != to, neither isAny() — the freeze() branch at
    // transformationregistry.cpp:159-162.
    void seedRegistry(TransformationRegistry &registry)
    {
        registry.registerShape(shapeA(), makeStubCatalogue());
        registry.registerShape(shapeB(), makeStubCatalogue());
        registry.declareCanonical(shapeA().domain, shapeA());
        registry.registerEdge({ shapeA(), shapeB(), lossyProfile(QStringLiteral("attendees")),
                                 std::make_shared<PrefixStage>("B-") });

        // Prove the freeze branch is actually live before hammering it
        // concurrently — if this QVERIFY fails, the race test below proves
        // nothing. compile() re-inserts into m_frozenDomains on every call
        // regardless of prior state (see compile()'s unconditional freeze()
        // call on its non-identity branch), so leaving the domain frozen
        // here does not weaken the concurrent test that follows — every
        // thread's every compile() call still performs the write.
        auto warmup = registry.compile(shapeA(), shapeB());
        QVERIFY(warmup.has_value());
        QVERIFY(registry.isFrozen(shapeA().domain));
    }

    Shape shapeA() const { return { DomainId{"threadsafety"}, EncodingId{"a"} }; }
    Shape shapeB() const { return { DomainId{"threadsafety"}, EncodingId{"b"} }; }
};

QTEST_MAIN(TestTransformationRegistryThreadSafety)
#include "tst_transformationregistry_threadsafety.moc"
