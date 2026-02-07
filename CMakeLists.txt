cmake_minimum_required(VERSION 3.20)
project(DaraWebGameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


include(FetchContent)

# -------------------------
# Build types (default Debug for single-config generators)
# -------------------------
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

# Optional: enable sanitizers in Debug (GCC/Clang)
option(DARA_SANITIZE "Enable sanitizers in Debug" OFF)

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

# =========================
# DaraWebGameServer executable
# =========================
add_executable(DaraWebGameServer
    main.cpp
    parse.cpp
    combatant.cpp
    combatlog.cpp
    CombatDirector.cpp
    Wave.cpp
    uistate.cpp
    auth.cpp
    sessions.cpp
    MobTemplateStore.cpp
    character.cpp
    CharacterRepository.cpp
    CharacterDbWorker.cpp
    DbJobQueue.cpp
    ServerOptions.cpp
)

target_include_directories(DaraWebGameServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DaraWebGameServer PRIVATE
    cpr::cpr
    OpenSSL::SSL
    OpenSSL::Crypto
    mysqlcppconn
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

# -------------------------
# Debug / Release compile flags + defines (per-config)
# -------------------------
target_compile_options(DaraWebGameServer PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    $<$<CONFIG:Release>:-O3>
)

target_compile_definitions(DaraWebGameServer PRIVATE
    $<$<CONFIG:Debug>:DARA_DEBUG=1 DARA_DEBUG_COMBATLOG=1>
    $<$<CONFIG:RelWithDebInfo>:DARA_DEBUG=1>
    $<$<CONFIG:Release>:DARA_DEBUG=0>
)

# -------------------------
# Optional sanitizers (Debug only, GCC/Clang)
# -------------------------
if (DARA_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(DaraWebGameServer PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
    target_link_options(DaraWebGameServer PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
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

# Optional: same warnings/debug defines for testgame
target_compile_options(testgame PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    $<$<CONFIG:Release>:-O3>
)

target_compile_definitions(testgame PRIVATE
    $<$<CONFIG:Debug>:DARA_DEBUG=1>
    $<$<CONFIG:RelWithDebInfo>:DARA_DEBUG=1>
    $<$<CONFIG:Release>:DARA_DEBUG=0>
)

if (DARA_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(testgame PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
    target_link_options(testgame PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
endif()

