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

# Task 2.1 (fanout collapse Phase 2) — davSlugFromUrl() helper: the stable
# per-account calendar id derived from a DAV URL's last path segment.
# Pure QString helper; runs in the default profile.
kalburator_add_sync_test(tst_davslug)

# VP.c-step-1a (vtodo-parity) — detached-exception blob-pipeline record
# identity. Pure QString/QDateTime helper; runs in the default profile.
kalburator_add_sync_test(tst_record_identity)

# 2026-06-14 — shared Akonadi collection-id scheme helper (producer<->consumer
# single source of truth). Pure QString helper; runs in the default profile.
kalburator_add_sync_test(tst_akonadicollectionid)

# Sync-topology promotion (2026-05-27) — pure generateMappings() generator.
kalburator_add_sync_test(tst_syncmappinggenerator)

# Phase 5 step 1 — ProviderLifecycle coordinator canary tests.
kalburator_add_sync_test(tst_providerlifecycle)

# Phase 5 step 2 — SyncRunCoordinator coordinator canary tests.
kalburator_add_sync_test(tst_syncruncoordinator)

# K.7.1 Task 2 — BackendContribution interface + BackendRegistry extension.
kalburator_add_sync_test(tst_backend_contribution)

# O.1.1 — contribution-change signals on BackendRegistry.
kalburator_add_sync_test(tst_backendregistry_signals)

# O.1.3 — IProvider::applyConfig default impl + edit-account path.
kalburator_add_sync_test(tst_iprovider_applyconfig)

# Plan 8 step 1 — ISyncHost registry-backed defaults (non-pure
# backendById/backends + setBackendRegistry; dynamic_cast clean-miss).
kalburator_add_sync_test(tst_isynchost_defaults)

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

