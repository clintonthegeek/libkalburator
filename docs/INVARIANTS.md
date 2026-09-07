# Invariants

These rules apply throughout the consolidation. An exception must be explicit in the task and, if durable, recorded in an ADR.

## Correctness

1. A successful run means every requested mapping reached a terminal, truthfully reported state.
2. Every future, callback, and operation completes exactly once on success, failure, or cancellation.
3. Cancellation and teardown are bounded. No fallback may wait forever.
4. A fetch or load failure is not an empty collection and cannot advance baselines or tokens.
5. Persist baselines, aliases, and progress tokens only for the data a successful run actually observed and applied.
6. Empty selection means no work. “All enabled” is always explicit.
7. Unsupported operations return a typed unsupported result. Base-class no-ops cannot masquerade as support.
8. Transformation failure is loud. Non-empty native input becoming empty canon/demotion is an error unless the edge explicitly permits it.
9. Declared loss, canonical comparison, merge, and demotion must agree for every supported record kind.
10. Credentials never appear in logs or general configuration values.

## Ownership and execution

11. Consumers own user intent and external-resource sessions; libkalburator owns synchronization execution.
12. One library runtime owns registries, plugins, providers, backends, stores, run state, and teardown for one application collection/profile.
13. Consumers never move backend QObjects or call backend I/O on an arbitrary thread.
14. Every backend operation runs on its declared executor. Network, device, and large filesystem I/O are asynchronous and timeout-bounded.
15. There is one run coordinator for all, subset, single, mirror, full, and destructive synchronization intents.
16. Normal graph convergence is library policy, not a consumer-owned future loop.
17. Provider, topology, materialization, and mapping mutations are atomic or return explicit repair state.
18. Runtime events expose typed canonical facts; consumers do not parse backend-native records to update models.

## Consumer exceptions

19. WildPalms owns Palm connection establishment, handshake, keepalive, category preparation, backup, restore, write flush, and EndOfSync timing.
20. WildPalms exposes those responsibilities through a generic external-resource lease; Palm types do not enter libkalburator.
21. PlanStan owns its UI, application model, collection open/close intent, and application-only adapters.
22. A consumer-specific backend or shape extension may remain in the consumer when it has no reusable meaning.

## Architecture and build

23. The headless core does not depend on Qt Widgets or consumer targets.
24. Public headers live under a namespaced installed include tree.
25. Public CMake targets do not expose private source directories.
26. Registration is explicit; correctness never depends on whole-archive or static-constructor side effects.
27. Feature families are independently optional at link and dependency level.
28. No compatibility shim is added without a deletion task and named consumer need.
29. During this pre-production refactor, a coordinated clean break is preferred to permanent compatibility complexity.

## Scope

30. Stabilize the PlanStan and WildPalms vertical slices before adding a domain, vendor interior, provider, or general-purpose abstraction.
31. A feature is supported only when it is provisionable, capability-checked, lifecycle-owned, tested, consumer-integrated, and documented.
32. Code with no supported path is experimental or removed; registry visibility alone does not make it a feature.

## Documentation

33. `README.md` is the front door; `docs/ROADMAP.md` is the only roadmap; `docs/TASKS.md` is the only active queue; `docs/KNOWN_ISSUES.md` is the only active defect list.
34. Current documents describe current facts. Git and `docs/archive/` preserve history.
35. Completing code requires updating every affected maintained document listed in `AGENTS.md`.
36. Test failures are classified by both cause and product impact. “Pre-existing” is not a severity or disposition.

