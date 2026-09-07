# ADR 0002: Coordinated breaking refactor

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

Libkalburator, PlanStan, and WildPalms are not in production use. Existing compatibility layers, deprecated overloads, source include leaks, target aliases, and duplicated execution paths make the architecture harder to correct.

## Decision

During the consolidation roadmap, the three repositories may make coordinated breaking changes. A clean shared boundary is preferred to preserving old source, binary, schema, or behavior compatibility.

Compatibility scaffolding is permitted only when a named task requires staged migration. It must have a deletion task and may not create a second permanent execution path.

## Consequences

- Library and consumer changes may land as coordinated flag days.
- Local development data may be reset when a migration offers less value than its complexity; affected data must be named.
- Pre-consolidation tags do not imply public API stability.
- Stable semantic versioning begins only after the installable-package gate.

