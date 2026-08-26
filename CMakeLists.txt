cmake_minimum_required(VERSION 3.19)
project(Kalburator VERSION 0.2.4 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# libkalburator — a reusable sync substrate for Qt6/KF6 calendar apps.
# Three targets:
#   Kalburator::Types — minimal type vocabulary (value types, enums, pure
#     interfaces: LogicalCalendar, BackendRecord, ICalendarCollection, SyncMapping,
#     etc.). Carries NO JSON, NO file I/O, NO QObject machinery (Plan 5 / AUDIT B2
#     moved those out). Depends only on Qt6::Core, Qt6::Gui, KF6::CalendarCore.
#   Kalburator::TypeSupport — light behavioural companion to Types (config JSON,
#     crash-journal I/O, incidence lock registry, LogicalCalendar JSON codec).
#     Depends only on Kalburator::Types + Qt — NOT the Sync engine — so light
#     downstream foundation libs can link it.
#   Kalburator::Sync — sync backends + coordination. Depends on
#     Kalburator::Types plus KF6::DAV/KIO/Holidays. Link this from host
#     applications that embed the sync engine.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Sql Xml Network Widgets Concurrent)
find_package(KF6CalendarCore REQUIRED)
find_package(KF6DAV REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(KF6Holidays REQUIRED)
find_package(KF6Contacts REQUIRED)
find_package(KF6Config REQUIRED)

# Optional org-io support for OrgBackend. When ON, host must provide the
# planstan-org-io target via PUBLIC linkage from outside; standalone builds
# default OFF and exclude orgbackend files.
set(KALBURATOR_HAVE_ORG_IO OFF CACHE BOOL "Build with PlanStan org-io (for OrgBackend)")

# Optional Akonadi support; follows host's HAVE_AKONADI setting in-tree.
set(KALBURATOR_HAVE_AKONADI OFF CACHE BOOL "Build with KPim6 Akonadi support")

# Optional outline-domain org encoding peer. The outline domain's canon and OPML
# peers are always built; the org peer (org<->canon transcoder) is the ONLY thing
# that needs OrgGrove. When OFF, orgcanonstages is excluded, the org peer/edges are
# not registered, and OrgGrove is not fetched/linked at all — consumers that don't
# need org outlines get an OrgGrove-free build. Default ON preserves prior behavior
# and existing (outline,org) configs. Independent of KALBURATOR_HAVE_ORG_IO (that
# flag gates the calendar OrgBackend, a separate subsystem).
set(KALBURATOR_HAVE_OUTLINE_ORG ON CACHE BOOL "Build the outline org encoding peer (requires OrgGrove)")

# --- Kalburator::Types ------------------------------------------------------
file(GLOB KALBURATOR_TYPE_HEADERS CONFIGURE_DEPENDS src/types/*.h)
file(GLOB KALBURATOR_TYPE_SOURCES CONFIGURE_DEPENDS src/types/*.cpp)

# Shape value type — pure Qt, no KCalendarCore / sync machinery.
# Lives in src/shape/ but exposed via kalburator-types so that
# IIncidenceSource::Entry and IIncidenceRegistry can carry Shape values
# without consumers needing to link the full Kalburator::Sync library.
set(KALBURATOR_SHAPE_CORE_HEADERS src/shape/shape.h)
set(KALBURATOR_SHAPE_CORE_SOURCES src/shape/shape.cpp)

add_library(kalburator-types STATIC
    ${KALBURATOR_TYPE_HEADERS}
    ${KALBURATOR_TYPE_SOURCES}
    ${KALBURATOR_SHAPE_CORE_HEADERS}
    ${KALBURATOR_SHAPE_CORE_SOURCES}
)
add_library(Kalburator::Types ALIAS kalburator-types)

target_include_directories(kalburator-types
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/types>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/shape>
)

target_link_libraries(kalburator-types
    PUBLIC
        Qt6::Core
        Qt6::Gui
        KF6::CalendarCore
)

set_target_properties(kalburator-types PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Kalburator::TypeSupport ------------------------------------------------
# Light behavioural companion to Kalburator::Types: JSON codecs, crash-journal
# I/O, and the incidence lock registry that used to sit in the "minimal type
# vocabulary" (AUDIT B2). Depends ONLY on Kalburator::Types + Qt — NOT on the
# heavy Kalburator::Sync engine — so deliberately-light downstream consumers
# (libkalcal, PlanStan tests, PlanEngine) can link it without DAV/KIO/Network.
add_library(kalburator-typesupport STATIC
    src/typesupport/backendconfiguration.h
    src/typesupport/backendconfiguration.cpp
    src/typesupport/crashjournal.h
    src/typesupport/crashjournal.cpp
    src/typesupport/incidencelock_registry.h
    src/typesupport/incidencelock_registry.cpp
    src/typesupport/logicalcalendarjson.h
    src/typesupport/logicalcalendarjson.cpp
)
add_library(Kalburator::TypeSupport ALIAS kalburator-typesupport)

target_include_directories(kalburator-typesupport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/typesupport>
)

target_link_libraries(kalburator-typesupport
    PUBLIC
        kalburator-types
)

set_target_properties(kalburator-typesupport PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Kalburator::Sync -------------------------------------------------------
# Sync sources are layered across sub-directories per docs/phase0/05-repo-strategy.md:
#   calendar/   — backends, coordination, CRUD items, ISyncHost
#   conflict/   — conflict framework (incl. Kalburator::Sync::QSyncCore files)
#   diff/       — incidencediff/syncdiff (relocated from transcoding/ in Plan 4 Task 1)
#   journal/    — crash-recovery journal, baseline store, ID mapping, async writer
#   discovery/  — Syncthing auto-discovery + monitor (experimental)
#   blob/       — blob-backend layer (empty placeholder for future work)
#
# NOTE: Source lists below are explicit (not file(GLOB)) on purpose.
# AUTOMOC's timestamp file (build/kalburator_autogen/timestamp) is
# invalidated by changes to the explicit source list, but NOT by new
# files appearing under a globbed directory — so adding a Q_OBJECT
# class to a globbed subdir produces a confusing vtable link error on
# first build until the timestamp is manually removed. See FINDINGS.md
# (2026-04-28 entry) and docs/phase0/04n-phase-e-transcoding-plan.md.
set(KALBURATOR_CALENDAR_HEADERS
    src/calendar/akonadibackend.h
    src/calendar/backendcapabilities.h
    src/sync/backendregistry.h
    src/calendar/caldavcontentcache.h
    src/calendar/calendarmanager.h
    # calendarmetadatamanager moved from src/types/ in Plan 5 (atomic QSaveFile Vdir
    # metadata I/O; no downstream consumer, so calendar/ not TypeSupport; AUDIT B2).
    src/calendar/calendarmetadatamanager.h
    src/calendar/decsyncactivecontroller.h
    src/calendar/decsyncbackend.h
    src/calendar/decsynccontrollerstore.h
    src/calendar/decsyncgarbagecollector.h
    src/calendar/decsynclib.h
    src/calendar/discoveredcalendar.h
    src/calendar/holidaysubscriptionbackend.h
    src/calendar/icalcodec.h
    src/calendar/iconflictpresenter.h
    src/calendar/iconflictresolver.h
    src/calendar/isynchost.h
    src/calendar/localbackend.h
    src/calendar/logicalcalendarbuilder.h
    src/calendar/mockbackend.h
    src/calendar/orgbackend.h
    src/calendar/remotecalendarbackend.h
    src/calendar/subscriptionbackend.h
    src/calendar/syncbackend.h
    src/calendar/syncconflictstore.h
    src/calendar/syncoperation.h
    # Phase G.2 — calendar plugin
    src/calendar/icalproperties.h
    src/calendar/calendarcanonproperties.h
    src/calendar/icalrecorddiffer.h
    # Plan 3 Task C2/C3 — ical ↔ canon JSON stages
    src/calendar/icalcanonstages.h
    # Plan 6 Task 3 — VEVENT field mapping helpers
    src/calendar/eventcanonfields.h
    # Plan 4 Task 1 — VJOURNAL field mapping helpers
    src/calendar/journalcanonfields.h
    # Plan 4 Task 2 — org-ical stages (RRULE simplification)
    src/calendar/orgicalcanonstages.h
    # N1 fix — shared component-scoped recurrence-line extraction
    src/calendar/icalcomponentscan.h
    # N3 fix — authoritative iCal timestamp extraction (LAST-MODIFIED/DTSTAMP/CREATED)
    src/calendar/icaltimestamp.h
    src/calendar/calendardomaindefinition.h
    src/calendar/calendarstockshapes.h
    src/calendar/calendardomainoperations.h
    src/calendar/calendarplugin.h
)
set(KALBURATOR_CALENDAR_SOURCES
    src/calendar/akonadibackend.cpp
    src/calendar/backendcapabilities.cpp
    src/sync/backendregistry.cpp
    src/calendar/caldavcontentcache.cpp
    src/calendar/calendarmanager.cpp
    src/calendar/calendarmetadatamanager.cpp
    src/calendar/decsyncactivecontroller.cpp
    src/calendar/decsyncbackend.cpp
    src/calendar/decsynccontrollerstore.cpp
    src/calendar/decsyncgarbagecollector.cpp
    src/calendar/decsynclib.cpp
    src/calendar/holidaysubscriptionbackend.cpp
    src/calendar/isynchost.cpp
    src/calendar/localbackend.cpp
    src/calendar/logicalcalendarbuilder.cpp
    src/calendar/mockbackend.cpp
    src/calendar/orgbackend.cpp
    src/calendar/remotecalendarbackend.cpp
    src/calendar/subscriptionbackend.cpp
    src/calendar/syncbackend.cpp
    src/calendar/syncconflictstore.cpp
    src/calendar/syncoperation.cpp
    # Plan 3 Task 1 — neutral SyncOperation base extracted into sync/
    src/sync/syncoperation.cpp
    # E5.3 — neutral WriteOperation (SyncBackendBase::applyRecords() return type)
    src/sync/writeoperation.cpp
    # Phase G.2 — calendar plugin
    src/calendar/icalproperties.cpp
    src/calendar/calendarcanonproperties.cpp
    src/calendar/icalrecorddiffer.cpp
    # Plan 3 Task C2/C3 — ical ↔ canon JSON stages
    src/calendar/icalcanonstages.cpp
    # Plan 6 Task 3 — VEVENT field mapping helpers
    src/calendar/eventcanonfields.cpp
    # Plan 4 Task 1 — VJOURNAL field mapping helpers
    src/calendar/journalcanonfields.cpp
    # Plan 4 Task 2 — org-ical stages (RRULE simplification)
    src/calendar/orgicalcanonstages.cpp
    # EEE Phase 2 — Google Calendar v3 event ⇄ canon stages + catalogue
    src/calendar/googleeventproperties.cpp
    src/calendar/googlecanonstages.cpp
    # EEE Phase 7.B — RFC5545 ⇄ Graph patternedRecurrence converter
    # (+ vendored CLDR windowsZones map in windowszonesmap.h, header-only)
    src/calendar/recurrencepatternconverter.cpp
    # EEE Phase 7.B — Microsoft Graph event ⇄ canon stages + catalogue
    src/calendar/mseventproperties.cpp
    src/calendar/mseventcanonstages.cpp
    # EEE Phase 7.C foundation — Graph transport client (pagination + delta)
    src/graph/graphapiclient.cpp
    # B2C P0 — shared blocking HTTP primitive (lab CLI + test-driver use)
    src/net/blockinghttp.cpp
    # B2C P0 — Graph OAuth transport extracted from the GraphCLI lab
    src/graph/graphauthenticator.cpp
    # B2C P0 — Google OAuth transport extracted from the googlecli lab
    src/google/googleauth.cpp
    # B2C P1 — Google Calendar v3 transport client (pagination + sync tokens)
    src/google/googleapiclient.cpp
    # B2C P1 — Google Calendar backend (mock-tested; live checkpoint pending)
    src/calendar/googlecalendarbackend.cpp
    # EEE Phase 7.C — Microsoft Graph calendar backend (Stage-D tested)
    src/calendar/msgraphcalendarbackend.cpp
    # N1 fix — shared component-scoped recurrence-line extraction
    src/calendar/icalcomponentscan.cpp
    # N3 fix — authoritative iCal timestamp extraction (LAST-MODIFIED/DTSTAMP/CREATED)
    src/calendar/icaltimestamp.cpp
    src/calendar/calendardomaindefinition.cpp
    src/calendar/calendarstockshapes.cpp
    src/calendar/calendardomainoperations.cpp
    src/calendar/calendarplugin.cpp
)

set(KALBURATOR_CONFLICT_HEADERS
    src/conflict/conflicthandlerregistry.h
    src/conflict/conflictmanager.h
    src/conflict/conflictpolicy.h
    src/conflict/conflictrecord.h
    src/conflict/conflictstore.h
    src/conflict/synccommon.h
)
set(KALBURATOR_CONFLICT_SOURCES
    src/conflict/conflicthandlerregistry.cpp
    src/conflict/conflictmanager.cpp
    src/conflict/conflictpolicy.cpp
    src/conflict/conflictrecord.cpp
    src/conflict/conflictstore.cpp
)

set(KALBURATOR_DIFF_HEADERS
    src/diff/incidencediff.h
    src/diff/syncdiff.h
)
set(KALBURATOR_DIFF_SOURCES
    src/diff/incidencediff.cpp
    src/diff/syncdiff.cpp
)

set(KALBURATOR_JOURNAL_HEADERS
    src/journal/asyncfilewriter.h
    src/journal/calendarjournal.h
)
set(KALBURATOR_JOURNAL_SOURCES
    src/journal/asyncfilewriter.cpp
    src/journal/calendarjournal.cpp
)

set(KALBURATOR_STORAGE_HEADERS
    src/storage/baselinestore.h
    src/storage/idmappingstore.h
)
set(KALBURATOR_STORAGE_SOURCES
    src/storage/baselinestore.cpp
    src/storage/idmappingstore.cpp
)

set(KALBURATOR_IDENTITY_HEADERS
    src/identity/identitystore.h
    src/identity/identityresolver.h
    src/identity/persondirectory.h
)
set(KALBURATOR_IDENTITY_SOURCES
    src/identity/identitystore.cpp
)

set(KALBURATOR_DISCOVERY_HEADERS
    src/discovery/syncthingdiscovery.h
    src/discovery/syncthingmonitor.h
)
set(KALBURATOR_DISCOVERY_SOURCES
    src/discovery/syncthingdiscovery.cpp
    src/discovery/syncthingmonitor.cpp
)

set(KALBURATOR_BLOB_HEADERS
    src/blob/blobdomaindefinition.h
    src/blob/blobplugin.h
    src/blob/blobstockshapes.h
    src/blob/iblobbackend.h
    src/blob/localblobbackend.h
    src/blob/mockblobbackend.h
)
set(KALBURATOR_BLOB_SOURCES
    src/blob/blobdomaindefinition.cpp
    src/blob/blobplugin.cpp
    src/blob/blobstockshapes.cpp
    src/blob/iblobbackend.cpp
    src/blob/localblobbackend.cpp
    src/blob/mockblobbackend.cpp
)

set(KALBURATOR_ENGINE_HEADERS
    src/engine/enginediff.h
    src/engine/imassdeleteguard.h
    src/engine/mappingqueue.h
    src/engine/perrecorddiff.h
    src/engine/propertydiff.h
    src/engine/syncengine.h
    src/engine/syncenginefuture.h
    src/engine/syncrequest.h
    src/engine/workerteardown.h
)
# Private headers — AUTOMOC needs them in the target sources so Q_OBJECT
# is processed, but they are not part of the library's public surface.
# Only the implementation TU (syncengine.cpp) may #include these.
set(KALBURATOR_ENGINE_PRIVATE_HEADERS
    src/engine/syncengine_p.h
)
set(KALBURATOR_ENGINE_SOURCES
    src/engine/enginediff.cpp
    src/engine/mappingqueue.cpp
    src/engine/perrecorddiff.cpp
    src/engine/propertydiff.cpp
    src/engine/syncengine.cpp
    src/engine/syncenginefuture.cpp
    src/engine/workerteardown.cpp
)

# Phase G — shape-pipeline architecture primitives.
# Foundational vocabulary: Shape, PropertyCatalogue, LossProfile,
# TransformationEdge/Pipeline, TransformationRegistry, DomainPlugin.
# No domain edges registered here; calendar plugin lands in G.2,
# new domains (contacts/memo/todo) in G.5/G.10.
set(KALBURATOR_SHAPE_HEADERS
    src/shape/shape.h
    src/shape/propertycatalogue.h
    src/shape/recordfilter.h
    src/shape/lossprofile.h
    src/shape/transformationedge.h
    src/shape/pipeline.h
    src/shape/transformationregistry.h
    src/shape/canonicalrecord.h
    src/shape/autoresolvestrategy.h
    src/shape/recorddiffer.h
    src/shape/recordmerger.h
    src/shape/recordwriter.h
    src/shape/defaultblobwriter.h
    src/shape/domainregistry.h
    # K.7.1 — split plugin interface
    src/shape/domaindefinition.h
    src/shape/shapecontribution.h
    src/shape/domainoperations.h
    src/shape/domainoperationsregistry.h
    # Plan 3 — canon JSON differ + merger
    src/shape/canonjsondiffer.h
    src/shape/canonjsonmerger.h
)
set(KALBURATOR_SHAPE_SOURCES
    # shape.cpp lives in kalburator-types (exposed via KALBURATOR_SHAPE_CORE_SOURCES)
    src/shape/propertycatalogue.cpp
    src/shape/recordfilter.cpp
    src/shape/lossprofile.cpp
    src/shape/transformationedge.cpp
    src/shape/pipeline.cpp
    src/shape/transformationregistry.cpp
    src/shape/domainregistry.cpp
    src/shape/defaultblobwriter.cpp
    # K.7.1 — split plugin interface
    src/shape/domainoperationsregistry.cpp
    src/shape/canonenvelope.cpp
    # Plan 3 — canon JSON differ + merger
    src/shape/canonjsondiffer.cpp
    src/shape/canonjsonmerger.cpp
)

# Phase G.5 — new domain plugins (todo, contacts, memo).
set(KALBURATOR_TODO_HEADERS
    src/todo/icalvtodoproperties.h
    src/todo/todocanonproperties.h
    src/todo/icalvtododiffer.h
    src/todo/todotxttransformation.h
    # Plan 3 Task B2/B3 — vtodo ↔ canon JSON stages
    src/todo/vtodocanonstages.h
    src/todo/vtodocanonfields.h
    src/todo/tododomaindefinition.h
    src/todo/todostockshapes.h
    src/todo/todoplugin.h
    # B2C P3.c: Google Tasks backend (full-listing reads — Tasks has NO sync
    # tokens; O68 strip-at-create; idempotent deletes)
    src/todo/googletasksbackend.h
    # B2C P3.d: Microsoft Graph todoTask backend (expanded-listing reads,
    # nav-POST carrier writes; O66/O69/O73 wire truths)
    src/todo/graphtodotaskbackend.h
)
set(KALBURATOR_TODO_SOURCES
    src/todo/icalvtodoproperties.cpp
    src/todo/todocanonproperties.cpp
    src/todo/icalvtododiffer.cpp
    src/todo/todotxttransformation.cpp
    # Plan 3 Task B2/B3 — vtodo ↔ canon JSON stages
    src/todo/googletaskproperties.cpp
    src/todo/googletaskcanonstages.cpp
    src/todo/mstodotaskproperties.cpp
    src/todo/mstodotaskcanonstages.cpp
    # Plan 3 Task B2/B3 — vtodo ↔ canon JSON stages
    src/todo/vtodocanonstages.cpp
    src/todo/vtodocanonfields.cpp
    src/todo/tododomaindefinition.cpp
    src/todo/todostockshapes.cpp
    src/todo/todoplugin.cpp
    # B2C P3.c: Google Tasks backend
    src/todo/googletasksbackend.cpp
    # B2C P3.d: Microsoft Graph todoTask backend
    src/todo/graphtodotaskbackend.cpp
)

set(KALBURATOR_CONTACTS_HEADERS
    src/contacts/vcardproperties.h
    src/contacts/contactscanonproperties.h
    src/contacts/vcarddiffer.h
    src/contacts/vcardmerger.h
    src/contacts/vcard3to4transformation.h
    # Plan 3 Task A2/A3 — vcard4 ↔ canon JSON stages
    src/contacts/vcardcanonstages.h
    src/contacts/contactsdomaindefinition.h
    src/contacts/contactsstockshapes.h
    src/contacts/contactsplugin.h
    # Phase Ib Task 5: CardDAV contacts backend
    src/contacts/remotecontactsbackend.h
    # B2C P2.c: Microsoft Graph contacts backend (expanded-listing reads,
    # nav-POST carrier writes; O66/O70 wire truths)
    src/contacts/graphcontactsbackend.h
    # B2C P2.d: Google People contacts backend (sync-token walks with 410
    # self-heal; inline clientData carriers; mask-derived PATCH)
    src/contacts/googlepeoplebackend.h
    # Phase L.6: Akonadi contacts backend (gated on HAVE_AKONADI)
    src/contacts/akonadicontactsbackend.h
)
set(KALBURATOR_CONTACTS_SOURCES
    src/contacts/vcardproperties.cpp
    src/contacts/contactscanonproperties.cpp
    src/contacts/googlepersonproperties.cpp
    src/contacts/googlepersoncanonstages.cpp
    # EEE Phase 3 — ms-contact ⇄ contacts-canon edge
    src/contacts/mscontactproperties.cpp
    src/contacts/mscontactcanonstages.cpp
    src/contacts/vcarddiffer.cpp
    src/contacts/vcardmerger.cpp
    src/contacts/vcard3to4transformation.cpp
    # Plan 3 Task A2/A3 — vcard4 ↔ canon JSON stages
    src/contacts/vcardcanonstages.cpp
    src/contacts/contactsdomaindefinition.cpp
    src/contacts/contactsstockshapes.cpp
    src/contacts/contactsplugin.cpp
    # Phase Ib Task 5: CardDAV contacts backend
    src/contacts/remotecontactsbackend.cpp
    # B2C P2.c: Microsoft Graph contacts backend
    src/contacts/graphcontactsbackend.cpp
    # B2C P2.d: Google People contacts backend
    src/contacts/googlepeoplebackend.cpp
    # Phase L.6: Akonadi contacts backend (gated on HAVE_AKONADI)
    src/contacts/akonadicontactsbackend.cpp
)

set(KALBURATOR_NOTE_HEADERS
    src/note/noteproperties.h
    src/note/textdiffer.h
    src/note/textmerger.h
    src/note/notedomaindefinition.h
    src/note/noteplugin.h
    src/note/markdowncanonstages.h
    src/note/notestockshapes.h
)
set(KALBURATOR_NOTE_SOURCES
    src/note/noteproperties.cpp
    src/note/textdiffer.cpp
    src/note/textmerger.cpp
    src/note/notedomaindefinition.cpp
    src/note/noteplugin.cpp
    src/note/markdowncanonstages.cpp
    src/note/notestockshapes.cpp
)

set(KALBURATOR_OUTLINE_HEADERS
    src/outline/outlinenode.h
    src/outline/outlinecanonproperties.h
    src/outline/outlinediffer.h
    src/outline/outlinemerger.h
    src/outline/opmlcanonstages.h
    src/outline/orgcanonstages.h
    src/outline/outlinedomaindefinition.h
    src/outline/outlinestockshapes.h
    src/outline/outlineplugin.h
)
set(KALBURATOR_OUTLINE_SOURCES
    src/outline/outlinenode.cpp
    src/outline/outlinecanonproperties.cpp
    src/outline/outlinediffer.cpp
    src/outline/outlinemerger.cpp
    src/outline/opmlcanonstages.cpp
    src/outline/orgcanonstages.cpp
    src/outline/outlinedomaindefinition.cpp
    src/outline/outlinestockshapes.cpp
    src/outline/outlineplugin.cpp
)

# Phase G.8 — Universal sinks (RawFilesBackend, GenericSqliteBackend).
# Phase K.7.3 — UniversalStoragePlugin wrapping those backends.
set(KALBURATOR_SINKS_HEADERS
    src/universal/rawfilesbackend.h
    src/universal/markdownfilesbackend.h
    src/universal/genericsqlitebackend.h
    src/universal/universalstorageplugin.h
    src/universal/filteredcollectionbackend.h
    src/universal/kinddemuxbackend.h
)
set(KALBURATOR_SINKS_SOURCES
    src/universal/rawfilesbackend.cpp
    src/universal/markdownfilesbackend.cpp
    src/universal/genericsqlitebackend.cpp
    src/universal/universalstorageplugin.cpp
    src/universal/filteredcollectionbackend.cpp
    src/universal/kinddemuxbackend.cpp
)

# Phase K.7.1 — Qt plugin entry-point interface + manifest parser.
# Phase K.7.2 — PluginManager dependency DAG resolver.
# Phase K.7.3 — Bootstrap registerStockPlugins().
set(KALBURATOR_PLUGIN_HEADERS
    src/plugin/plugin.h
    src/plugin/manifest.h
    src/plugin/pluginloaderror.h
    src/plugin/pluginmanager.h
    src/plugin/stock_plugins.h
    src/plugin/caldavproviderplugin.h
    src/plugin/carddavproviderplugin.h
    # Phase M: Multi-protocol DAV provider plugin
    src/plugin/multiprotocoldavproviderplugin.h
    # Phase L.3: Akonadi plugin manifest (gated on HAVE_AKONADI)
    src/plugin/akonadiproviderplugin.h
)
set(KALBURATOR_PLUGIN_SOURCES
    src/plugin/manifest.cpp
    src/plugin/pluginmanager.cpp
    src/plugin/stock_plugins.cpp
    src/plugin/caldavproviderplugin.cpp
    src/plugin/carddavproviderplugin.cpp
    # Phase M: Multi-protocol DAV provider plugin
    src/plugin/multiprotocoldavproviderplugin.cpp
    # Phase L.3: Akonadi plugin manifest (gated on HAVE_AKONADI)
    src/plugin/akonadiproviderplugin.cpp
)

# Phase H — provider abstraction
# Phase Ib — CardDAV transport (carddavcapabilitydiscovery)
set(KALBURATOR_PROVIDER_CORE_HEADERS
    src/sync/backendcontribution.h
    src/sync/caldavbackendcontribution.h
    src/sync/carddavbackendcontribution.h
    src/sync/iprovider.h
    # Uniform config-widget contract (widget <-> provider bridge)
    src/sync/iproviderconfigwidget.h
    src/sync/providermanager.h
    src/sync/caldavprovider.h
    src/sync/caldavconfigwidget.h
    src/sync/caldavcapabilitydiscovery.h
    src/sync/carddavcapabilitydiscovery.h
    # VP.a (vtodo-parity W8): unified capability/trait query API (public)
    src/sync/calendarcapabilities.h
    src/sync/carddavprovider.h
    src/sync/carddavconfigwidget.h
    # Phase K.4: domain-neutral sync-backend base
    src/sync/syncbackendbase.h
    src/sync/writeoperation.h
    src/sync/writerbatch.h
    # Neutral backend capability mixin (Plan 9: moved from src/backend/)
    src/sync/changedetection.h
    # E11: generalized async-blocking rendezvous (audit B7 / FINDINGS O39)
    src/sync/blockonasync.h
    # VP.c-step-1a (vtodo-parity): detached-exception record identity
    src/sync/recordidentity.h
    # Phase K.7.1: account-neutral IProvider adapter
    src/sync/neutralprovider.h
    # Phase L.1: Akonadi first-class provider (gated on HAVE_AKONADI)
    src/sync/akonadiprovider.h
    # Phase L.2: Akonadi backend contribution factory (gated on HAVE_AKONADI)
    src/sync/akonadibackendcontribution.h
    # Phase L.8: Akonadi config widget (gated on HAVE_AKONADI)
    src/sync/akonadiconfigwidget.h
    # Phase M: Multi-protocol DAV provider
    src/sync/multiprotocoldavprovider.h
    # Phase M.7: Multi-protocol DAV backend contribution
    src/sync/multiprotocoldavbackendcontribution.h
    # Phase M.6: Multi-protocol DAV config widget
    src/sync/multiprotocoldavconfigwidget.h
    # Phase 5 step 1: provider lifecycle coordinator
    src/sync/providerlifecycle.h
    # Phase 5 step 2: sync execution coordinator
    src/sync/syncruncoordinator.h
    # Akonadi change-detection digest helper (pure; unconditional)
    src/sync/akonadirevisiondigest.h
    # Akonadi persistent per-collection revision store (pure; unconditional)
    src/sync/akonadirevisionstore.h
    # Sync-topology promotion (2026-05-27): pure (logical calendars + topology)
    # -> SyncMapping list generator. No provider/engine state.
    src/sync/syncmappinggenerator.h
)
set(KALBURATOR_PROVIDER_CORE_SOURCES
    src/sync/providermanager.cpp
    src/sync/caldavprovider.cpp
    src/sync/caldavconfigwidget.cpp
    src/sync/caldavcapabilitydiscovery.cpp
    src/sync/carddavcapabilitydiscovery.cpp
    src/sync/calendarcapabilities.cpp
    src/sync/carddavprovider.cpp
    src/sync/carddavconfigwidget.cpp
    # Phase K.4: domain-neutral sync-backend base
    src/sync/syncbackendbase.cpp
    # Phase K.7.1: account-neutral IProvider adapter
    src/sync/neutralprovider.cpp
    # Phase L.1: Akonadi first-class provider (gated on HAVE_AKONADI)
    src/sync/akonadiprovider.cpp
    # Phase L.2: Akonadi backend contribution factory (gated on HAVE_AKONADI)
    src/sync/akonadibackendcontribution.cpp
    # Phase L.8: Akonadi config widget (gated on HAVE_AKONADI)
    src/sync/akonadiconfigwidget.cpp
    # Phase M: Multi-protocol DAV provider
    src/sync/multiprotocoldavprovider.cpp
    # Phase M.6: Multi-protocol DAV config widget
    src/sync/multiprotocoldavconfigwidget.cpp
    # Phase 5 step 1: provider lifecycle coordinator
    src/sync/providerlifecycle.cpp
    # Phase 5 step 2: sync execution coordinator
    src/sync/syncruncoordinator.cpp
    # Akonadi change-detection digest helper (pure; unconditional)
    src/sync/akonadirevisiondigest.cpp
    # Akonadi persistent per-collection revision store (pure; unconditional)
    src/sync/akonadirevisionstore.cpp
    # Sync-topology promotion (2026-05-27): pure generator (see header)
    src/sync/syncmappinggenerator.cpp
)

# Phase M.9–M.11: Library UI widgets
set(KALBURATOR_UI_HEADERS
    src/ui/collectionpickerwidget.h
    src/ui/accountslistwidget.h
    src/ui/providerconfigdialog.h
)
set(KALBURATOR_UI_SOURCES
    src/ui/collectionpickerwidget.cpp
    src/ui/accountslistwidget.cpp
    src/ui/providerconfigdialog.cpp
)

# --- OrgGrove (Org-mode parser/serializer for the outline domain) -----------
# Consumers that embed libkalburator via add_subdirectory can point
# KALBURATOR_ORGGROVE_SOURCE_DIR at a local checkout to avoid network access.
set(KALBURATOR_ORGGROVE_SOURCE_DIR "" CACHE PATH
    "Optional local OrgGrove checkout (overrides FetchContent)")
set(KALBURATOR_ORGGROVE_GIT_TAG "2901bff8197cc704421e9b65c68a9c3817fa1875" CACHE STRING
    "OrgGrove commit to fetch when SOURCE_DIR is unset")

# Only pull OrgGrove when the outline org peer is enabled — it is the sole user.
if(KALBURATOR_HAVE_OUTLINE_ORG)
    if(KALBURATOR_ORGGROVE_SOURCE_DIR)
        add_subdirectory(${KALBURATOR_ORGGROVE_SOURCE_DIR} orggrove EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(OrgGrove
            GIT_REPOSITORY https://github.com/clintonthegeek/OrgGrove.git
            GIT_TAG        ${KALBURATOR_ORGGROVE_GIT_TAG})
        FetchContent_MakeAvailable(OrgGrove)
    endif()
endif()

# Aggregate
set(KALBURATOR_SYNC_HEADERS
    ${KALBURATOR_CALENDAR_HEADERS}
    ${KALBURATOR_CONFLICT_HEADERS}
    ${KALBURATOR_DIFF_HEADERS}
    ${KALBURATOR_JOURNAL_HEADERS}
    ${KALBURATOR_STORAGE_HEADERS}
    ${KALBURATOR_IDENTITY_HEADERS}
    ${KALBURATOR_DISCOVERY_HEADERS}
    ${KALBURATOR_BLOB_HEADERS}
    ${KALBURATOR_ENGINE_HEADERS}
    ${KALBURATOR_SHAPE_HEADERS}
    ${KALBURATOR_TODO_HEADERS}
    ${KALBURATOR_CONTACTS_HEADERS}
    ${KALBURATOR_NOTE_HEADERS}
    ${KALBURATOR_OUTLINE_HEADERS}
    ${KALBURATOR_SINKS_HEADERS}
    ${KALBURATOR_PROVIDER_CORE_HEADERS}
    ${KALBURATOR_PLUGIN_HEADERS}
    ${KALBURATOR_UI_HEADERS}
)
set(KALBURATOR_SYNC_SOURCES
    ${KALBURATOR_CALENDAR_SOURCES}
    ${KALBURATOR_CONFLICT_SOURCES}
    ${KALBURATOR_DIFF_SOURCES}
    ${KALBURATOR_JOURNAL_SOURCES}
    ${KALBURATOR_STORAGE_SOURCES}
    ${KALBURATOR_IDENTITY_SOURCES}
    ${KALBURATOR_DISCOVERY_SOURCES}
    ${KALBURATOR_BLOB_SOURCES}
    ${KALBURATOR_ENGINE_SOURCES}
    ${KALBURATOR_SHAPE_SOURCES}
    ${KALBURATOR_TODO_SOURCES}
    ${KALBURATOR_CONTACTS_SOURCES}
    ${KALBURATOR_NOTE_SOURCES}
    ${KALBURATOR_OUTLINE_SOURCES}
    ${KALBURATOR_SINKS_SOURCES}
    ${KALBURATOR_PROVIDER_CORE_SOURCES}
    ${KALBURATOR_PLUGIN_SOURCES}
    ${KALBURATOR_UI_SOURCES}
)

if(NOT KALBURATOR_HAVE_ORG_IO)
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "orgbackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "orgbackend\\.cpp$")
endif()

if(NOT KALBURATOR_HAVE_OUTLINE_ORG)
    # Drop the org<->canon transcoder (the only OrgGrove-dependent outline file).
    # canon + OPML peers remain; outlinestockshapes.cpp #ifdef-guards the org edges.
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "orgcanonstages\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "orgcanonstages\\.cpp$")
endif()

if(NOT KALBURATOR_HAVE_AKONADI)
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadibackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadibackend\\.cpp$")
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadiprovider\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadiprovider\\.cpp$")
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadibackendcontribution\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadibackendcontribution\\.cpp$")
    # Phase L.3: plugin manifest
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadiproviderplugin\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadiproviderplugin\\.cpp$")
    # Phase L.6: Akonadi contacts backend
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadicontactsbackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadicontactsbackend\\.cpp$")
    # Phase L.8: Akonadi config widget
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadiconfigwidget\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadiconfigwidget\\.cpp$")
endif()

add_library(kalburator STATIC
    ${KALBURATOR_SYNC_HEADERS}
    ${KALBURATOR_SYNC_SOURCES}
    ${KALBURATOR_ENGINE_PRIVATE_HEADERS}
)
add_library(Kalburator::Sync ALIAS kalburator)

target_include_directories(kalburator
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/calendar>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/conflict>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/diff>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/storage>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/identity>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/journal>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/discovery>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/universal>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/blob>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/engine>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/shape>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/todo>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/contacts>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/note>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/outline>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/sync>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/graph>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/net>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/google>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/plugin>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/ui>
)

target_link_libraries(kalburator
    PUBLIC
        kalburator-typesupport
        kalburator-types
        Qt6::Sql
        Qt6::Network
        Qt6::Xml
        Qt6::Widgets
        Qt6::Concurrent
        KF6::DAV
        KF6::KIOCore
        KF6::Holidays
        KF6::Contacts
        KF6::ConfigCore
)

if(KALBURATOR_HAVE_OUTLINE_ORG)
    target_link_libraries(kalburator PRIVATE OrgGrove::OrgGrove)
    # PUBLIC so library sources, tests, and downstream consumers can #ifdef on it.
    target_compile_definitions(kalburator PUBLIC KALBURATOR_HAVE_OUTLINE_ORG)
endif()

if(KALBURATOR_HAVE_AKONADI)
    find_package(KPim6Akonadi REQUIRED)
    target_link_libraries(kalburator PUBLIC KPim6::AkonadiCore)
    target_compile_definitions(kalburator PUBLIC HAVE_AKONADI)
endif()

set_target_properties(kalburator PROPERTIES POSITION_INDEPENDENT_CODE ON)

message(STATUS "libkalburator: configured "
        "(org-io=${KALBURATOR_HAVE_ORG_IO} "
        "outline-org=${KALBURATOR_HAVE_OUTLINE_ORG} "
        "akonadi=${KALBURATOR_HAVE_AKONADI})")

# -- Kalburator::Widgets (G.10 Task 83) ------------------------------------
# Optional UI component — depends on Qt6::Widgets. Excluded from
# add_subdirectory consumers that don't want widget deps.
if(PROJECT_IS_TOP_LEVEL)
    option(KALBURATOR_BUILD_WIDGETS "Build Kalburator::Widgets (requires Qt6::Widgets)" ON)
else()
    option(KALBURATOR_BUILD_WIDGETS "Build Kalburator::Widgets (requires Qt6::Widgets)" OFF)
endif()

if(KALBURATOR_BUILD_WIDGETS)
    add_subdirectory(widgets)
endif()

# -- Library tests ----------------------------------------------------------
# Gated so that add_subdirectory consumers (PlanStan, WP) don't pick up
# libkalburator's own test executables. Top-level standalone builds get
# them on by default.
if(PROJECT_IS_TOP_LEVEL)
    option(KALBURATOR_BUILD_TESTS "Build libkalburator's own tests" ON)
else()
    option(KALBURATOR_BUILD_TESTS "Build libkalburator's own tests" OFF)
endif()

if(KALBURATOR_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

option(KALBURATOR_BUILD_EXAMPLES "Build example consumers (reference_consumer)" ON)
if(KALBURATOR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# -- GraphCLI experiment tool -------------------------------------------------
# Standalone Microsoft Graph protocol lab (device-code auth + /me CRUD +
# payload capture for the vendor-convergence corpus). Qt6 Core+Network only;
# does not link the library. Runtime credentials live in gitignored msgraph/.
if(PROJECT_IS_TOP_LEVEL)
    option(KALBURATOR_BUILD_GRAPHCLI "Build the GraphCLI Microsoft Graph experiment tool" ON)
else()
    option(KALBURATOR_BUILD_GRAPHCLI "Build the GraphCLI Microsoft Graph experiment tool" OFF)
endif()

# -- GoogleCLI experiment tool -------------------------------------------------
# Standalone Google Calendar protocol lab (loopback OAuth code flow + Calendar
# v3 verbs + payload capture for the vendor-convergence corpus). Qt6
# Core+Network only; does not link the library. Runtime credentials live in
# gitignored google/.
if(PROJECT_IS_TOP_LEVEL)
    option(KALBURATOR_BUILD_GOOGLECLI "Build the GoogleCLI Google Calendar experiment tool" ON)
else()
    option(KALBURATOR_BUILD_GOOGLECLI "Build the GoogleCLI Google Calendar experiment tool" OFF)
endif()

if(KALBURATOR_BUILD_GRAPHCLI OR KALBURATOR_BUILD_GOOGLECLI)
    add_subdirectory(tools)
endif()
