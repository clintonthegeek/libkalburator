#pragma once

#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "transformationregistry.h"

namespace Kalburator::Shape {

/// The composition-root product for shape state: the three registries a
/// SyncEngine reads and a PluginManager writes, bundled so they can be
/// owned by value and injected by reference into both. This is the shape
/// equivalent of an OSGi BundleContext (design §8). One bundle per engine
/// gives that engine its own versioned spine (in `transformation`) and its
/// own domain/operations tables.
///
/// A ShapeRegistries is constructed at the composition root and injected
/// by reference; it is the sole construction site. There is deliberately
/// no process-global default and no `::instance()` accessor — the
/// Ambient-Context scaffolding that bridged the DI migration was removed
/// once downstream adopted the injecting ctors (FINDINGS O7, resolved).
struct ShapeRegistries {
    TransformationRegistry   transformation;
    DomainRegistry           domain;
    DomainOperationsRegistry operations;
};

}  // namespace Kalburator::Shape
