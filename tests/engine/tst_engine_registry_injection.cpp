// Proves per-engine registry isolation: two ShapeRegistries bundles
// configured differently route independently. Impossible under the old
// process-global singleton (there was only one set of edges). See design §8.
#include <QtTest>

#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"
#include "shape.h"
#include "shaperegistries.h"

using namespace Kalburator;
using namespace Kalburator::Shape;

class TestEngineRegistryInjection : public QObject
{
    Q_OBJECT

private slots:
    // Bundle A has the stock contacts vcard3<->vcard4 edge; bundle B is
    // empty. The same compile() the worker performs succeeds on A and
    // fails on B — the two bundles do not share state.
    void distinctBundlesRouteIndependently()
    {
        Sync::BackendRegistry pmRegA;
        ShapeRegistries shapeA;
        PluginManager pmA(&pmRegA, shapeA);   // injecting ctor (Task 4)
        registerStockPlugins(pmA);

        ShapeRegistries shapeB;               // never populated

        const Kalburator::Shape::Shape v3{ DomainId{"contacts"}, EncodingId{"vcard3"} };
        const Kalburator::Shape::Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };

        QVERIFY(shapeA.transformation.compile(v3, v4).has_value());
        QVERIFY(!shapeB.transformation.compile(v3, v4).has_value());
    }
};

QTEST_MAIN(TestEngineRegistryInjection)
#include "tst_engine_registry_injection.moc"
