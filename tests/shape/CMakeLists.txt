# Phase G — shape-pipeline architecture unit tests.
# Each test is a QTEST_GUILESS_MAIN executable linking Kalburator::Sync.

function(kalburator_add_shape_test TEST_NAME)
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

kalburator_add_shape_test(tst_shape)
kalburator_add_shape_test(tst_property_catalogue)
kalburator_add_shape_test(tst_loss_profile)
