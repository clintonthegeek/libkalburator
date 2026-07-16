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

# Task 44 — MappingScheduler unit tests (deleted in fanout-collapse Task 3.1;
# the scheduler was declared but never used by the live queue path).
kalburator_add_engine_test(tst_lastwritewins)

# Architectural-redress Plan 1 Task 3 — MappingQueue unit tests.
# Fast inner-loop guard for the queue collaborator extracted from
# SyncEngine. Engine-level protection lives in
# tst_syncengine_unification (INVARIANTS §6).
kalburator_add_engine_test(tst_mappingqueue)

# Task 46 — CancellationReason integration tests
kalburator_add_engine_integration_test(tst_cancellation_reason)

# Architectural-redress Plan 8 step 3 — pins the canonical single-mapping
# cancel contract after the dual future-interface collapse (resultCount()==1,
# resultAt(0).first().cancelled). Shown RED against the pre-collapse .then()
# path (commit 0595044); green only on the native post-collapse engine.
kalburator_add_engine_integration_test(tst_engine_single_mapping_cancel)

# Sync-excellence campaign E1.1 (FINDINGS O30) — pins SyncResult::
# sourceStats/targetStats population by the unified steady-state write
# path and the skipped-vs-partial-cancel distinction it enables.
kalburator_add_engine_integration_test(tst_sync_result_stats)

# Phase Ia.5 Task 7 — generic property-phase helper unit tests
kalburator_add_engine_test(tst_property_phase)

# Phase Ia.5 Task 20 — unified-path integration test
# (contacts, vcard3) -> (contacts, vcard4) end-to-end through the
# unified dispatchSync. Pins Pipeline compilation, byte
# transformation at the edge, and LossProfile delivery to the host.
kalburator_add_engine_test(tst_engine_unified_routing)

# Plan 2 Task 2 — per-engine registry isolation (deliberately fails to compile
# until Task 4 adds the injecting PluginManager(BackendRegistry*, ShapeRegistries&)
# ctor). Red test: pins the design invariant that two independently-configured
# ShapeRegistries bundles route independently.
kalburator_add_engine_test(tst_engine_registry_injection)

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

# 2026-06-14 — hub-side ChangeDetection skip-unchanged integration test.
# Two GenericSqliteBackends (now Sync::ChangeDetection-capable) stand in for
# the WildPalms sqlite hub; pins that a settled mapping is skipped and a
# mutation defeats the skip (WP efficiency RFC). Needs Qt6::Sql for the
# sqlite backend and KF6::CalendarCore for the shared SyncBackend vtable.
kalburator_add_engine_test(tst_engine_skip_unchanged)
target_link_libraries(tst_engine_skip_unchanged
    PRIVATE
        Qt6::Sql
        KF6::CalendarCore
)

# Phase Ib Task 9 — CardDAV engine integration test.
# FakeCardDavServer → CardDavProvider → RemoteContactsBackend (source)
# → SyncEngine::runSync → ShapedTestBackend (peer, vcard4).
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

# Sync-graph redesign campaign Task 1 (spec §5.9 L1) — un-freeze the
# fast-path skip set on intra-run writes. Three chained MockBackends
# (A<->B<->C) pin that a single run propagates a write two hops when the
# first hop's write invalidates the second mapping's frozen skip verdict;
# an unrelated pair (D<->E) pins that a genuinely untouched mapping still
# skips with zero backend I/O.
kalburator_add_engine_integration_test(tst_engine_skip_invalidation)

# Sync-graph redesign campaign Task 2 (spec §5.9 L2) — fixpoint passes: a
# Queue run re-primes over dirtied mappings until quiescent (cap 3 passes),
# so a single Sync converges regardless of mapping order. Same chained
# A<->B<->C fixture as Task 1, but mappings are listed in the hostile
# [M2, M1] order to prove L2 (not just L1) is doing the work.
kalburator_add_engine_integration_test(tst_engine_fixpoint_passes)

