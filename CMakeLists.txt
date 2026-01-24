cmake_minimum_required(VERSION 3.20)
project(DaraWebGameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CPR als Unterprojekt (lokal)
set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cpr)

add_executable(DaraWebGameServer main.cpp)

# Deine lokalen single-header libs:
target_include_directories(DaraWebGameServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# CPR linken
target_link_libraries(DaraWebGameServer PRIVATE cpr::cpr)
