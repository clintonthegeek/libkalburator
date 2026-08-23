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
