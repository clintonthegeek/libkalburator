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

# K.7.1 Task 2 — BackendContribution interface + BackendRegistry extension.
kalburator_add_sync_test(tst_backend_contribution)

# K.7.1 Task 8 — NeutralProvider account-neutral IProvider adapter.
kalburator_add_sync_test(tst_neutral_provider)

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

# Phase H Task 7 — CalDavConfigWidget UI tests. Reuses FakeCalDavServer
# from Task 6 for the Test-Connection button workflow.
add_executable(tst_caldav_config_widget
    tst_caldav_config_widget.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
)
target_link_libraries(tst_caldav_config_widget
    PRIVATE
        Qt6::Core
        Qt6::Test
        Qt6::Network
        Qt6::Widgets
        Kalburator::Sync
)
add_test(NAME tst_caldav_config_widget COMMAND tst_caldav_config_widget)
set_tests_properties(tst_caldav_config_widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase H Task 8 — End-to-end integration test wiring KConfig persistence,
# ProviderManager (default factory), CalDavProvider, FakeCalDavServer,
# and BackendRegistry. Verifies the additive provider design's full
# registration path. Reuses FakeCalDavServer from Task 6.
add_executable(tst_caldav_integration
    tst_caldav_integration.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
)
target_link_libraries(tst_caldav_integration
    PRIVATE
        Qt6::Core
        Qt6::Test
        Qt6::Network
        KF6::ConfigCore
        Kalburator::Sync
)
add_test(NAME tst_caldav_integration COMMAND tst_caldav_integration)
set_tests_properties(tst_caldav_integration PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase Ib Task 3 — FakeCardDavServer self-test.
# Exercises every route and failure mode of the CardDAV server fixture.
add_executable(tst_fake_carddav_server
    tst_fake_carddav_server.cpp
    fakecarddavserver.cpp
    fakecarddavserver.h
)
target_link_libraries(tst_fake_carddav_server
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_fake_carddav_server COMMAND tst_fake_carddav_server)
set_tests_properties(tst_fake_carddav_server PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase Ib Task 4 — CardDavCapabilityDiscovery unit tests.
# Exercises the RFC 6352 PROPFIND discovery chain against FakeCardDavServer
# and two inline minimal server fixtures (empty home-set, mixed home-set).
add_executable(tst_carddav_capability_discovery
    tst_carddav_capability_discovery.cpp
    fakecarddavserver.cpp
    fakecarddavserver.h
)
target_link_libraries(tst_carddav_capability_discovery
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_carddav_capability_discovery COMMAND tst_carddav_capability_discovery)
set_tests_properties(tst_carddav_capability_discovery PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase Ib Task 8 — CardDavProvider tests against a fake CardDAV server.
add_executable(tst_carddav_provider
    tst_carddav_provider.cpp
    fakecarddavserver.cpp
    fakecarddavserver.h
)
target_link_libraries(tst_carddav_provider
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_carddav_provider COMMAND tst_carddav_provider)
set_tests_properties(tst_carddav_provider PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase L.1 — AkonadiProvider identity tests.
if(KALBURATOR_HAVE_AKONADI)
    add_executable(tst_akonadiprovider tst_akonadiprovider.cpp)
    target_link_libraries(tst_akonadiprovider PRIVATE
        Kalburator::Sync Qt6::Test Qt6::Core)
    add_test(NAME tst_akonadiprovider COMMAND tst_akonadiprovider)
    set_tests_properties(tst_akonadiprovider PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
