function(kalburator_add_todo_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Test
            $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_todo_test(tst_vtodo_plugin)
kalburator_add_todo_test(tst_vtodo_differ)
kalburator_add_todo_test(tst_todotxt_stage)
# Plan 3 Task B2/B3/B5 — vtodo ↔ canon round-trip + loss profile tests
kalburator_add_todo_test(tst_todo_canon_roundtrip)
# EEE Phase 3 — google-task / ms-todotask ⇄ canon edges
kalburator_add_todo_test(tst_google_task_canon_edge)
# Fixture-promotion slot reads committed vendor fixtures directly.
target_compile_definitions(tst_google_task_canon_edge PRIVATE
    KALBURATOR_VENDOR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/vendor"
)
kalburator_add_todo_test(tst_ms_todotask_canon_edge)
# Fixture-promotion slot: committed live-capture corpus extracts
target_compile_definitions(tst_ms_todotask_canon_edge PRIVATE
    KALBURATOR_VENDOR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/vendor")
