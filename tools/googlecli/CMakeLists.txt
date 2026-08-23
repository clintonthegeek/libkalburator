add_executable(googlecli
    main.cpp
    googleauth.cpp
    googleauth.h
    googleclient.cpp
    googleclient.h
)
target_link_libraries(googlecli PRIVATE Qt6::Core Qt6::Network)
