cmake_minimum_required(VERSION 3.20)
project(DaraRestServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CPR als Unterprojekt (lokal)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cpr)

add_executable(dara_server main.cpp)

# Deine lokalen single-header libs:
target_include_directories(dara_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# CPR linken
target_link_libraries(dara_server PRIVATE cpr::cpr)
