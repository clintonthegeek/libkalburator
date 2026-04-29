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
            Kalburator::Sync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_calendar_test(tst_icsfeedfetcher)
kalburator_add_calendar_test(tst_calendar_baseline_store)
kalburator_add_calendar_test(tst_mockbackend_blob_view)
kalburator_add_calendar_test(tst_localbackend_blob_view)
kalburator_add_calendar_test(tst_remotebackend_blob_view)

if(KALBURATOR_HAVE_ORG_IO)
    kalburator_add_calendar_test(tst_orgbackend_blob_view)
endif()

if(KALBURATOR_HAVE_AKONADI)
    kalburator_add_calendar_test(tst_akonadibackend_blob_view)
endif()

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
