add_executable(graphcli
    main.cpp
    labpaths.cpp
    labpaths.h
)
target_link_libraries(graphcli PRIVATE Qt6::Core Qt6::Network $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>)
