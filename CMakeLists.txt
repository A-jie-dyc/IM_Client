cmake_minimum_required(VERSION 3.19)
project(IM_Client LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

qt_add_executable(IM_Client
    main.cpp
    TcpWorker.h TcpWorker.cpp
    TcpClient.h TcpClient.cpp

)

target_link_libraries(IM_Client
    PRIVATE
        Qt::Core
        Qt::Network
        Qt::Widgets
)
target_link_libraries(IM_Client PRIVATE Qt6::Widgets)

install(TARGETS IM_Client
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
