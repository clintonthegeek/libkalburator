# G.6 — engine-layer unit and integration tests.

function(kalburator_add_engine_test TEST_NAME)
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

function(kalburator_add_engine_integration_test TEST_NAME)
    kalburator_add_engine_test(${TEST_NAME})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Network
            KF6::CalendarCore
            kalburator_calendar_test_stubs
    )
    # Expose the stubs' parent dir so #include "stubsynchost.h" resolves
    # via the stubs library's PUBLIC include path.
endfunction()

# Task 44 — MappingScheduler unit tests
kalburator_add_engine_test(tst_mapping_scheduler)

# Task 46 — CancellationReason integration tests
kalburator_add_engine_integration_test(tst_cancellation_reason)

# Phase Ia.5 Task 7 — generic property-phase helper unit tests
kalburator_add_engine_test(tst_property_phase)

# Phase Ia.5 Task 20 — unified-path integration test
# (contacts, vcard3) -> (contacts, vcard4) end-to-end through the
# unified dispatchSync. Pins Pipeline compilation, byte
# transformation at the edge, and LossProfile delivery to the host.
kalburator_add_engine_test(tst_engine_unified_routing)

# Phase K.9 — typed-source -> universal-sink (RawFilesBackend) dispatch
# regression test. Today (pre-K.9): red, because dispatchSync rejects the
# cross-domain mismatch between the source's typed shape and the
# RawFilesBackend's Shape::Any(). Green once shape becomes a per-
# collection property.
kalburator_add_engine_test(tst_engine_universal_sink_dispatch)
target_link_libraries(tst_engine_universal_sink_dispatch
    PRIVATE
        KF6::CalendarCore
)

# Phase Ib Task 9 — CardDAV engine integration test.
# FakeCardDavServer → CardDavProvider → RemoteContactsBackend (source)
# → SyncEngine::runSyncFuture → ShapedTestBackend (peer, vcard4).
# Pins 2-record round-trip and vCard 3.0 passthrough behavior.
# Uses --whole-archive so the contacts domain plugin's static initializer
# registers the vcard3↔vcard4 edges in TransformationRegistry.
add_executable(tst_carddav_engine_integration
    tst_carddav_engine_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecarddavserver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecarddavserver.h
)
target_include_directories(tst_carddav_engine_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../sync
)
target_link_libraries(tst_carddav_engine_integration
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        KF6::Contacts
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_carddav_engine_integration COMMAND tst_carddav_engine_integration)
set_tests_properties(tst_carddav_engine_integration PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Layer B — silent-success guard (failing test: no engine call site migrated yet)
kalburator_add_engine_integration_test(tst_engine_silent_success_guard)

# T3 — mass-delete guard engine integration test
kalburator_add_engine_integration_test(tst_mass_delete_guard)

# Phase N.1 — per-record diff helper unit tests
kalburator_add_engine_test(tst_perrecorddiff)
