function(kalburator_add_outline_test TEST_NAME)
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

kalburator_add_outline_test(tst_outline_plugin)
kalburator_add_outline_test(tst_outline_node)
kalburator_add_outline_test(tst_outline_catalogue)
kalburator_add_outline_test(tst_outline_differ)
kalburator_add_outline_test(tst_outline_opml)
# The org peer (and its OrgGrove dependency) is optional; only build its test
# when KALBURATOR_HAVE_OUTLINE_ORG is ON. The shapes/round-trip tests below
# #ifdef-guard their org assertions, so they build and pass either way.
if(KALBURATOR_HAVE_OUTLINE_ORG)
    kalburator_add_outline_test(tst_outline_org)
endif()
kalburator_add_outline_test(tst_outline_shapes)
kalburator_add_outline_test(tst_outline_canon_roundtrip)
