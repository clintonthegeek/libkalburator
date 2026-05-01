# G.8 — Universal sink tests.

function(kalburator_add_sink_test TEST_NAME)
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

kalburator_add_sink_test(tst_rawfiles_backend)
kalburator_add_sink_test(tst_generic_sqlite_backend)
kalburator_add_sink_test(tst_universal_sink_as_source)
