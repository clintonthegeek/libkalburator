add_executable(googlecli
    main.cpp
    labpaths.cpp
    labpaths.h
)
target_link_libraries(googlecli PRIVATE Qt6::Core Qt6::Network $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>)
