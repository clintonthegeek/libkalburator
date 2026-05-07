# Phase H — sync-layer tests (provider manager).
function(kalburator_add_sync_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Test
            KF6::ConfigCore
            Kalburator::Sync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_sync_test(tst_provider_manager)

# tst_caldav_provider has multiple sources (the QTcpServer-based fake
# server fixture plus the test driver), so we can't use the
# single-source helper.
add_executable(tst_caldav_provider
    tst_caldav_provider.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
)
target_link_libraries(tst_caldav_provider
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_caldav_provider COMMAND tst_caldav_provider)
set_tests_properties(tst_caldav_provider PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
