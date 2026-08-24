function(kalburator_add_convergence_test TEST_NAME)
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

# EEE Phase 6 — G⇄M pipeline convergence gate + generated-matrix pin
kalburator_add_convergence_test(tst_gm_pipeline_convergence)
target_compile_definitions(tst_gm_pipeline_convergence
    PRIVATE KALBURATOR_EEE_DOC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../docs/campaign/eee"
)
