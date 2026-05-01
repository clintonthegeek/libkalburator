add_library(KalburatorWidgets STATIC
    lossprofiledetailview.cpp
    lossprofiledetailview.h
)

set_target_properties(KalburatorWidgets PROPERTIES AUTOMOC ON)

target_include_directories(KalburatorWidgets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/kalburator/widgets>
)

target_link_libraries(KalburatorWidgets
    PUBLIC
        Qt6::Widgets
        Kalburator::Sync
)

add_library(Kalburator::Widgets ALIAS KalburatorWidgets)
