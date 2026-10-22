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
add_library(notegran_core STATIC src/core/Assigner.cpp src/core/Clock.cpp src/core/NoteSet.cpp src/core/OwnedNotes.cpp src/core/Pitch.cpp
    src/core/Random.cpp src/core/Shape.cpp src/core/Cloud.cpp src/core/Scales.cpp src/core/Chords.cpp src/core/Grid.cpp src/core/Burst.cpp)
target_include_directories(notegran_core PUBLIC src)
target_link_libraries(notegran_core PUBLIC uw::ump)
set_target_properties(notegran_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(notegran_core PRIVATE -Wall -Wextra -Wconversion)
endif()

# --- the .clap ---------------------------------------------------------------------------------
# Hidden visibility, so clap_entry (CLAP_EXPORT in plugin.cpp) is the only exported symbol and two
# plugins with different uw-core pins can't bind to each other's copies.
add_library(NoteGran MODULE src/plugin.cpp)
target_include_directories(NoteGran PRIVATE external/clap/include)
target_link_libraries(NoteGran PRIVATE notegran_core)
target_compile_definitions(NoteGran PRIVATE NOTEGRAN_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(NoteGran PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(NoteGran PRIVATE -Wall -Wextra)
endif()

if(APPLE)
    set_target_properties(NoteGran PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unlikelyware.notegran"
        MACOSX_BUNDLE_BUNDLE_NAME "NoteGran"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(NOTEGRAN_CLAP_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    set(NOTEGRAN_CLAP_ARTIFACT "$<TARGET_BUNDLE_DIR:NoteGran>")
else()
    set_target_properties(NoteGran PROPERTIES PREFIX "" SUFFIX ".clap")
    set(NOTEGRAN_CLAP_DIR "$ENV{HOME}/.clap")
    set(NOTEGRAN_CLAP_ARTIFACT "$<TARGET_FILE:NoteGran>")
endif()

# A separate target, as in the synths, so an ordinary build never writes outside the build tree.
add_custom_target(install-clap
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTEGRAN_CLAP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${NOTEGRAN_CLAP_DIR}/NoteGran.clap"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${NOTEGRAN_CLAP_ARTIFACT}" "${NOTEGRAN_CLAP_DIR}/NoteGran.clap"
    DEPENDS NoteGran
    VERBATIM
    COMMENT "Symlinking NoteGran.clap into ${NOTEGRAN_CLAP_DIR}")

# --- tests -------------------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(notegran-core-tests test/CoreTests.cpp)
    target_link_libraries(notegran-core-tests PRIVATE notegran_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(notegran-core-tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME notegran-core COMMAND notegran-core-tests)

    # Drives the built .clap over the CLAP C API: no DAW, no GUI.
    add_executable(notegran-clap-tests test/ClapTests.cpp)
    target_include_directories(notegran-clap-tests PRIVATE external/clap/include)
    target_link_libraries(notegran-clap-tests PRIVATE uw::ump ${CMAKE_DL_LIBS})
    add_dependencies(notegran-clap-tests NoteGran)
    add_test(NAME notegran-clap COMMAND notegran-clap-tests "${NOTEGRAN_CLAP_ARTIFACT}")
endif()
