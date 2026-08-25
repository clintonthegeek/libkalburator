# B2C P1 — mock Google Calendar API server test bed.
# In-process QTcpServer speaking GOOGLE wire conventions (items[]/
# nextPageToken pagination, syncToken/410-Gone semantics, O67 create-path
# truths) for GoogleApiClient + GoogleCalendarBackend; CI-able, no network.

add_library(kalburator_mock_google_server STATIC
    mockgoogleserver.cpp
    mockgoogleserver.h
)
target_link_libraries(kalburator_mock_google_server PUBLIC Qt6::Core Qt6::Network)
target_include_directories(kalburator_mock_google_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# B2C P1 — GoogleApiClient transport tests (pagination, sync tokens,
# 410 Gone, Bearer injection, O67 insert rejection) against the mock.
add_executable(tst_google_api_client tst_google_api_client.cpp)
target_link_libraries(tst_google_api_client
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        kalburator_mock_google_server
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_google_api_client COMMAND tst_google_api_client)
set_tests_properties(tst_google_api_client PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
