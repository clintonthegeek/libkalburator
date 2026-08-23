add_executable(ms-roundtrip
    main.cpp
)
target_link_libraries(ms-roundtrip
    PRIVATE
        Qt6::Core
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
