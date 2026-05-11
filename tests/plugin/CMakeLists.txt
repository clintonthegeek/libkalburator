add_executable(tst_manifest_parser tst_manifest_parser.cpp)
target_link_libraries(tst_manifest_parser PRIVATE kalburator Qt6::Test Qt6::Core)
add_test(NAME tst_manifest_parser COMMAND tst_manifest_parser)
