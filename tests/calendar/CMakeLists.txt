# Phase E.13 — calendar-layer tests.
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.

set(KALBURATOR_CALENDAR_FIXTURE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

function(kalburator_add_calendar_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_compile_definitions(${TEST_NAME}
        PRIVATE
            KALBURATOR_CALENDAR_FIXTURE_DIR="${KALBURATOR_CALENDAR_FIXTURE_DIR}"
    )
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            KF6::CalendarCore
            $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

# G.9.b Task 73 — backend streaming signal tests (migrated from PlanStan).
# OrgBackend tests are conditionally compiled via KALBURATOR_HAVE_ORG_IO.
kalburator_add_calendar_test(tst_backend_signals)
if(KALBURATOR_HAVE_ORG_IO)
    target_compile_definitions(tst_backend_signals PRIVATE KALBURATOR_HAVE_ORG_IO)
endif()

kalburator_add_calendar_test(tst_icsfeedfetcher)
kalburator_add_calendar_test(tst_mockbackend_blob_view)
kalburator_add_calendar_test(tst_mockbackend_failure_injection)
kalburator_add_calendar_test(tst_localbackend_blob_view)
kalburator_add_calendar_test(tst_localbackend)
# Plan 7b T1 — default-lane pins for LocalBackend's write paths (startSync
# via AsyncFileWriter, removeItem, updateCalendar metadata round-trip,
# readonly marker) ahead of the T3/T4 internal rewrites.
kalburator_add_calendar_test(tst_localbackend_writepaths)

# WP-D9: updateRecord tests use FakeCalDavServer (PUT + DELETE added in v0.63/WP-D2).
add_executable(tst_remotecalendarbackend_blob_view
    tst_remotecalendarbackend_blob_view.cpp
    ../sync/fakecaldavserver.cpp
)
target_compile_definitions(tst_remotecalendarbackend_blob_view
    PRIVATE KALBURATOR_CALENDAR_FIXTURE_DIR="${KALBURATOR_CALENDAR_FIXTURE_DIR}"
)
target_include_directories(tst_remotecalendarbackend_blob_view
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../sync
)
target_link_libraries(tst_remotecalendarbackend_blob_view
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_remotecalendarbackend_blob_view COMMAND tst_remotecalendarbackend_blob_view)
set_tests_properties(tst_remotecalendarbackend_blob_view PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

kalburator_add_calendar_test(tst_remotecalendarbackend)

if(KALBURATOR_HAVE_ORG_IO)
    kalburator_add_calendar_test(tst_orgbackend_blob_view)
    # G.9.b Tasks 68-69 — migrated from PlanStan
    kalburator_add_calendar_test(tst_orgbackend)
    kalburator_add_calendar_test(tst_orgbackend_external)
endif()

if(KALBURATOR_HAVE_AKONADI)
    kalburator_add_calendar_test(tst_akonadibackend_blob_view)
    kalburator_add_calendar_test(tst_akonadi_payload)
    kalburator_add_calendar_test(tst_akonadibackend_live)
endif()

kalburator_add_calendar_test(tst_decsyncbackend)
kalburator_add_calendar_test(tst_decsyncbackend_blob_view)
kalburator_add_calendar_test(tst_subscriptionbackend_blob_view)
kalburator_add_calendar_test(tst_holidaysubscriptionbackend_blob_view)

# Phase D.0 — integration tests against MockBackend through stub host.
add_subdirectory(stubs)

function(kalburator_add_calendar_integration_test TEST_NAME)
    kalburator_add_calendar_test(${TEST_NAME})
    target_link_libraries(${TEST_NAME}
        PRIVATE kalburator_calendar_test_stubs)
endfunction()

kalburator_add_calendar_integration_test(tst_calendar_sync_full)
kalburator_add_calendar_integration_test(tst_calendar_sync_oneway)
kalburator_add_calendar_integration_test(tst_calendar_conflict)
kalburator_add_calendar_integration_test(tst_calendar_transcoding_warning)

# Phase D Task 21 — first-sync path via BlobSyncEngine
kalburator_add_calendar_integration_test(tst_calendar_first_sync_via_blob_engine)

# Phase D Task 22 — subsequent-sync blob view + hash skip
kalburator_add_calendar_integration_test(tst_calendar_subsequent_sync_uses_blob_view)

# Phase F.0 — Backend write/fetch failure paths
kalburator_add_calendar_integration_test(tst_calendar_sync_error_recovery)

# Phase F1 Task 7 — engine boundary integration (calendar mapping +
# blob one-shot through one SyncEngine instance)
kalburator_add_calendar_integration_test(tst_engine_unified_boundary)

# Phase F2 Task 2 — cancellation contract scaffold (skipped until
# Group 2 lands the QFuture-based runSync and cancellation wiring).
kalburator_add_calendar_integration_test(tst_engine_cancellation)

# Phase G.6 Task 43 — subset dispatch via runSyncFuture(QList<QString>)
kalburator_add_calendar_integration_test(tst_engine_subset_dispatch)

# Plan 2 (architectural-redress) — CalendarManager characterization suite
kalburator_add_calendar_integration_test(tst_calendar_manager)

# Phase F2 Task 4 — SyncOperation contract (State enum, idempotent
# setState, cancelRequested flag, isFinished predicate).
kalburator_add_calendar_test(tst_syncoperation_contract)

# Migrated from PlanStan (types defined in libkalburator)
kalburator_add_calendar_test(tst_syncdiff)
kalburator_add_calendar_test(tst_incidencediff)

kalburator_add_calendar_test(tst_syncstore)
target_link_libraries(tst_syncstore PRIVATE Qt6::Sql)

# Task 6 — ConflictManager tests (migrated from PlanStan)
kalburator_add_calendar_test(tst_conflictmanager)
target_link_libraries(tst_conflictmanager PRIVATE Qt6::Sql Qt6::Widgets)

# Phase K.5 Task 6 — DomainPlugin::baselineProperties() virtual
kalburator_add_calendar_test(tst_calendardomain_baseline_properties)

# Phase G.2 — calendar property catalogue, differ, merger, plugin
add_subdirectory(differs)
add_subdirectory(plugin)

# Plan 3 Task C2/C3/C5 — ical ↔ canon round-trip + loss profile tests
kalburator_add_calendar_test(tst_calendar_canon_roundtrip)

# Plan 4 Task 2 — org-ical (de)serialization stages (RRULE simplification)
kalburator_add_calendar_test(tst_orgical_canon_roundtrip)

# Sync-topology promotion (2026-05-27) — domain field, collectionId, alias,
# domain serialization, hasWritableRemoteSyncTarget() demotion fact
kalburator_add_calendar_test(tst_logicalcalendar_domain)
kalburator_add_calendar_test(tst_logicalcalendarbuilder_readonly_seed)
