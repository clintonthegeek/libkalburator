add_executable(g-roundtrip
    main.cpp
)
target_link_libraries(g-roundtrip
    PRIVATE
        Qt6::Core
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
