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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Sql Xml Network Widgets)
find_package(KF6CalendarCore REQUIRED)
find_package(KF6DAV REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(KF6Holidays REQUIRED)

# Optional org-io support for OrgBackend. When ON, host must provide the
# planstan-org-io target via PUBLIC linkage from outside; standalone builds
# default OFF and exclude orgbackend files.
set(KALBURATOR_HAVE_ORG_IO OFF CACHE BOOL "Build with PlanStan org-io (for OrgBackend)")

# Optional Akonadi support; follows host's HAVE_AKONADI setting in-tree.
set(KALBURATOR_HAVE_AKONADI OFF CACHE BOOL "Build with KPim6 Akonadi support")

# --- Kalburator::Types ------------------------------------------------------
file(GLOB KALBURATOR_TYPE_HEADERS CONFIGURE_DEPENDS src/types/*.h)
file(GLOB KALBURATOR_TYPE_SOURCES CONFIGURE_DEPENDS src/types/*.cpp)

add_library(kalburator-types STATIC
    ${KALBURATOR_TYPE_HEADERS}
    ${KALBURATOR_TYPE_SOURCES}
)
add_library(Kalburator::Types ALIAS kalburator-types)

target_include_directories(kalburator-types
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/types>
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
    src/calendar/backendregistry.h
    src/calendar/caldavcapabilitydiscovery.h
    src/calendar/calendarbaselinestore.h
    src/calendar/calendardomainadapter.h
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
    src/calendar/remotebackend.h
    src/calendar/subscriptionbackend.h
    src/calendar/syncbackend.h
    src/calendar/syncconflictstore.h
    src/calendar/synccoordinator.h
    src/calendar/syncoperation.h
    src/calendar/synctesthooks.h
    src/calendar/synctransaction.h
    src/calendar/synctransactionitem.h
    src/calendar/syncworker.h
    src/calendar/updateincidenceitem.h
)
set(KALBURATOR_CALENDAR_SOURCES
    src/calendar/akonadibackend.cpp
    src/calendar/backendcapabilities.cpp
    src/calendar/backendregistry.cpp
    src/calendar/caldavcapabilitydiscovery.cpp
    src/calendar/calendarbaselinestore.cpp
    src/calendar/calendardomainadapter.cpp
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
    src/calendar/remotebackend.cpp
    src/calendar/subscriptionbackend.cpp
    src/calendar/syncbackend.cpp
    src/calendar/syncconflictstore.cpp
    src/calendar/synccoordinator.cpp
    src/calendar/syncoperation.cpp
    src/calendar/synctransaction.cpp
    src/calendar/synctransactionitem.cpp
    src/calendar/syncworker.cpp
    src/calendar/updateincidenceitem.cpp
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
    src/journal/baselinestore.h
    src/journal/blobbaselinestore.h
    src/journal/calendarjournal.h
    src/journal/idmappingstore.h
)
set(KALBURATOR_JOURNAL_SOURCES
    src/journal/asyncfilewriter.cpp
    src/journal/baselinestore.cpp
    src/journal/blobbaselinestore.cpp
    src/journal/calendarjournal.cpp
    src/journal/idmappingstore.cpp
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
    src/blob/blobdomainadapter.h
    src/blob/blobsyncengine.h
    src/blob/iblobbackend.h
    src/blob/localblobbackend.h
    src/blob/mockblobbackend.h
)
set(KALBURATOR_BLOB_SOURCES
    src/blob/blobdomainadapter.cpp
    src/blob/blobsyncengine.cpp
    src/blob/iblobbackend.cpp
    src/blob/localblobbackend.cpp
    src/blob/mockblobbackend.cpp
)

# Phase F1 — unified engine + IDomainAdapter interface.
set(KALBURATOR_ENGINE_HEADERS
    src/engine/enginediff.h
    src/engine/idomainadapter.h
)
set(KALBURATOR_ENGINE_SOURCES
    src/engine/enginediff.cpp
)

# Aggregate
set(KALBURATOR_SYNC_HEADERS
    ${KALBURATOR_CALENDAR_HEADERS}
    ${KALBURATOR_CONFLICT_HEADERS}
    ${KALBURATOR_TRANSCODING_HEADERS}
    ${KALBURATOR_JOURNAL_HEADERS}
    ${KALBURATOR_DISCOVERY_HEADERS}
    ${KALBURATOR_BLOB_HEADERS}
    ${KALBURATOR_ENGINE_HEADERS}
)
set(KALBURATOR_SYNC_SOURCES
    ${KALBURATOR_CALENDAR_SOURCES}
    ${KALBURATOR_CONFLICT_SOURCES}
    ${KALBURATOR_TRANSCODING_SOURCES}
    ${KALBURATOR_JOURNAL_SOURCES}
    ${KALBURATOR_DISCOVERY_SOURCES}
    ${KALBURATOR_BLOB_SOURCES}
    ${KALBURATOR_ENGINE_SOURCES}
)

if(NOT KALBURATOR_HAVE_ORG_IO)
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "orgbackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "orgbackend\\.cpp$")
endif()

if(NOT KALBURATOR_HAVE_AKONADI)
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadibackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadibackend\\.cpp$")
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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/journal>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/discovery>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/blob>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/engine>
)

target_link_libraries(kalburator
    PUBLIC
        kalburator-types
        Qt6::Sql
        Qt6::Network
        Qt6::Xml
        Qt6::Widgets
        KF6::DAV
        KF6::KIOCore
        KF6::Holidays
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
