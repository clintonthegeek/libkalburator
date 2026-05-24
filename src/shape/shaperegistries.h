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
/// New code MUST inject a ShapeRegistries& rather than call
/// TransformationRegistry::instance() etc. The instance() accessors and
/// defaultShapeRegistries() are transitional Ambient-Context scaffolding
/// (Seemann) kept only so downstream consumers compile during migration;
/// see FINDINGS O7 for their scheduled removal.
struct ShapeRegistries {
    TransformationRegistry   transformation;
    DomainRegistry           domain;
    DomainOperationsRegistry operations;
};

/// The process-global default bundle. TransformationRegistry::instance()
/// and friends delegate to its members so existing callers and the
/// no-bundle ctor overloads keep working. ANTI-PATTERN (Ambient Context):
/// do not reach for this in new code — inject a ShapeRegistries& instead.
ShapeRegistries &defaultShapeRegistries();

}  // namespace Kalburator::Shape
