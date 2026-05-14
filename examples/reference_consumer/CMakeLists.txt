add_executable(reference_consumer main.cpp)
target_link_libraries(reference_consumer PRIVATE
    kalburator
    Qt6::Core)

get_filename_component(LIBKALBURATOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_test(NAME tst_reference_consumer_smoke
    COMMAND reference_consumer --smoke /tmp/refconsumer-${CMAKE_SYSTEM_PROCESSOR}
    WORKING_DIRECTORY ${LIBKALBURATOR_ROOT})
set_tests_properties(tst_reference_consumer_smoke PROPERTIES
    FAIL_REGULAR_EXPRESSION "qCritical|Missing provider contributions"
    TIMEOUT 30)
