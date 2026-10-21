cmake_minimum_required(VERSION 3.21)

project(NoteGran VERSION 0.1.0 LANGUAGES CXX)

# The core will be linked into the .clap, a shared module (phase 2), so it must be
# position-independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- uw-core (git submodule): only uw::ump, no CLAP host, JACK or ALSA ---------------------
# NOTEGRAN_UW_CORE_DIR points at another uw-core clone (e.g. ~/Dev-Common/uw-core) to build
# against a change that isn't pushed and pinned yet.
set(NOTEGRAN_UW_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/uw-core" CACHE PATH "uw-core source")
set(UW_CORE_UMP_ONLY ON CACHE BOOL "" FORCE)
set(UW_CORE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${NOTEGRAN_UW_CORE_DIR} uw-core)

# --- core: the shared note logic over uw::ump, no CLAP ----------------------------------------
add_library(notegran_core STATIC src/core/Clock.cpp src/core/NoteSet.cpp src/core/OwnedNotes.cpp src/core/Pitch.cpp
    src/core/Random.cpp src/core/Shape.cpp src/core/Cloud.cpp)
target_include_directories(notegran_core PUBLIC src)
target_link_libraries(notegran_core PUBLIC uw::ump)
set_target_properties(notegran_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(notegran_core PRIVATE -Wall -Wextra -Wconversion)
endif()

# --- tests -------------------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(notegran-core-tests test/CoreTests.cpp)
    target_link_libraries(notegran-core-tests PRIVATE notegran_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(notegran-core-tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME notegran-core COMMAND notegran-core-tests)
endif()
