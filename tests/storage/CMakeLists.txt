# Phase K.5 — storage-layer tests (moved from tests/journal/ in K.5.T2).
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.

function(kalburator_add_storage_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Sql
            Qt6::Test
            $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_storage_test(tst_baseline_store_per_record_keys)
kalburator_add_storage_test(tst_baseline_store_v3)
kalburator_add_storage_test(tst_idmappingstore)
kalburator_add_storage_test(tst_baseline_store_collection_properties)
kalburator_add_storage_test(tst_baseline_store_mapping_metadata)
kalburator_add_storage_test(tst_baseline_store_v4_to_v5_migration)
kalburator_add_storage_test(tst_baseline_store_calendar_shape_round_trip)
