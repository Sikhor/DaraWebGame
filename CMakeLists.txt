cmake_minimum_required(VERSION 3.20)
project(DaraWebGameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# -------------------------
# CPR (local subproject)
# -------------------------
set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cpr)

# -------------------------
# OpenSSL (needed for jwt-cpp RS256 verify)
# -------------------------
find_package(OpenSSL REQUIRED)

# -------------------------
# jwt-cpp (prefer vcpkg / system package, otherwise FetchContent)
# -------------------------
find_package(jwt-cpp CONFIG QUIET)

if (NOT jwt-cpp_FOUND)
    message(STATUS "jwt-cpp not found via find_package(), fetching with FetchContent...")
    FetchContent_Declare(
        jwt-cpp
        GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
        GIT_TAG v0.7.0
    )
    FetchContent_MakeAvailable(jwt-cpp)
endif()

# -------------------------
# DaraWebGameServer executable
# -------------------------
add_executable(DaraWebGameServer
    main.cpp
    parse.cpp
    combatant.cpp
    combatlog.cpp
    CombatDirector.cpp
    auth.cpp
)

target_include_directories(DaraWebGameServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DaraWebGameServer PRIVATE
    cpr::cpr
    OpenSSL::SSL
    OpenSSL::Crypto
)

# jwt-cpp target name differs depending on how it is provided.
# - vcpkg usually provides: jwt-cpp::jwt-cpp
# - FetchContent from upstream provides: jwt-cpp (CMake target)
if (TARGET jwt-cpp::jwt-cpp)
    target_link_libraries(DaraWebGameServer PRIVATE jwt-cpp::jwt-cpp)
elseif (TARGET jwt-cpp)
    target_link_libraries(DaraWebGameServer PRIVATE jwt-cpp)
else()
    message(FATAL_ERROR "jwt-cpp target not found after configuration.")
endif()

# pthread for Linux
if (UNIX)
    target_link_libraries(DaraWebGameServer PRIVATE pthread)
endif()

# =========================
# Testgame Executable
# =========================
add_executable(testgame
    testgame.cpp
)

target_include_directories(testgame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if (UNIX)
    target_link_libraries(testgame PRIVATE pthread)
endif()

