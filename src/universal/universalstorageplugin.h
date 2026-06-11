#ifndef KALBURATOR_UNIVERSAL_STORAGEPLUGIN_H
#define KALBURATOR_UNIVERSAL_STORAGEPLUGIN_H

// LAYER ROLE (Plan 9) — `universal/` holds the concrete, domain-neutral "sink"
// backends (GenericSqliteBackend, RawFilesBackend, MarkdownFilesBackend,
// FilteredCollectionBackend) in namespace `Kalburator::Sinks` — concrete
// implementations of the neutral `sync/` contracts, used by WildPalms' hub and
// generic storage. NOTE the dir (`universal/`) ↔ namespace (`Sinks`) mismatch:
// a vocabulary wart deferred to Plan 10 (renaming `Kalburator::Sinks` is a
// downstream wave — 11 WildPalms `Kalburator::Sinks` sites). See FINDINGS.

#include "plugin.h"

namespace Kalburator {

class UniversalStoragePlugin : public Plugin {
public:
    QList<std::shared_ptr<Sync::BackendContribution>> backendContributions() const override;
};

} // namespace Kalburator

#endif
