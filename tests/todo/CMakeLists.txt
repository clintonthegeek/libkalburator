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

# W3 (VP.e) — pure splitSeriesAtInstant() unit tests.
kalburator_add_todo_test(tst_todo_series_split)

# B2C P3.b — tasks-flavored vendor doubles (Google Tasks + Graph todoTask)
# mirroring the P1/P2 mock mechanics; transport-level suites pin pagination,
# visibility flags / expand carriers, create truths, and delete semantics.

add_library(kalburator_mock_google_tasks_server STATIC
    mockgoogletasksserver.cpp
    mockgoogletasksserver.h
)
target_link_libraries(kalburator_mock_google_tasks_server
    PUBLIC Qt6::Core Qt6::Network)
target_include_directories(kalburator_mock_google_tasks_server
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(kalburator_mock_graph_todo_server STATIC
    mockgraphtodoserver.cpp
    mockgraphtodoserver.h
)
target_link_libraries(kalburator_mock_graph_todo_server
    PUBLIC Qt6::Core Qt6::Network)
target_include_directories(kalburator_mock_graph_todo_server
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

function(kalburator_add_todo_vendor_mock_test TEST_NAME MOCK_LIB)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            ${MOCK_LIB}
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

kalburator_add_todo_vendor_mock_test(tst_mockgoogle_tasks_server
    kalburator_mock_google_tasks_server)
kalburator_add_todo_vendor_mock_test(tst_mockgraph_todo_server
    kalburator_mock_graph_todo_server)

# B2C P3.c — GoogleTasksBackend against the P3.b mock Tasks server.
# Links the real library (whole-archive) so the backend under test is the
# production one, mirroring tests/contacts/tst_google_people_backend.
add_executable(tst_google_tasks_backend tst_google_tasks_backend.cpp)
target_link_libraries(tst_google_tasks_backend
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        kalburator_mock_google_tasks_server
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_google_tasks_backend COMMAND tst_google_tasks_backend)
set_tests_properties(tst_google_tasks_backend PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# B2C P3.d — GraphTodoTaskBackend against the P3.b mock Graph todo server,
# mirroring tests/contacts/tst_graph_contacts_backend (expanded listings +
# nav-POST carriers + re-list delete confirmation, pointed at todoTask).
add_executable(tst_graph_todo_backend tst_graph_todo_backend.cpp)
target_link_libraries(tst_graph_todo_backend
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        kalburator_mock_graph_todo_server
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_graph_todo_backend COMMAND tst_graph_todo_backend)
set_tests_properties(tst_graph_todo_backend PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# B2C P3.f — LIVE checkpoints (skip without KALBURATOR_GOOGLE_DIR /
# KALBURATOR_MSGRAPH_DIR); wiring mirrors tests/contacts P2.f live suites:
# no mock lib, production whole-archive, offscreen. Fixture-replay slots
# read committed vendor fixtures directly.
add_executable(tst_google_tasks_backend_live tst_google_tasks_backend_live.cpp)
target_link_libraries(tst_google_tasks_backend_live
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
target_compile_definitions(tst_google_tasks_backend_live PRIVATE
    KALBURATOR_VENDOR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/vendor"
)
add_test(NAME tst_google_tasks_backend_live
    COMMAND tst_google_tasks_backend_live)
set_tests_properties(tst_google_tasks_backend_live PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(tst_graph_todo_backend_live tst_graph_todo_backend_live.cpp)
target_link_libraries(tst_graph_todo_backend_live
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
target_compile_definitions(tst_graph_todo_backend_live PRIVATE
    KALBURATOR_VENDOR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/vendor"
)
add_test(NAME tst_graph_todo_backend_live
    COMMAND tst_graph_todo_backend_live)
set_tests_properties(tst_graph_todo_backend_live PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
