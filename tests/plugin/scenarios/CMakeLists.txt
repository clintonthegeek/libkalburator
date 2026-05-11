add_library(kalburator_scenario_fakes STATIC
    fake_docstogo_plugin.cpp
    fake_msoffice_plugin.cpp
    fake_odf_plugin.cpp)
target_link_libraries(kalburator_scenario_fakes PUBLIC
    kalburator kalburator_plugin_test_fakes Qt6::Core)
target_include_directories(kalburator_scenario_fakes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(tst_docstogo_scenario tst_docstogo_scenario.cpp)
target_link_libraries(tst_docstogo_scenario PRIVATE
    kalburator kalburator_scenario_fakes kalburator_plugin_test_fakes Qt6::Test)
add_test(NAME tst_docstogo_scenario COMMAND tst_docstogo_scenario)
