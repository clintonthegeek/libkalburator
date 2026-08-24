function(kalburator_add_identity_test TEST_NAME)
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

# EEE identity layer — first resolver (one rule: shared email ⇒ one entity)
kalburator_add_identity_test(tst_identity_links)
target_compile_definitions(tst_identity_links
    PRIVATE KALBURATOR_VENDOR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/vendor"
)

# PersonDirectory — the §5 payoff: "who is in this meeting?"
kalburator_add_identity_test(tst_person_directory)
target_compile_definitions(tst_person_directory
    PRIVATE KALBURATOR_VENDOR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/vendor"
)
