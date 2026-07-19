# WP-A1 RFC — calendarsOnly mode design

**Status:** RESOLVED 2026-07-19. Both consumers signed off (PlanStan uses
`calendarsOnly=true` explicitly; WildPalms signed off 2026-07-18,
`WildPalms/docs/2026-07-18-wpa1-calendarsonly-rfc-response-wildpalms.md`). The
last residue — the ctor default disagreeing with the contribution's explicit
`false` — is now closed: `MultiProtocolDavProvider`'s `calendarsOnly` ctor
default (and `m_calendarsOnly` member) flipped `true`→`false`, so the ctor
default agrees with the sole real construction
(`multiprotocoldavbackendcontribution.h` passes `false`) and no direct
construction silently gets the mode. `tst_multiprotocoldavprovider` green.
_(Original status below, for provenance.)_

**Status (original):** Open — awaiting consumer sign-off (PlanStan + WildPalms) before landing.
**Supersedes:** The `b47d75e` behavior-preserving patch (ctor-argument swallow fix) which
left the mode permanently engaged at the provider level and the dialog filter as the
only active gate. That patch was correct but left the multi-layer policy conflict
(supplement S3) unresolved.

## Problem statement (supplement S3)

After `b47d75e`, three layers describe the calendarsOnly policy and they disagree:

| Layer | Code | Policy |
|---|---|---|
| Provider ctor | `MultiProtocolDavProvider(bool calendarsOnly = true, ...)` | **Always calendarsOnly** (ctor default stays true; every direct construction gets the mode for free) |
| Dialog filter | `providerconfigdialog.cpp:313` — `c.type == "calendar"` filter | Filters to calendar-only in the UI |
| Registry registration | `providermanager.cpp:256` — `provider->collections()` with no filter | **ALL collections** registered — contacts backends reach BackendRegistry regardless of mode |

The provider-side mode is never persisted (not in `BackendConfiguration.connectionParams`);
if a persisted config is `load()`-ed into a freshly constructed provider, `m_calendarsOnly`
reverts to the ctor default (`true`) regardless of how the account was originally created.
This is a trap: any direct construction of `MultiProtocolDavProvider` silently engages the
mode.

## Proposed shape

### 1. Persist the mode in `BackendConfiguration`

```cpp
// In MultiProtocolDavProvider::load():
m_calendarsOnly = p.value(QStringLiteral("calendarsOnly"), true).toBool();

// In MultiProtocolDavProvider::save():
c.connectionParams[QStringLiteral("calendarsOnly")] = m_calendarsOnly;
```

The `true` default in `load()` preserves backward compatibility for configs that
predate this field (they had the mode engaged by ctor default and should continue to).

### 2. Flip the ctor default to `false`

```cpp
explicit MultiProtocolDavProvider(bool calendarsOnly = false, QObject *parent = nullptr);
```

Rationale: once the mode is config-driven, a direct construction with no explicit
argument should start in the less-surprising "full" mode. Callers that need the mode
either pass `true` explicitly or, better, route through `load(BackendConfiguration)`.
The `b47d75e` contributions already pass `false` explicitly, so this is safe.

### 3. Dialog filter stays as UI concern

`providerconfigdialog.cpp:313` filters the collection picker to `type == "calendar"`.
This is correct UI behavior for the "add CalDAV account only" flow and should remain
regardless of the provider-side mode. **No change needed here.**

### 4. `registerProviderBackends` honors the provider's effective collection set

`providermanager.cpp:256` calls `provider->collections()` — this already returns the
effective set the provider exposes after `connect()`. The fix is to ensure the provider
itself restricts `collections()` to the appropriate subset when `m_calendarsOnly` is
true. Currently `MultiProtocolDavProvider::collections()` returns all collections; it
should filter to calendar-type collections when `m_calendarsOnly` is true.

```cpp
QList<CollectionInfo> MultiProtocolDavProvider::collections() const
{
    if (!m_calendarsOnly)
        return m_collections;
    QList<CollectionInfo> out;
    for (const auto &c : m_collections)
        if (c.type == QStringLiteral("calendar"))
            out.append(c);
    return out;
}
```

With this change, `registerProviderBackends` requires no modification — it naturally
registers only what the provider exposes, and the mode is enforced at the source.

## Regression tests (to be added with WP-D3)

A composed FakeCalDav+FakeCardDav harness asserting:
- `m_calendarsOnly = true`: `collections()` returns only `type == "calendar"` entries;
  `registerProviderBackends` registers exactly those backends.
- `m_calendarsOnly = false`: all collections registered.
- Round-trip: `save()` → `load()` preserves the mode in both directions.
- `b47d75e` ctor regression: `MultiProtocolDavProvider(false, parent)` — mode starts false.

## Consumer impact

| Consumer | Impact |
|---|---|
| PlanStan | Accounts persisted via `BackendConfiguration` will gain the `calendarsOnly` key on first `save()` after this lands. Old configs (no key) behave identically (default `true`). No code change needed unless PlanStan constructs `MultiProtocolDavProvider` directly. **Verify:** `grep -rn "MultiProtocolDavProvider" ~/dev/PlanStan/src` |
| WildPalms | Same persistence migration story. **Verify:** `grep -rn "MultiProtocolDavProvider\|calendarsOnly" ~/dev/WildPalms/src` |

**Action required from consumers:** Confirm the ctor-default flip from `true` to `false`
does not break any direct-construction sites, and confirm the `collections()` filter
shape is consistent with how they consume the provider's collection list.

## Files to touch (after sign-off)

- `src/sync/multiprotocoldavprovider.h` — ctor default `bool calendarsOnly = false`
- `src/sync/multiprotocoldavprovider.cpp` — `load()`, `save()`, `collections()`
- `tests/calendar/tst_multiprotocoldavprovider.cpp` — round-trip + mode-filter tests
