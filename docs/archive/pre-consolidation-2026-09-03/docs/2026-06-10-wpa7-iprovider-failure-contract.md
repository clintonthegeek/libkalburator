# WP-A7 — IProvider failure contract (RFC-lite)

**Status:** Landed (2026-06-10). Consumer notification required; see §Consumer impact.
**Scope:** `connectionStateChanged(false)` semantics, signal-vs-future ordering,
`lastError()` pull accessor. Supplement S6.

## What changed

### 1. `connectionStateChanged(false)` no longer emits on connect failure

**Before:** `MultiProtocolDavProvider::onDiscoveryFinished` emitted
`connectionStateChanged(anyOk)` unconditionally, so a failed connect emitted
`connectionStateChanged(false)` even though the provider was never in a connected
state. CalDav/Akonadi never did this (they only emit false from `disconnect()`).

**Decision:** The signal's contract is "connected state changed from true to false" —
not "connect() returned false". Emitting false on a failed connect is a protocol
violation (you can't go from connected=false to connected=false). Fixed in
`multiprotocoldavprovider.cpp`: only emit when `anyOk == true` (connection made) or
from `disconnect()` (connection lost).

**Consumer impact:** If your code calls `connect()` and waits for
`connectionStateChanged(false)` as the "connect failed" signal, you will no longer
receive it. Use the future returned by `connect()` instead — it resolves to `false` on
failure. The `error()` signal fires on all failures and already provides a message.

### 2. Signal-vs-future ordering

The future returned by `connect()` resolves **before** any signals fire. This
ensures callers driving the connect purely via the future can safely read
`collections()` in a then()-continuation before any `collectionsChanged()` listener
runs. The `error()` signal fires **before** the future resolves on failure (so UI
can show the message immediately) — this is unchanged.

### 3. `lastError()` pull accessor added

`IProvider::lastError()` is now a pull accessor parallel to `lastWarning()`. Default
returns `QString{}`. Implementations that accumulate a failure message (CalDav,
CardDav, MultiProto) should populate `m_lastError` from the discovery error message
and clear it at the start of `connect()`.

Added to:
- `iprovider.h` — virtual accessor with empty-string default
- `MultiProtocolDavProvider` — populated from the combined calDav/cardDav error on
  connect failure; cleared at connect start
- `CalDavProvider` — populated from `m_discovery->errorMessage()` on failure; cleared
  at connect start

## Consumer action required

| Consumer | Required change |
|---|---|
| PlanStan | If any code path awaits `connectionStateChanged(false)` as a connect-failure signal, switch to future.result() == false. Audit: `grep -rn "connectionStateChanged" src/`. |
| WildPalms | Same audit. `palmruntime.cpp` drives connect via futures — likely already correct. |

Both consumers subscribe to `error()` for UI messages, which is unaffected.
`lastError()` is additive and requires no migration.
