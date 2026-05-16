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

# M.10 — AccountsListWidget
add_executable(tst_accountslistwidget tst_accountslistwidget.cpp)
target_link_libraries(tst_accountslistwidget
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_accountslistwidget COMMAND tst_accountslistwidget)
set_tests_properties(tst_accountslistwidget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M.11 — ProviderConfigDialog
add_executable(tst_providerconfigdialog tst_providerconfigdialog.cpp)
target_link_libraries(tst_providerconfigdialog
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        Kalburator::Sync
)
add_test(NAME tst_providerconfigdialog COMMAND tst_providerconfigdialog)
set_tests_properties(tst_providerconfigdialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