# VP.a (vtodo-parity W8) — unified capability/trait query API. Static
# report pins, JSON round-trips, discovery-fact derivation, and the PRODID/
# sync-collection discovery extensions (reuses FakeCalDavServer).
add_executable(tst_calendar_capabilities
    tst_calendar_capabilities.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
)
target_link_libraries(tst_calendar_capabilities
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_calendar_capabilities COMMAND tst_calendar_capabilities)
set_tests_properties(tst_calendar_capabilities PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# v0.63 RemoteCalendarBackend convergence: discovery primer (zero redundant
# PROPFINDs) + content-cache filename determinism. Reuses FakeCalDavServer.
add_executable(tst_remotecalendarbackend_convergence
    tst_remotecalendarbackend_convergence.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
)
target_link_libraries(tst_remotecalendarbackend_convergence
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_remotecalendarbackend_convergence COMMAND tst_remotecalendarbackend_convergence)
set_tests_properties(tst_remotecalendarbackend_convergence PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Plan 7 T1 — default-lane pins for the legacy write path (startSync /
# removeItem; PlanStan stagingcontroller.cpp:307 is a PROD caller) and the
# calendar CRUD trio (MKCALENDAR / PROPPATCH / DELETE), ahead of the T3/T4
# internal rewrites. Reuses FakeCalDavServer (MKCALENDAR / PROPPATCH /
# collection-DELETE handling added for this test).
add_executable(tst_remotecalendarbackend_writepaths
    tst_remotecalendarbackend_writepaths.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
)
target_link_libraries(tst_remotecalendarbackend_writepaths
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        Kalburator::Sync
)
add_test(NAME tst_remotecalendarbackend_writepaths COMMAND tst_remotecalendarbackend_writepaths)
set_tests_properties(tst_remotecalendarbackend_writepaths PROPERTIES
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

# CardDavConfigWidget tests — lean config-widget round-trip + provider wiring.
add_executable(tst_carddav_config_widget tst_carddav_config_widget.cpp)
target_link_libraries(tst_carddav_config_widget
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_carddav_config_widget COMMAND tst_carddav_config_widget)
set_tests_properties(tst_carddav_config_widget PROPERTIES
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

# M.1 — IProvider optional accessors smoke test
add_executable(tst_iprovider_accessors tst_iprovider_accessors.cpp)
target_link_libraries(tst_iprovider_accessors
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        KF6::ConfigCore
        Kalburator::Sync
)
add_test(NAME tst_iprovider_accessors COMMAND tst_iprovider_accessors)
set_tests_properties(tst_iprovider_accessors PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M.2 — MultiProtocolDavProvider smoke tests + fake-server CalDAV-leg
# projection tests (contentTypes, WildPalms RFC 2026-06-09).
add_executable(tst_multiprotocoldavprovider
    tst_multiprotocoldavprovider.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
    fakecarddavserver.cpp
    fakecarddavserver.h
)
target_link_libraries(tst_multiprotocoldavprovider
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::ConfigCore
        Kalburator::Sync
)
add_test(NAME tst_multiprotocoldavprovider COMMAND tst_multiprotocoldavprovider)
set_tests_properties(tst_multiprotocoldavprovider PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Task 4 (sync-graph redesign Phase 1) — providers emit fine-grained
# connection state (Connecting/Connected/Error) + lastError(). Drives
# MultiProtocolDavProvider against FakeCalDavServer / a closed port, so it
# needs the same fake-server fixture as tst_multiprotocoldavprovider.
add_executable(tst_provider_connection_state
    tst_provider_connection_state.cpp
    fakecaldavserver.cpp
    fakecaldavserver.h
)
target_link_libraries(tst_provider_connection_state
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::ConfigCore
        Kalburator::Sync
)
add_test(NAME tst_provider_connection_state COMMAND tst_provider_connection_state)
set_tests_properties(tst_provider_connection_state PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M.6 — MultiProtocolDavConfigWidget UI test
add_executable(tst_multiprotocoldavconfigwidget tst_multiprotocoldavconfigwidget.cpp)
target_link_libraries(tst_multiprotocoldavconfigwidget
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        KF6::ConfigCore
        Kalburator::Sync
)
add_test(NAME tst_multiprotocoldavconfigwidget COMMAND tst_multiprotocoldavconfigwidget)
set_tests_properties(tst_multiprotocoldavconfigwidget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Task 7 — Akonadi revision digest helper (pure; unconditional — no Akonadi gate).
kalburator_add_sync_test(tst_akonadirevisiondigest)

# Task 8 — Akonadi persistent per-collection revision store (pure; unconditional — no Akonadi gate).
kalburator_add_sync_test(tst_akonadirevisionstore)

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

# WP-D6 — Syncthing discovery/ smoke tests.
# SyncthingDiscovery (static, file-based) + SyncthingMonitor (network,
# with an inline fake Syncthing REST endpoint). Zero prior coverage.
add_executable(tst_syncthing_discovery tst_syncthing_discovery.cpp)
target_link_libraries(tst_syncthing_discovery
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_syncthing_discovery COMMAND tst_syncthing_discovery)
set_tests_properties(tst_syncthing_discovery PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Manual live-server probe (NOT a CI test). Drives CalDavProvider against a real
# Radicale instance to trace the v0.63 discovery primer end-to-end. Off by
# default; enable with -DKALBURATOR_BUILD_LIVE_PROBES=ON.
option(KALBURATOR_BUILD_LIVE_PROBES "Build manual live-server probes (not CI tests)" OFF)
if(KALBURATOR_BUILD_LIVE_PROBES)
    add_executable(live_radicale_primer_probe live_radicale_primer_probe.cpp)
    target_link_libraries(live_radicale_primer_probe
        PRIVATE Qt6::Core Qt6::Network Qt6::Test Kalburator::Sync)

    # E7/O36 — confirms the RFC 6578 sync-collection REPORT path actually
    # fires against a real (not faked) CalDAV server.
    add_executable(live_sync_collection_probe live_sync_collection_probe.cpp)
    target_link_libraries(live_sync_collection_probe
        PRIVATE Qt6::Core Qt6::Network Qt6::Test Kalburator::Sync)
endif()
