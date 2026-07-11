#ifndef KALBURATOR_SYNC_DAVSLUG_H
#define KALBURATOR_SYNC_DAVSLUG_H

#include <QString>

namespace Kalburator::Sync {

// PHASE2-TASK2.3 — shared slug helper for v2 ProviderBackendSpec.backendId.
//
// The v1 createBackend() path persisted backends keyed by a synthetic
// "<providerId>:<collectionId>" string; the v2 spec-driven registration
// pipeline (Phase 2.4+) needs a stable, server-derived suffix that
// survives a server-side rename of the display name. Every CalDAV /
// CardDAV / multi-protocol DAV provider participates, so the rule lives
// here instead of being duplicated inside each provider's anonymous
// namespace (which Tasks 2.1 and 2.2 both did, with the explicit ask to
// consolidate once the per-domain collapse proved the helper shape).
//
// Rule: prefer the last non-empty path segment of the server-supplied
// href (e.g. https://cloud.example.com/dav/calendars/alice/personal/
// yields "personal"). When the href yields no usable URL characters
// (rare — e.g. a calendar at a child URL the server routed to "/" or
// an addressbook that the discovery layer advertised with no path),
// fall back to a sanitised slice of the human display name so we never
// return an empty slug. The sanitiser keeps lowercase a-z0-9, replaces
// everything else with '-', collapses separator runs, and trims edges.
//
// This is intentionally a free function in a small header rather than a
// method on ProviderBackendSpec: the slug derivation is the providers'
// concern, not the spec's, and keeping it free keeps the struct
// header-free of QUrl / string-massaging logic.
QString makeDavSlug(const QString &rawName, const QString &href);

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_DAVSLUG_H
