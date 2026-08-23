add_executable(graphcli
    main.cpp
    graphauth.cpp
    graphauth.h
    graphclient.cpp
    graphclient.h
)
target_link_libraries(graphcli PRIVATE Qt6::Core Qt6::Network)
