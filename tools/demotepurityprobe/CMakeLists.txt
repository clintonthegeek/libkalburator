add_executable(demotepurityprobe
    main.cpp
)
target_link_libraries(demotepurityprobe
    PRIVATE
        Qt6::Core
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
