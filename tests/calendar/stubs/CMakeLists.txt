# tests/calendar/stubs/CMakeLists.txt
# Reusable stubs for libkalburator's calendar-layer integration tests.
# Phase D.0 — added 2026-04-28; intended to be pinned by Phase D/E/F/G tests.

add_library(kalburator_calendar_test_stubs STATIC
    stubsynchost.cpp
    stubcalendarcollection.cpp
    stubincidenceregistry.cpp
    stubsyncconfigstore.cpp
)
set_target_properties(kalburator_calendar_test_stubs PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(kalburator_calendar_test_stubs
    PUBLIC
        Qt6::Core
        KF6::CalendarCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
target_include_directories(kalburator_calendar_test_stubs
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
