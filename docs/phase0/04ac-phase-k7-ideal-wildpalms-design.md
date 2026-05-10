# Phase K.7 — Ideal WildPalms Architectural Design

**Status:** Design-only phase (no code changes)  
**Date:** 2026-05-10  
**Audience:** WildPalms development team; reference for future libkalburator consumers  
**Reviewed by:** User (2026-05-10)  
**Tag:** `v0.35.5-phase-k7-design`

---

## 1. Core Principle

WildPalms is the **first multi-domain consumer of the unified libkalburator engine**. Its architecture should be a **reference implementation** for how consumers integrate with libkalburator — useful as a blueprint for future consumers (PlanStan enhancements, mobile apps, etc.).

The existing WildPalms plugin system is well-designed and KDE-idiomatic. K.7 extends it to enable plugins to:
- Declare domain plugins (non-stock shapes + transformers)
- Provide both Palm-side AND PC-side backends
- Participate in shape transformation pipelines

Boundary clarity: libkalburator owns library infrastructure (CalDAV, CardDAV, base transcoding); WildPalms and its plugins own consumer-specific logic (Palm device access, custom formats).

---

## 2. Plugin Architecture Extension

### 2.1 IBackendPluginV2 API Changes

Current shape (per K.6 state):

```cpp
class IBackendPluginV2 : public IPlugin {
    virtual QStringList claimedDatabases() const = 0;
    virtual std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(PalmDeviceAccess *device) = 0;
    virtual void registerDomain(DomainRegistry &) {}
    virtual Kalburator::Conflict::ConflictHandler *createConflictHandler() { return nullptr; }
};
```

**K.7 extension:**

```cpp
class IBackendPluginV2 : public IPlugin {
    // Existing methods unchanged...
    
    // NEW: Optional PC-side backend (default: nullptr)
    virtual std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPCBackend(const QString &collectionId)
    {
        return nullptr;  // default: no PC backend
    }
};
```

**Semantics:**
- `createPalmBackend()` — required; provides the device-side backend (syncs from/to Palm device via HotSync conduit or equivalent)
- `createPCBackend()` — optional; provides the data-source backend (syncs from/to PC resources: files, URLs, services)
- Example: Documents To Go plugin
  - Palm side: reads Palm .pdb files from device memory via device access
  - PC side: reads/writes ODF or MS Office files from filesystem or online storage
  - Both implement `IBlobBackend`; domain plugin bridges transformations

**Lifecycle:**
1. Plugin loaded via KPluginFactory
2. `registerDomain()` called (wires domains + transformers into libkalburator)
3. Runtime creates mappings that wire Palm backend + PC backend via SyncEngine
4. Sync flow: device → Palm backend → shape transformation → PC backend → PC resource

### 2.2 Plugin Loading Integration

**Current (K.6):** BackendPluginManager::loadPlugin() creates plugin but doesn't invoke `registerDomain()`.

**K.7:** Integrate domain registration into the load path.

```cpp
bool BackendPluginManager::loadPlugin(const QString &pluginId) {
    // ... existing factory load + cast ...
    
    it->instance = plug;
    
    // NEW: Register domains if plugin provides them
    plug->registerDomain(DomainRegistry::instance());
    
    qDebug() << "[BackendPluginManager] Loaded plugin:" << plug->pluginId();
    emit pluginLoaded(plug);
    return true;
}
```

**Guarantees:**
- `registerDomain()` called **after** plugin instance exists
- `registerDomain()` called **before** any sync work that touches the plugin's domains
- Safe to call from app startup (single-threaded, before sync threads start)
- Subsequent registrations during plugin load update TransformationRegistry immediately (per DomainRegistry::registerPlugin contract)

---

## 3. Domain & Transformer Registration

### 3.1 Stock Domains (Library-Owned)

Provided by libkalburator; available out-of-the-box:

| Domain | Canonical Shape | Peer Shapes | Backends | Transformers |
|--------|-----------------|-------------|----------|--------------|
| calendar | iCal | blob, ical-minimal | CalDAV, local, blob | iCal↔blob |
| contacts | vCard 4.0 | vCard 3.0, blob, carddav-subset | CardDAV, local, blob | vCard3↔4, vCard↔blob |
| memo | blob | plain-text, html | local, blob | text↔blob, html↔blob |
| todo | blob | ical-todo, blob | local, blob | iCal↔blob |

Registered at static-init time via internal registrars in each domain plugin. DomainRegistry::initialize() called early (app startup) populates TransformationRegistry for all stock domains.

### 3.2 Consumer-Owned Domains (Plugin-Provided)

