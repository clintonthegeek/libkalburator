# Phase M UI widget tests

add_executable(tst_collectionpickerwidget tst_collectionpickerwidget.cpp)
target_link_libraries(tst_collectionpickerwidget
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_collectionpickerwidget COMMAND tst_collectionpickerwidget)
set_tests_properties(tst_collectionpickerwidget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
