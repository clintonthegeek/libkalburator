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
set(KALBURATOR_SYNC_SUBDIRS calendar conflict transcoding journal discovery blob)
set(KALBURATOR_SYNC_HEADERS "")
set(KALBURATOR_SYNC_SOURCES "")
foreach(_subdir IN LISTS KALBURATOR_SYNC_SUBDIRS)
    file(GLOB _subdir_headers CONFIGURE_DEPENDS src/${_subdir}/*.h)
    file(GLOB _subdir_sources CONFIGURE_DEPENDS src/${_subdir}/*.cpp)
    list(APPEND KALBURATOR_SYNC_HEADERS ${_subdir_headers})
    list(APPEND KALBURATOR_SYNC_SOURCES ${_subdir_sources})
endforeach()

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