Plugins introduce new domains by implementing DomainPlugin and registering via registerDomain() hook.

**Example: Documents To Go office-document domain**

Plugin declares:
- Domain ID: `office-document`
- Canonical shape: Palm .pdb (proprietary format)
- Peer shapes:
  - ODF (OpenDocument Format)
  - MS Office (.docx, .xlsx, .pptx)
- Transformers:
  - .pdb ↔ ODF (bidirectional)
  - .pdb ↔ MS Office (bidirectional)
- Backends:
  - Palm: reads device memory, writes to device memory
  - PC: reads/writes ODF and MS Office files from filesystem or cloud storage

```cpp
class DocumentsToGoPlugin : public IBackendPluginV2 {
    void registerDomain(DomainRegistry &registry) override {
        auto plugin = std::make_shared<OfficeDocumentDomainPlugin>();
        registry.registerPlugin(plugin);
        // DomainRegistry::registerPlugin() immediately:
        // - calls plugin->registerEdges(TransformationRegistry::instance())
        // - updates TransformationRegistry with edges for .pdb↔ODF, .pdb↔MS
    }
};
```

### 3.3 Transformer Pipeline

The shape transformation pipeline (from Phase G) is the transport layer. Plugins populate it by declaring edges.

**Flow:**
1. Plugin A provides domain X with shapes X₁ (canonical), X₂ (peer)
2. Plugin A registers transformers: X₁ ↔ X₂
3. Future plugin B provides domain Y with shapes Y₁ (canonical), Y₂
4. Plugin B registers transformers: Y₁ ↔ Y₂
5. SyncEngine can now route records: X₂ → X₁ → (hypothetical cross-domain edge?) → Y₁ → Y₂

Current (Phase K): edges are intra-domain. Cross-domain edges are out-of-scope; domains are independent sync pipelines.

**Plugin behavior for complex scenarios:**

If a Documents To Go plugin introduces a second domain (e.g., spreadsheet-documents), it registers both:
- `registerDomain()` creates and registers the office-document domain
- `registerDomain()` creates and registers the spreadsheet-documents domain
- Both register their transformers independently

---

## 4. Backend Ownership Model (Hybrid)

| Component | Owner | Location | Scope |
|-----------|-------|----------|-------|
| **Library-provided backends** | libkalburator | `src/backend/` | CalDAV, CardDAV, local file, base blob |
| **Stock domain plugins** | libkalburator | `src/calendar/`, `src/contacts/`, etc. | iCal, vCard, memo, todo |
| **Transformers (stock)** | libkalburator | `src/shape/`, `src/transcoding/` | Base iCal↔blob, vCard↔blob, etc. |
| **Consumer backends** | WildPalms + plugins | `src/runtime/`, `src/plugins/` | Palm device access, local Palm files |
| **Custom domain plugins** | Plugins | `src/plugins/*/` | Documents To Go office-document, future domains |
| **Custom transformers** | Plugins | `src/plugins/*/` | Registered via domain plugin's registerEdges() |

### 4.1 Plugin-Provided Backends

