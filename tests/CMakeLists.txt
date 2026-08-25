# libkalburator's test tree. Gated by KALBURATOR_BUILD_TESTS in the
# top-level CMakeLists.txt. Each subdirectory hosts a cluster of
# related test executables (QTEST_MAIN style).

add_subdirectory(blob)
add_subdirectory(convergence)
add_subdirectory(graph)
add_subdirectory(google)
add_subdirectory(engine)
add_subdirectory(journal)
add_subdirectory(storage)
add_subdirectory(identity)
add_subdirectory(calendar)
add_subdirectory(shape)
add_subdirectory(todo)
add_subdirectory(contacts)
add_subdirectory(note)
add_subdirectory(outline)
add_subdirectory(sinks)
add_subdirectory(sync)
add_subdirectory(plugin)
add_subdirectory(typesupport)
add_subdirectory(ui)
