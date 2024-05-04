cmake_minimum_required(VERSION 3.20)

project(database VERSION 0.1 DESCRIPTION "Poco echo TCPServer")

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -std=c++17")

add_subdirectory(src)
add_subdirectory(base)
add_subdirectory(contrib EXCLUDE_FROM_ALL)

add_executable(server src/Server/server.cpp)
target_link_libraries(server db)
target_include_directories(server PUBLIC src base contrib)

add_executable(client src/Client/client.cpp)
target_link_libraries(client db)
target_include_directories(client PUBLIC src base contrib)