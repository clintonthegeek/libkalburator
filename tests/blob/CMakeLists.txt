# Phase B2 — blob-layer tests.
# Each test is a QTEST_MAIN executable linking Kalburator::Sync.

function(kalburator_add_blob_test TEST_NAME)
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

kalburator_add_blob_test(tst_mockblobbackend)
kalburator_add_blob_test(tst_localblobbackend)
kalburator_add_blob_test(tst_blob_domain_adapter)
kalburator_add_blob_test(tst_engine_mirror_direction)
kalburator_add_blob_test(tst_engine_clobber)
kalburator_add_blob_test(tst_engine_baseonly_backend)

# WP-D2 — wipeCollection contract tests: LocalBlobBackend (default per-record
# delete path) + RemoteCalendarBackend over FakeCalDavServer (CalDAV DELETE).
# FakeCalDavServer gained DELETE support in WP-D2 (fakecaldavserver.cpp).
add_executable(tst_wipe_collection
    tst_wipe_collection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.h
)
target_include_directories(tst_wipe_collection
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../sync
)
target_link_libraries(tst_wipe_collection
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_wipe_collection COMMAND tst_wipe_collection)
set_tests_properties(tst_wipe_collection PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
