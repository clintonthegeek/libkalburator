cmake_minimum_required(VERSION 3.19)
project(KalburatorExternalConsumer LANGUAGES CXX)

find_package(Kalburator 0.2.4 CONFIG REQUIRED COMPONENTS Types)

add_executable(kalburator_external_consumer main.cpp)
target_link_libraries(kalburator_external_consumer PRIVATE Kalburator::Types)