**Palm-side (required by IBackendPluginV2):**
- Device-specific backend that syncs from/to Palm device
- Example: HotSync conduit plugin reads/writes Palm device memory
- Only one plugin typically provides this (WildPalms's own runtime plugin)

**PC-side (optional in K.7):**
- Data-source backend that syncs from/to PC resources
- Examples:
  - Local file system (documents, photos, records)
  - Web service (Dropbox, OneDrive, Google Drive)
  - Database (SQLite, PostgreSQL)
  - Cloud calendar service (Nextcloud, iCloud)
- Plugin can provide multiple PC backends (one per resource type or service)

**Ownership decision (K.7):**
- Library provides stock PC backends: CalDAV, CardDAV, local file
- Plugins provide custom PC backends: cloud storage, custom formats, etc.
- Rationale: library can't know all possible data sources; plugins extend as needed

### 4.2 Backend Lifecycle

```
Plugin A (Documents To Go) loads:
1. registerDomain() called
   → Creates OfficeDocumentDomainPlugin
   → Registers with DomainRegistry
   → registerEdges() populates TransformationRegistry
   
2. Runtime wires backends into SyncEngine:
   → Palm mapping: device → plugin->createPalmBackend() → SyncEngine
   → PC mapping: filesystem → plugin->createPCBackend("collectionId") → SyncEngine
   
3. User creates a sync mapping:
   Palm Documents To Go ↔ PC Filesystem (ODF files)
   SyncEngine handles:
   - Fetch from device (via plugin's Palm backend)
   - Transform to ODF (via registered transformers)
   - Store to filesystem (via plugin's PC backend)
```

---

## 5. Single SyncEngine in WildPalms

### 5.1 Engine Ownership

- **One libkalburator SyncEngine instance** per PalmRuntime
- Owned and lifetime-managed by PalmRuntime
- Shared across all plugins and all sync mappings
- No per-app-plugin engine; no wrapper in KF6MainWindow

### 5.2 Configuration & Settings

Settings apply globally to the engine (not per-plugin):
- Sync thread pool size
- Cancellation timeout
- Conflict resolution policies (per-mapping, but evaluated by one engine)
- Logging levels

Plugin-specific settings (device connection string, format preferences, etc.) stored by the plugin itself, passed to backend constructors.

### 5.3 Deletions (from pre-K.7 state)

- ❌ Native `SyncEngine` wrapper in `KF6MainWindow` (replaced by library instance)
- ❌ `BlobBackendAdapter` (no longer needed; unified engine handles blob routes)
- ❌ `CalendarCollection_WP` null-guard workaround (replaced by proper domain registration)
- ❌ Dead V1 `BackendPluginManager` (replaced by V2)
- ❌ `.wildpalms.providers` parallel persistence file (consolidated into .kalburator-sync.db)

### 5.4 Memory Safety

Per K.7 audit findings, memory corruption guard in `~KF6MainWindow` should be investigated and resolved at root cause (not papered over). After K.7 cleanup, the guard should not be necessary (proper ownership, no dangling pointers).

---

## 6. Persistence Model (Single)

### 6.1 Database State

**`.kalburator-sync.db`** (libkalburator's SQLite database):
- Sync mappings (which Palm database ↔ which PC collection)
- ID mappings (Palm UID ↔ PC UID)
- Baselines (three-way merge state)
- Mapping metadata (direction, conflict policy)
- Collection properties (calendar color, description, etc.)

Single database shared by all consumers of libkalburator; WildPalms doesn't need a separate copy.

### 6.2 Application State

**`.wildpalms` config file** (KConfigXT):
- Window geometry, dock state
- UI preferences (theme, sidebar width, etc.)
- Plugin configuration (enabled plugins, plugin-specific options)
- Per-mapping sync schedule and notification preferences

No overlap with `.kalburator-sync.db`; separate concerns.

### 6.3 Removed (K.7 cleanup)

- ❌ `.wildpalms.providers` (parallel ProviderManager state) — consolidated into libkalburator's provider registry + mapping persistence

---

## 7. Plugin Metadata (JSON)

WildPalms plugins use KDE's `.json` metadata format (co-located with plugin binary).

**K.6 baseline (current):**

```json
{
  "KPlugin": {
    "Id": "org.wildpalms.contacts",
    "Name": "Contacts",
    "Description": "Palm Contacts backend",
    "Icon": "contact-new",
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["AddressDB"],
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 10
  }
}
```

**K.7 extensions (optional, for discovery & debugging):**

```json
{
  "KPlugin": {
    "Id": "org.wildpalms.documents-to-go",
    "Name": "Documents To Go",
    "Description": "Palm office documents synced to ODF/MS Office formats",
    "Icon": "document-multiple",
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["DataDB MPowerDoc Word", "DataDB MPowerDoc Sheet"],
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 20,
    "X-WildPalms-ProvidedDomains": ["office-document"],
    "X-WildPalms-ProvidedShapes": ["pdb", "odf", "ms-office"],
    "X-WildPalms-ProvidesPCBackend": true
  }
}
```

**Metadata purposes:**
- `X-WildPalms-ProvidedDomains`: which domains this plugin registers (for logging, debugging, future UI)
- `X-WildPalms-ProvidedShapes`: which shapes this plugin handles (optional; for discovery)
- `X-WildPalms-ProvidesPCBackend`: whether plugin provides PC-side backends (helps UI know if plugin is full-featured)

Metadata is *optional*; plugins without these tags still work (they just don't advertise their capabilities).

---

## 8. Reference Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│ libkalburator (library)                                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ SyncEngine (unified, domain-agnostic)                  │ │
│  │ - runSyncFuture(mappingId)                             │ │
│  │ - Consults DomainRegistry for differ/merger            │ │
│  │ - Consults TransformationRegistry for shape pipelines  │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ DomainRegistry: stock (calendar, contacts, etc.)       │ │
│  │               + dynamic (plugin-provided)              │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ TransformationRegistry: shape edges & pipelines        │ │
│  │ (populated by stock domains + plugin domains)          │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  Stock backends: CalDAV, CardDAV, local file, blob          │
│  Stock domains: calendar, contacts, memo, todo              │
│                                                              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ WildPalms (consumer application)                            │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ PalmRuntime                                            │ │
│  │ ├─ SyncEngine instance (owned here)                    │ │
│  │ ├─ BackendPluginManager                                │ │
│  │ └─ SyncMapping registry & UI coordination              │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ Plugins (wildpalms/plugins/)                           │ │
│  ├─ Palm runtime plugin (HotSync conduit)                 │ │
│  │  ├─ Palm backend: device ↔ SyncEngine                  │ │
│  │  └─ No PC backend                                      │ │
│  │                                                        │ │
│  ├─ Documents To Go plugin (office documents)             │ │
│  │  ├─ Palm backend: device .pdb → SyncEngine             │ │
│  │  ├─ PC backend: filesystem ODF/MS ↔ SyncEngine         │ │
│  │  ├─ Domain: office-document (canonical .pdb)           │ │
│  │  └─ Transformers: .pdb ↔ ODF, .pdb ↔ MS Office        │ │
│  │                                                        │ │
│  ├─ Future custom plugin                                  │ │
│  │  ├─ Provides additional domains/shapes                │ │
│  │  └─ Registers via registerDomain() hook               │ │
│  │                                                        │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  Persistence: .kalburator-sync.db + .wildpalms config       │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 9. Verification Gates for K.8 (Code Implementation)

These gates verify that the K.7 design was implemented correctly.

**Build & Test:**
- ✅ `verify-all.sh` green across all three repos
- ✅ libkalburator 78+ tests pass (baseline from K.6)
- ✅ PlanStan 82+ tests pass (baseline from K.6)
- ✅ WildPalms 81+ tests pass (baseline from K.6)

**Plugin Integration:**
- ✅ BackendPluginManager calls `registerDomain()` after loading each plugin
- ✅ Plugins' DomainPlugin instances registered with DomainRegistry::registerPlugin()
- ✅ TransformationRegistry updated with plugin's shape edges
- ✅ PC-side backend hooks (`createPCBackend()`) wired into mapping setup

**Architecture:**
- ✅ Single SyncEngine instance owned by PalmRuntime (no per-plugin engines)
- ✅ No consumer type imports in libkalburator core (only shape/backend/conflict contracts)
- ✅ `.wildpalms.providers` file removed; state consolidated to .kalburator-sync.db

**E2E Scenarios:**
- ✅ Palm ↔ CalDAV sync passes (calendar domain, stock transformers)
- ✅ Palm ↔ CardDAV sync passes (contacts domain, stock transformers)
- ✅ Phase J E2E tests pass both directions:
  - ✅ palm→caldav
  - ✅ caldav→palm
- ✅ Plugin-provided domain sync scenario (if DTG or custom plugin available for testing)

**Memory Safety:**
- ✅ Root cause of memory corruption guard in `~KF6MainWindow` identified & resolved
- ✅ Guard should be unnecessary after K.7 cleanup (proper ownership, no dangling pointers)

---

## 10. Out of Scope (Deferred)

Explicitly deferred; not part of K.7 or K.8:

- Plugin UI pages in settings dialog (post-K.8 UX work)
- Plugin enable/disable toggles at runtime (load at startup; settings-based control deferred)
- Plugin marketplace or auto-update system (deferred; currently static install)
- Akonadi backend (tracked in deferred-work.md; future consumer of libkalburator)
- Cross-domain transformations (bridges between office-document and memo, etc.)

---

## 11. Relationship to K.0–K.6

- **K.0–K.3:** Structural generalization; unified engine + shape pipeline established
- **K.4–K.5:** Baseline unification & storage reorganization
- **K.5.5:** Semantic cleansing; coherent naming & vocabulary
- **K.6:** Docstring cleanup, audit report archival
- **K.7:** Plugin architecture extension & integration (this document)
- **K.8:** Code implementation of K.7 design

K.7 is design-only. K.8 lands the code.

---

## 12. Success Criteria

K.7 is successful when:

1. **Architecture clarity:** WildPalms developers and future libkalburator consumers understand:
   - How plugins integrate with SyncEngine
   - Who owns which components (library vs. consumer)
   - How to introduce new domains via plugins
   - How to bridge data sources (Palm + PC backends)

2. **Reference implementation:** WildPalms serves as a blueprint for:
   - Proper plugin loading and domain registration
   - Clean separation of library infrastructure from consumer logic
   - Hybrid plugin architecture (library-provided + consumer-provided)

3. **Design validation:** User approves design before K.8 code implementation

4. **Specification:** This document is sufficiently detailed that K.8 implementation can proceed without major design decisions remaining open
