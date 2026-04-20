cmake_minimum_required(VERSION 3.19)
project(Kalburator VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Phase 3a scaffold — a single-target build of libkalburator's sync module.
# Not yet layered (blob vs calendar), not yet namespaced (Kalburator::Sync::*),
# not yet feature-gated (KALBURATOR_SYNCTHING off, HAVE_AKONADI follows parent).
# Phase 3b integrates this into PlanStan via FetchContent; later phases layer,
# namespace, and migrate Wild Palms.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Sql Xml Network Widgets)
find_package(KF6CalendarCore REQUIRED)
find_package(KF6DAV REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(KF6Holidays REQUIRED)

# For in-tree builds where a host app provides PlanStan's org-io, skip
# org-backend. For standalone builds we require PlanStan's org-io library
# to be installed or the org backend source absent. Current scaffold
# assumes absence and excludes orgbackend.
set(KALBURATOR_HAVE_ORG_IO OFF CACHE BOOL "Build with PlanStan org-io (for OrgBackend)")

# When consumed in-tree by a host (e.g. PlanStan) that already supplies
# the underlying type headers (BackendConfiguration, ICalendarCollection,
# CalendarMetadataManager, ...), set KALBURATOR_PROVIDE_TYPES=OFF and have
# the host link kalburator PUBLIC against its own type-providing targets.
# Standalone builds (default) provide these types from src/types/.
set(KALBURATOR_PROVIDE_TYPES ON CACHE BOOL
    "Build libkalburator's own copies of underlying types (BackendConfiguration, ICalendarCollection, etc.). \
Turn OFF when consumed in-tree by a host that provides these types itself.")

file(GLOB KALBURATOR_SYNC_HEADERS CONFIGURE_DEPENDS src/sync/*.h)
file(GLOB KALBURATOR_SYNC_SOURCES CONFIGURE_DEPENDS src/sync/*.cpp)
if(KALBURATOR_PROVIDE_TYPES)
    file(GLOB KALBURATOR_TYPE_HEADERS CONFIGURE_DEPENDS src/types/*.h)
    file(GLOB KALBURATOR_TYPE_SOURCES CONFIGURE_DEPENDS src/types/*.cpp)
else()
    set(KALBURATOR_TYPE_HEADERS "")
    set(KALBURATOR_TYPE_SOURCES "")
endif()

if(NOT KALBURATOR_HAVE_ORG_IO)
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "orgbackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "orgbackend\\.cpp$")
endif()

# Akonadi is optional and follows the host's HAVE_AKONADI setting when
# built in-tree. Standalone builds default OFF.
set(KALBURATOR_HAVE_AKONADI OFF CACHE BOOL "Build with KPim6 Akonadi support")
if(NOT KALBURATOR_HAVE_AKONADI)
    list(FILTER KALBURATOR_SYNC_HEADERS EXCLUDE REGEX "akonadibackend\\.h$")
    list(FILTER KALBURATOR_SYNC_SOURCES EXCLUDE REGEX "akonadibackend\\.cpp$")
endif()

add_library(kalburator STATIC
    ${KALBURATOR_SYNC_HEADERS}
    ${KALBURATOR_SYNC_SOURCES}
    ${KALBURATOR_TYPE_HEADERS}
    ${KALBURATOR_TYPE_SOURCES}
)
add_library(Kalburator::Sync ALIAS kalburator)

target_include_directories(kalburator
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/sync>
)
if(KALBURATOR_PROVIDE_TYPES)
    target_include_directories(kalburator
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/types>
    )
endif()

target_link_libraries(kalburator
    PUBLIC
        Qt6::Core
        Qt6::Sql
        Qt6::Network
        Qt6::Xml
        Qt6::Widgets
        KF6::CalendarCore
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
        "akonadi=${KALBURATOR_HAVE_AKONADI} "
        "provide-types=${KALBURATOR_PROVIDE_TYPES})")
