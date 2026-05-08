function(kalburator_add_contacts_test TEST_NAME)
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

kalburator_add_contacts_test(tst_vcard_plugin)
kalburator_add_contacts_test(tst_vcard_differ)
kalburator_add_contacts_test(tst_vcard3_vcard4_edge)
# Phase Ib.5 Task 3 — AskUser pause/resume in unified dispatchSync (contacts domain)
kalburator_add_contacts_test(tst_unified_askuser_pause)

# Phase Ib Task 5 — RemoteContactsBackend read-side tests.
# Needs FakeCardDavServer (from tests/sync/); include source files directly.
add_executable(tst_remote_contacts_backend
    tst_remote_contacts_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecarddavserver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecarddavserver.h
)
target_include_directories(tst_remote_contacts_backend
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../sync
)
target_link_libraries(tst_remote_contacts_backend
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_remote_contacts_backend COMMAND tst_remote_contacts_backend)
set_tests_properties(tst_remote_contacts_backend PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
