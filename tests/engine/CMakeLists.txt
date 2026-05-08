# G.6 — engine-layer unit and integration tests.

function(kalburator_add_engine_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Test
            Kalburator::Sync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

function(kalburator_add_engine_integration_test TEST_NAME)
    kalburator_add_engine_test(${TEST_NAME})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Network
            KF6::CalendarCore
            kalburator_calendar_test_stubs
    )
    # Expose the stubs' parent dir so #include "stubsynchost.h" resolves
    # via the stubs library's PUBLIC include path.
endfunction()

# Task 44 — MappingScheduler unit tests
kalburator_add_engine_test(tst_mapping_scheduler)

# Task 46 — CancellationReason integration tests
kalburator_add_engine_integration_test(tst_cancellation_reason)

# Phase Ia.5 Task 7 — generic property-phase helper unit tests
kalburator_add_engine_test(tst_property_phase)
