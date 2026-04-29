add_executable(tst_transcoding_router tst_transcoding_router.cpp)
target_link_libraries(tst_transcoding_router
    PRIVATE
        Qt6::Test
        Qt6::Core
        KF6::CalendarCore
        Kalburator::Sync
)
add_test(NAME tst_transcoding_router COMMAND tst_transcoding_router)
