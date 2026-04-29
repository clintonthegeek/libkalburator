# Phase B2 — blob-layer tests.
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.

function(kalburator_add_blob_test TEST_NAME)
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

kalburator_add_blob_test(tst_mockblobbackend)
kalburator_add_blob_test(tst_localblobbackend)
kalburator_add_blob_test(tst_blobsyncengine)
kalburator_add_blob_test(tst_blob_domain_adapter)
kalburator_add_blob_test(tst_engine_blob_one_shot)
