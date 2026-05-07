# libkalburator's test tree. Gated by KALBURATOR_BUILD_TESTS in the
# top-level CMakeLists.txt. Each subdirectory hosts a cluster of
# related test executables (QTEST_MAIN style).

add_subdirectory(blob)
add_subdirectory(engine)
add_subdirectory(journal)
add_subdirectory(calendar)
add_subdirectory(transcoding)
add_subdirectory(shape)
add_subdirectory(todo)
add_subdirectory(contacts)
add_subdirectory(memo)
add_subdirectory(sinks)
add_subdirectory(sync)
