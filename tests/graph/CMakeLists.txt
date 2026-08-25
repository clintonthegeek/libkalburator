# EEE Stage D — mock Microsoft Graph server test bed.
# In-process QTcpServer replaying Graph wire semantics (pagination, delta
# walks, error shapes) for the Phase-7 MSGraphCalendarBackend; CI-able,
# no network. See docs/2026-08-22-campaign-proposal-vendor-convergence-eee.md §7.D.

add_library(kalburator_mock_graph_server STATIC
    mockgraphserver.cpp
    mockgraphserver.h
)
target_link_libraries(kalburator_mock_graph_server PUBLIC Qt6::Core Qt6::Network)
target_include_directories(kalburator_mock_graph_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

function(kalburator_add_graph_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            kalburator_mock_graph_server
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_graph_test(tst_mock_graph_server)

# EEE Phase 7.C foundation — GraphApiClient transport tests (pagination +
# delta walks against the Stage D mock). Links the real library so the
# client under test is the production one, not a test double.
add_executable(tst_graph_api_client tst_graph_api_client.cpp)
target_link_libraries(tst_graph_api_client
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        kalburator_mock_graph_server
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_graph_api_client COMMAND tst_graph_api_client)
set_tests_properties(tst_graph_api_client PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# EEE Phase 7.C — MSGraphCalendarBackend against the Stage D mock.
add_executable(tst_ms_graph_calendar_backend tst_ms_graph_calendar_backend.cpp)
target_link_libraries(tst_ms_graph_calendar_backend
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        kalburator_mock_graph_server
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_ms_graph_calendar_backend COMMAND tst_ms_graph_calendar_backend)
set_tests_properties(tst_ms_graph_calendar_backend PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# B2C P1.f — LIVE checkpoint (skips without KALBURATOR_MSGRAPH_DIR).
add_executable(tst_ms_graph_calendar_backend_live tst_ms_graph_calendar_backend_live.cpp)
target_link_libraries(tst_ms_graph_calendar_backend_live
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_ms_graph_calendar_backend_live COMMAND tst_ms_graph_calendar_backend_live)
set_tests_properties(tst_ms_graph_calendar_backend_live PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
