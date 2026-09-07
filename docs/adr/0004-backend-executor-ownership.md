# ADR 0004: Library-owned backend executors

- **Status:** Accepted
- **Date:** 2026-09-03

## Decision

Provider-created backends are adopted by a library-owned `BackendExecutor`.
The executor moves the QObject-backed backend before its thread starts, provides
the only synchronous compatibility dispatch point, destroys the backend on its
own thread, and then performs bounded thread shutdown. `BackendRegistry` stores
only a non-owning backend pointer for lookup and dispatch.

Consumer code must not move or destroy registered backends. A consumer-owned
external-resource executor remains the seam for leased device I/O such as Palm
access.

## Consequence

This removes backend thread-affinity assembly from the ordinary provider path.
The existing consumer-managed relocation tests remain migration coverage until
PlanStan and WildPalms use the runtime/executor boundary end to end.