# Fix B (2026-06-12 Akonadi scoped-backend handoff) — the engine must tell a
# genuine fetchItems failure (an overriding backend whose fetch failed, whose
# read path is silent — the scoped AkonadiBackend's shape) apart from a backend
# that doesn't implement fetchItems. Red pre-fix: a genuine failure is swallowed
# and the mapping "succeeds" with 0 records (and a clobber wipes the target).
kalburator_add_engine_integration_test(tst_engine_fetch_failure_discrimination)

# T3 — mass-delete guard engine integration test
kalburator_add_engine_integration_test(tst_mass_delete_guard)

# Phase N.1 — per-record diff helper unit tests
kalburator_add_engine_test(tst_perrecorddiff)

# Sync-topology promotion (2026-05-27) — write-gate: read-only targets receive
# zero writes on both the first-sync mirror (Task 5) and the steady-state sink
# writer (Task 6). Integration test (StubSyncHost + MockBackend).
kalburator_add_engine_integration_test(tst_engine_write_gate)
target_link_libraries(tst_engine_write_gate PRIVATE Qt6::Sql)

# Always-visible sync progress (status bar) Task 2 — per-item progress
# relay: MockBackend fetchProgressChanged (Task 1 fixture) must reach
# SyncEngine::fetchProgress through a dispatchSync-scoped relay.
kalburator_add_engine_integration_test(tst_engine_fetch_progress_relay)
target_link_libraries(tst_engine_fetch_progress_relay PRIVATE Qt6::Sql)

# Architectural-redress Plan 1 Task 1 — SyncEngine/SyncEngineWorker
# unification protective tests. Pins queue iteration order, conflict
# pause/resume round trip, and cancellation propagation through the
# worker yield path. These are the contract the decomposition in
# Tasks 2-6 must preserve (INVARIANTS §6).
kalburator_add_engine_integration_test(tst_syncengine_unification)
target_link_libraries(tst_syncengine_unification PRIVATE Qt6::Sql Qt6::Widgets)
target_include_directories(tst_syncengine_unification
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../calendar)

# WP-D1 — conflict-policy matrix: Duplicate, TargetWins, Skip.
# Mirrors tst_syncengine_unification fixture; pins the three policy
# paths (syncengine.cpp:1623/:1605/default) that had zero engine tests.
kalburator_add_engine_integration_test(tst_conflict_policy_matrix)
target_link_libraries(tst_conflict_policy_matrix PRIVATE Qt6::Sql Qt6::Widgets)
target_include_directories(tst_conflict_policy_matrix
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../calendar)

# WP-D5 — SyncEngine config API unit tests.
# Covers loadSyncMappings, setMappingEnabled, hasSyncWork,
# setSkipUnchangedMappings, register/unregisterActiveController.
# Uses StubSyncHost + StubSyncConfigStore (no sync runs).
kalburator_add_engine_integration_test(tst_syncengine_config_api)
target_include_directories(tst_syncengine_config_api
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../calendar)

# Per-kind canon dispatch Task 7 — fail-loud transcode guard predicate.
kalburator_add_engine_test(tst_transcode_guard)

