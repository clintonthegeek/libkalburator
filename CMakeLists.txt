cmake_minimum_required(VERSION 3.19)
project(Kalburator VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# libkalburator — a reusable sync substrate for Qt6/KF6 calendar apps.
# Two targets:
#   Kalburator::Types — minimal type vocabulary (BackendConfiguration,
#     LogicalCalendar, ICalendarCollection, etc.). Depends only on Qt6::Core,
#     Qt6::Gui, and KF6::CalendarCore. Link this from host foundation libs
#     that need the shared types but not the sync engine.
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

# --- Kalburator::Sync -------------------------------------------------------
# Sync sources are layered across sub-directories per docs/phase0/05-repo-strategy.md:
#   calendar/   — backends, coordination, CRUD items, ISyncHost
#   conflict/   — conflict framework (incl. Kalburator::Sync::QSyncCore files)
#   transcoding/— property/RRULE transcoding + diffs
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
    src/calendar/caldavcapabilitydiscovery.h
    src/calendar/calendarmanager.h
    src/calendar/createincidenceitem.h
    src/calendar/decsyncactivecontroller.h
    src/calendar/decsyncbackend.h
    src/calendar/decsynccontrollerstore.h
    src/calendar/decsyncgarbagecollector.h
    src/calendar/decsynclib.h
    src/calendar/deleteincidenceitem.h
    src/calendar/discoveredcalendar.h
    src/calendar/holidaysubscriptionbackend.h
    src/calendar/iconflictpresenter.h
    src/calendar/iconflictresolver.h
    src/calendar/icsfeedfetcher.h
    src/calendar/incidencesyncadapter.h
    src/calendar/isynchost.h
    src/calendar/isyncrecord.h
    src/calendar/localbackend.h
    src/calendar/logicalcalendarbuilder.h
    src/calendar/mockbackend.h
    src/calendar/orgbackend.h
    src/calendar/remotecalendarbackend.h
    src/calendar/subscriptionbackend.h
    src/calendar/syncbackend.h
    src/calendar/syncconflictstore.h
    src/calendar/syncoperation.h
    src/calendar/synctesthooks.h
    src/calendar/synctransaction.h
    src/calendar/synctransactionitem.h
    src/calendar/updateincidenceitem.h
    # Phase G.2 — calendar plugin
    src/calendar/icalproperties.h
    src/calendar/icalrecorddiffer.h
    src/calendar/icalrecordmerger.h
    src/calendar/calendardomaindefinition.h
    src/calendar/calendarstockshapes.h
    src/calendar/calendardomainoperations.h
    src/calendar/calendarplugin.h
    src/calendar/calendarplugin_writer.h
)
set(KALBURATOR_CALENDAR_SOURCES
    src/calendar/akonadibackend.cpp
    src/calendar/backendcapabilities.cpp
    src/sync/backendregistry.cpp
    src/calendar/caldavcapabilitydiscovery.cpp
    src/calendar/calendarmanager.cpp
    src/calendar/createincidenceitem.cpp
    src/calendar/decsyncactivecontroller.cpp
    src/calendar/decsyncbackend.cpp
    src/calendar/decsynccontrollerstore.cpp
    src/calendar/decsyncgarbagecollector.cpp
    src/calendar/decsynclib.cpp
    src/calendar/deleteincidenceitem.cpp
    src/calendar/holidaysubscriptionbackend.cpp
    src/calendar/icsfeedfetcher.cpp
    src/calendar/localbackend.cpp
    src/calendar/logicalcalendarbuilder.cpp
    src/calendar/mockbackend.cpp
    src/calendar/orgbackend.cpp
    src/calendar/remotecalendarbackend.cpp
    src/calendar/subscriptionbackend.cpp
    src/calendar/syncbackend.cpp
    src/calendar/syncconflictstore.cpp
    src/calendar/syncoperation.cpp
    src/calendar/synctransaction.cpp
    src/calendar/synctransactionitem.cpp
    src/calendar/updateincidenceitem.cpp
    # Phase G.2 — calendar plugin
    src/calendar/icalproperties.cpp
    src/calendar/icalrecorddiffer.cpp
    src/calendar/icalrecordmerger.cpp
    src/calendar/calendardomaindefinition.cpp
    src/calendar/calendarstockshapes.cpp
    src/calendar/calendardomainoperations.cpp
    src/calendar/calendarplugin.cpp
    src/calendar/calendarplugin_writer.cpp
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

set(KALBURATOR_TRANSCODING_HEADERS
    src/transcoding/incidencediff.h
    src/transcoding/propertytranscoder.h
    src/transcoding/rruletranscoder.h
    src/transcoding/syncdiff.h
    src/transcoding/transcodingplan.h
    src/transcoding/transcodingregistry.h
    src/transcoding/transcodingrouter.h
)
set(KALBURATOR_TRANSCODING_SOURCES
    src/transcoding/incidencediff.cpp
    src/transcoding/propertytranscoder.cpp
    src/transcoding/rruletranscoder.cpp
    src/transcoding/syncdiff.cpp
    src/transcoding/transcodingplan.cpp
    src/transcoding/transcodingregistry.cpp
    src/transcoding/transcodingrouter.cpp
)

set(KALBURATOR_JOURNAL_HEADERS
    src/journal/asyncfilewriter.h
    # baselinestore.h moved to src/storage/ in K.5; old file is stale/unused
    src/journal/calendarjournal.h
)
set(KALBURATOR_JOURNAL_SOURCES
    src/journal/asyncfilewriter.cpp
    src/journal/calendarjournal.cpp
)

# Phase K.5 — domain-neutral storage layer.
# BlobBaselineStore (renamed BaselineStore) and IDMappingStore moved here
# from src/journal/. Forwarding shims at old journal/ locations stay until
# Task 13 (PlanStan + WildPalms fully cut over).
set(KALBURATOR_STORAGE_HEADERS
    src/storage/baselinestore.h
    src/storage/idmappingstore.h
)
set(KALBURATOR_STORAGE_SOURCES
    src/storage/baselinestore.cpp
    src/storage/idmappingstore.cpp
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

# Phase K.1 — backend capability interfaces.
# Pure-virtual non-QObject side-interfaces opted into by concrete
# backends. Engine consumes via dynamic_cast in K.2; backends declare
# them now for forward compatibility (no behavior change in K.1).
set(KALBURATOR_BACKEND_HEADERS
    src/backend/changedetection.h
    src/backend/resourcelinearization.h
)
# (No sources — headers-only interfaces.)

set(KALBURATOR_ENGINE_HEADERS
    src/engine/enginediff.h
    src/engine/imassdeleteguard.h
    src/engine/mappingscheduler.h
    src/engine/perrecorddiff.h
    src/engine/propertydiff.h
    src/engine/syncengine.h
    src/engine/syncenginefuture.h
)
set(KALBURATOR_ENGINE_SOURCES
    src/engine/enginediff.cpp
    src/engine/mappingscheduler.cpp
    src/engine/perrecorddiff.cpp
    src/engine/propertydiff.cpp
    src/engine/syncengine.cpp
    src/engine/syncenginefuture.cpp
)

# Phase G — shape-pipeline architecture primitives.
# Foundational vocabulary: Shape, PropertyCatalogue, LossProfile,
# TransformationEdge/Pipeline, TransformationRegistry, DomainPlugin.
# No domain edges registered here; calendar plugin lands in G.2,
# new domains (contacts/memo/todo) in G.5/G.10.
set(KALBURATOR_SHAPE_HEADERS
    src/shape/shape.h
    src/shape/propertycatalogue.h
    src/shape/lossprofile.h
    src/shape/transformationedge.h
    src/shape/pipeline.h
    src/shape/transformationregistry.h
    src/shape/canonicalrecord.h
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
    src/shape/lossprofile.cpp
    src/shape/transformationedge.cpp
    src/shape/pipeline.cpp
    src/shape/transformationregistry.cpp
    src/shape/domainregistry.cpp
    src/shape/defaultblobwriter.cpp
    # K.7.1 — split plugin interface
    src/shape/domainoperationsregistry.cpp
    src/shape/shaperegistries.cpp
    src/shape/canonenvelope.cpp
    # Plan 3 — canon JSON differ + merger
    src/shape/canonjsondiffer.cpp
    src/shape/canonjsonmerger.cpp
)

# Phase G.5 — new domain plugins (todo, contacts, memo).
set(KALBURATOR_TODO_HEADERS
    src/todo/icalvtodoproperties.h
    src/todo/icalvtododiffer.h
    src/todo/icalvtodomerger.h
    src/todo/todotxttransformation.h
    src/todo/tododomaindefinition.h
    src/todo/todostockshapes.h
    src/todo/todoplugin.h
)
set(KALBURATOR_TODO_SOURCES
    src/todo/icalvtodoproperties.cpp
    src/todo/icalvtododiffer.cpp
    src/todo/icalvtodomerger.cpp
    src/todo/todotxttransformation.cpp
    src/todo/tododomaindefinition.cpp
    src/todo/todostockshapes.cpp
    src/todo/todoplugin.cpp
)

set(KALBURATOR_CONTACTS_HEADERS
    src/contacts/vcardproperties.h
    src/contacts/contactscanonproperties.h
    src/contacts/vcarddiffer.h
    src/contacts/vcardmerger.h
    src/contacts/vcard3to4transformation.h
    src/contacts/contactsdomaindefinition.h
    src/contacts/contactsstockshapes.h
    src/contacts/contactsplugin.h
    # Phase Ib Task 5: CardDAV contacts backend
    src/contacts/remotecontactsbackend.h
    # Phase L.6: Akonadi contacts backend (gated on HAVE_AKONADI)
    src/contacts/akonadicontactsbackend.h
)
set(KALBURATOR_CONTACTS_SOURCES
    src/contacts/vcardproperties.cpp
    src/contacts/contactscanonproperties.cpp
    src/contacts/vcarddiffer.cpp
    src/contacts/vcardmerger.cpp
    src/contacts/vcard3to4transformation.cpp
    src/contacts/contactsdomaindefinition.cpp
    src/contacts/contactsstockshapes.cpp
    src/contacts/contactsplugin.cpp
    # Phase Ib Task 5: CardDAV contacts backend
    src/contacts/remotecontactsbackend.cpp
    # Phase L.6: Akonadi contacts backend (gated on HAVE_AKONADI)
    src/contacts/akonadicontactsbackend.cpp
)

set(KALBURATOR_MEMO_HEADERS
    src/memo/memoproperties.h
    src/memo/textdiffer.h
    src/memo/textmerger.h
    src/memo/memodomaindefinition.h
    src/memo/memoplugin.h
)
set(KALBURATOR_MEMO_SOURCES
    src/memo/memoproperties.cpp
    src/memo/textdiffer.cpp
    src/memo/textmerger.cpp
    src/memo/memodomaindefinition.cpp
    src/memo/memoplugin.cpp
)

# Phase G.8 — Universal sinks (RawFilesBackend, GenericSqliteBackend).
# Phase K.7.3 — UniversalStoragePlugin wrapping those backends.
set(KALBURATOR_SINKS_HEADERS
    src/universal/rawfilesbackend.h
    src/universal/genericsqlitebackend.h
    src/universal/universalstorageplugin.h
)
set(KALBURATOR_SINKS_SOURCES
    src/universal/rawfilesbackend.cpp
    src/universal/genericsqlitebackend.cpp
    src/universal/universalstorageplugin.cpp
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
    src/sync/providermanager.h
    src/sync/caldavprovider.h
    src/sync/caldavconfigwidget.h
    src/sync/carddavcapabilitydiscovery.h
    src/sync/carddavprovider.h
    # Phase K.4: domain-neutral sync-backend base
    src/sync/syncbackendbase.h
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
)
set(KALBURATOR_PROVIDER_CORE_SOURCES
    src/sync/providermanager.cpp
    src/sync/caldavprovider.cpp
    src/sync/caldavconfigwidget.cpp
    src/sync/carddavcapabilitydiscovery.cpp
    src/sync/carddavprovider.cpp
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

# Aggregate
set(KALBURATOR_SYNC_HEADERS
    ${KALBURATOR_CALENDAR_HEADERS}
    ${KALBURATOR_CONFLICT_HEADERS}
    ${KALBURATOR_TRANSCODING_HEADERS}
    ${KALBURATOR_JOURNAL_HEADERS}
    ${KALBURATOR_STORAGE_HEADERS}
    ${KALBURATOR_DISCOVERY_HEADERS}
    ${KALBURATOR_BLOB_HEADERS}
    ${KALBURATOR_BACKEND_HEADERS}
    ${KALBURATOR_ENGINE_HEADERS}
    ${KALBURATOR_SHAPE_HEADERS}
    ${KALBURATOR_TODO_HEADERS}
    ${KALBURATOR_CONTACTS_HEADERS}
    ${KALBURATOR_MEMO_HEADERS}
    ${KALBURATOR_SINKS_HEADERS}
    ${KALBURATOR_PROVIDER_CORE_HEADERS}
    ${KALBURATOR_PLUGIN_HEADERS}
    ${KALBURATOR_UI_HEADERS}
)
set(KALBURATOR_SYNC_SOURCES
    ${KALBURATOR_CALENDAR_SOURCES}
    ${KALBURATOR_CONFLICT_SOURCES}
    ${KALBURATOR_TRANSCODING_SOURCES}
    ${KALBURATOR_JOURNAL_SOURCES}
    ${KALBURATOR_STORAGE_SOURCES}
    ${KALBURATOR_DISCOVERY_SOURCES}
    ${KALBURATOR_BLOB_SOURCES}
    ${KALBURATOR_ENGINE_SOURCES}
    ${KALBURATOR_SHAPE_SOURCES}
    ${KALBURATOR_TODO_SOURCES}
    ${KALBURATOR_CONTACTS_SOURCES}
    ${KALBURATOR_MEMO_SOURCES}
    ${KALBURATOR_SINKS_SOURCES}
    ${KALBURATOR_PROVIDER_CORE_SOURCES}
    ${KALBURATOR_PLUGIN_SOURCES}
    ${KALBURATOR_UI_SOURCES}
)

if(NOT KALBURATOR_HAVE_ORG_IO)
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "orgbackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "orgbackend\\.cpp$")
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
)
add_library(Kalburator::Sync ALIAS kalburator)

target_include_directories(kalburator
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/calendar>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/conflict>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/transcoding>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/storage>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/journal>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/discovery>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/universal>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/blob>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/engine>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/shape>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/todo>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/contacts>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/memo>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/sync>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/backend>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/plugin>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/ui>
)

target_link_libraries(kalburator
    PUBLIC
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

if(KALBURATOR_HAVE_AKONADI)
    find_package(KPim6Akonadi REQUIRED)
    target_link_libraries(kalburator PUBLIC KPim6::AkonadiCore)
    target_compile_definitions(kalburator PUBLIC HAVE_AKONADI)
endif()

set_target_properties(kalburator PROPERTIES POSITION_INDEPENDENT_CODE ON)

message(STATUS "libkalburator: configured "
        "(org-io=${KALBURATOR_HAVE_ORG_IO} "
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
