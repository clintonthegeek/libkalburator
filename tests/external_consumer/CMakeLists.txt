cmake_minimum_required(VERSION 3.19)
project(KalburatorExternalConsumer LANGUAGES CXX)

find_package(Kalburator 0.2.4 CONFIG REQUIRED COMPONENTS Types Core)

add_executable(kalburator_external_consumer main.cpp)
target_link_libraries(kalburator_external_consumer PRIVATE Kalburator::Types)

# Core is the documented headless composition target. Reuse the same tiny
# program so the install proof verifies both public include propagation and
# that Core does not require the Sync/UI archive.
add_executable(kalburator_headless_consumer main.cpp)
target_link_libraries(kalburator_headless_consumer PRIVATE Kalburator::Core)
