# Phase G — shape-pipeline architecture unit tests.
# Each test is a QTEST_GUILESS_MAIN executable linking Kalburator::Sync.

function(kalburator_add_shape_test TEST_NAME)
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

kalburator_add_shape_test(tst_shape)
kalburator_add_shape_test(tst_property_catalogue)
kalburator_add_shape_test(tst_loss_profile)
kalburator_add_shape_test(tst_pipeline)
kalburator_add_shape_test(tst_transformation_registry)
kalburator_add_shape_test(tst_domain_registry)
kalburator_add_shape_test(tst_dynamic_domain_registration)
kalburator_add_shape_test(tst_default_blob_writer)
# K.7.1 — DomainOperations + registry
kalburator_add_shape_test(tst_domain_operations_registry)
kalburator_add_shape_test(tst_canonical_spine)
kalburator_add_shape_test(tst_canonjson_diff_merge)