# Phase B4 (N2 fix) — the sync-convergence campaign's core regression gate.
# FakeCalDavServer -> CalDavProvider -> RemoteCalendarBackend (source)
# <-> SyncEngine::runSync <-> LocalBackend (target, real .ics files).
# secondSyncIsNoOp proves per-side baseline hashes let an already-converged
# mirror sync twice with zero writes, even though the two backends never
# serialize the same logical record identically.
add_executable(tst_sync_convergence
    tst_sync_convergence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.h
)
target_include_directories(tst_sync_convergence
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../sync
        ${CMAKE_CURRENT_SOURCE_DIR}/../calendar
)
target_link_libraries(tst_sync_convergence
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_sync_convergence COMMAND tst_sync_convergence)
set_tests_properties(tst_sync_convergence PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# H3 (sync-hardening campaign) — engine-owned sync-progress tokens.
# Pins O17 (failed apply must not strand a change behind a wrongly-skipped
# mapping), O18 (a foreign edit since the last stored token must defeat the
# skip), that skip still works for genuinely idle cycles, and that a
# clobber run clears a mapping's stored tokens.
add_executable(tst_sync_token_soundness
    tst_sync_token_soundness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.h
)
target_include_directories(tst_sync_token_soundness
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../sync
        ${CMAKE_CURRENT_SOURCE_DIR}/../calendar
)
target_link_libraries(tst_sync_token_soundness
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_sync_token_soundness COMMAND tst_sync_token_soundness)
set_tests_properties(tst_sync_token_soundness PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Sync-excellence campaign E3 (FINDINGS O33a) — pins that a cancel
# observed while mapping 1 is in flight stops the queue before mapping 2
# ever writes anything, protecting against SyncEngineWorker::processSync's
# former unconditional m_cancelled clear erasing an already-landed cancel.
kalburator_add_engine_integration_test(tst_engine_cancel_queue_race)

# Sync-excellence campaign E3 (FINDINGS O33b) — pins that the DecSync
# active-controller loop (driveQueue()'s runActiveSync() dispatch) now
# executes on the worker thread, not driveQueue()'s caller thread.
add_executable(tst_decsync_active_controller_thread tst_decsync_active_controller_thread.cpp)
target_link_libraries(tst_decsync_active_controller_thread
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
        KF6::CalendarCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        kalburator_calendar_test_stubs
)
add_test(NAME tst_decsync_active_controller_thread COMMAND tst_decsync_active_controller_thread)
set_tests_properties(tst_decsync_active_controller_thread PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Sync-excellence campaign E3 (O22 residue) — pins
# waitForWorkerWithDiagnostic()'s bounded-wait-then-diagnostic-then-
# unbounded-wait contract using a stub thread, without deliberately
# deadlocking a real SyncEngine.
kalburator_add_engine_test(tst_worker_teardown)

# Sync-excellence campaign — FINDINGS O43 (found 2026-07-09 at E10).
# prepareFastPath's A6 revision-query marshal must survive engine teardown
# while the query is still pending on a blocked backend I/O thread (the
# PlanStan close-mid-sync / destroy-after-auto-sync-on-load window).
# MockBackend + ChangeDetection mixin; needs KF6::CalendarCore for the
# SyncBackend vtable.
kalburator_add_engine_test(tst_fastpath_teardown_race)
target_link_libraries(tst_fastpath_teardown_race
    PRIVATE
        KF6::CalendarCore
)

# Sync-excellence campaign E8 (FINDINGS O28) — post-crash phantom-conflict
# adoption. LocalBackend (source, real .ics files) -> SyncEngine::runSync
# -> RemoteCalendarBackend (target) -> FakeCalDavServer with
# setDieAfterNWrites(), replaying the H8 kill-mid-push live incident.
add_executable(tst_phantom_conflict_adoption
    tst_phantom_conflict_adoption.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sync/fakecaldavserver.h
)
target_include_directories(tst_phantom_conflict_adoption
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../sync
        ${CMAKE_CURRENT_SOURCE_DIR}/../calendar
)
target_link_libraries(tst_phantom_conflict_adoption
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
        KF6::CalendarCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
add_test(NAME tst_phantom_conflict_adoption COMMAND tst_phantom_conflict_adoption)
set_tests_properties(tst_phantom_conflict_adoption PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# CP-B live smoke — manual live-server probe (NOT a CI test). Drives a full
# create/sync/modify/converge pass plus the pulled-cable (SIGSTOP) O22 check
# against a real Radicale. See docs/campaign/2026-07-05-sync-hardening-phases.md
# CP-B / H5.5. Same opt-in as tests/sync's primer probe.
if(KALBURATOR_BUILD_LIVE_PROBES)
    add_executable(live_cpb_smoke live_cpb_smoke.cpp)
    target_link_libraries(live_cpb_smoke
        PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            KF6::CalendarCore
            $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
    )

    # E12 live smoke — manual live-server probe (NOT a CI test, delete after
    # the phase closes). Re-runs the CP-B kill-mid-push protocol with
    # timestamp-less source events against a real Radicale. See
    # docs/campaign/2026-07-07-sync-excellence-phases.md §14c.
    add_executable(live_e12_smoke live_e12_smoke.cpp)
    target_link_libraries(live_e12_smoke
        PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            KF6::CalendarCore
            $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
    )
endif()
