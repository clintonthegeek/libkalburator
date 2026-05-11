add_library(tinyplugin MODULE tinyplugin.cpp)
target_link_libraries(tinyplugin PRIVATE kalburator Qt6::Core)
set_target_properties(tinyplugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PREFIX "")
