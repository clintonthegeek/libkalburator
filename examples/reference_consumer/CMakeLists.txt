add_executable(reference_consumer main.cpp)
target_link_libraries(reference_consumer PRIVATE
    kalburator
    Qt6::Core)
