add_executable(matrixgen
    main.cpp
)
target_link_libraries(matrixgen
    PRIVATE
        Qt6::Core
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
)
