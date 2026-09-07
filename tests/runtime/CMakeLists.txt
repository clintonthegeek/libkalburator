add_executable(tst_planstan_runtime_contract
    tst_planstan_runtime_contract.cpp
    ../sync/fakecaldavserver.cpp
    ../sync/fakecaldavserver.h
)
target_include_directories(tst_planstan_runtime_contract PRIVATE ../sync ../../src/calendar)
target_link_libraries(tst_planstan_runtime_contract
    PRIVATE Qt6::Core Qt6::Network Qt6::Test Kalburator::Sync)
add_test(NAME tst_planstan_runtime_contract COMMAND tst_planstan_runtime_contract)
set_tests_properties(tst_planstan_runtime_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(tst_wildpalms_runtime_contract
    tst_wildpalms_runtime_contract.cpp
)
target_include_directories(tst_wildpalms_runtime_contract PRIVATE ../../src/calendar)
target_link_libraries(tst_wildpalms_runtime_contract
    PRIVATE Qt6::Core Qt6::Test Kalburator::Sync)
add_test(NAME tst_wildpalms_runtime_contract COMMAND tst_wildpalms_runtime_contract)
set_tests_properties(tst_wildpalms_runtime_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(tst_run_request_contract tst_run_request_contract.cpp)
target_link_libraries(tst_run_request_contract PRIVATE Qt6::Core Qt6::Test Kalburator::Sync)
add_test(NAME tst_run_request_contract COMMAND tst_run_request_contract)
