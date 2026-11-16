cmake_minimum_required(VERSION 3.24)
project(MPML C CXX)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" MPML_VERSION_LINES)

foreach(LINE IN LISTS MPML_VERSION_LINES)
    if(LINE MATCHES "^Version *= *\"([^\"]+)\"$")
        set(kMPMLVersion "${CMAKE_MATCH_1}")
    elseif(LINE MATCHES "^API *= *\"([^\"]+)\"$")
        set(kMPMLApiVersion "${CMAKE_MATCH_1}")
    endif()
endforeach()

if(NOT DEFINED kMPMLVersion OR NOT DEFINED kMPMLApiVersion)
    message(FATAL_ERROR "Missing Version or API in version.txt")
endif()

# Validate version components
if(NOT kMPMLVersion MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR "Invalid version '${kMPMLVersion}'. Expected numeric version components.")
endif()

string(REPLACE "." ";" MPML_VERSION_PARTS "${kMPMLVersion}")
list(LENGTH MPML_VERSION_PARTS MPML_VERSION_PARTS_COUNT)

if(MPML_VERSION_PARTS_COUNT GREATER 4)
    message(FATAL_ERROR "Invalid version '${kMPMLVersion}'. Maximum of 4 components allowed.")
endif()

# Pad version to exactly four components
while(MPML_VERSION_PARTS_COUNT LESS 4)
    list(APPEND MPML_VERSION_PARTS 0)
    math(EXPR MPML_VERSION_PARTS_COUNT "${MPML_VERSION_PARTS_COUNT} + 1")
endwhile()

# Generate Windows VERSIONINFO formats
string(JOIN "." MPML_VER_DOT ${MPML_VERSION_PARTS})
string(JOIN "," MPML_VER_COMMA ${MPML_VERSION_PARTS})

# Export version definitions
add_compile_definitions(
    MPML_VERSION="${kMPMLVersion}"
    MPML_API_VERSION="${kMPMLApiVersion}"
    MPML_VERSION_DOT="${MPML_VER_DOT}"
    MPML_VERSION_COMMA=${MPML_VER_COMMA}
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
  add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
  )
  enable_language(RC)
endif()
include(FetchContent)
FetchContent_Declare(asp2
  GIT_REPOSITORY https://github.com/dankmeme01/asp2.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asp2) # static target: asp
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(MINIZ_STANDALONE_PROJECT OFF)
FetchContent_Declare(miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG        3.0.2
  GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF)
set(BUILD_FUZZERS OFF)
set(AMALGAMATE_SOURCES OFF)
set(BUILD_HEADER_ONLY OFF)
set(INSTALL_PROJECT OFF)
FetchContent_MakeAvailable(miniz) # static target: miniz
unset(CMAKE_POLICY_VERSION_MINIMUM)

# Warning level for OUR targets only (third-party code builds with its own).
add_library(mpml_warnings INTERFACE)
if(MSVC)
  target_compile_options(mpml_warnings INTERFACE /W4)
else()
  target_compile_options(mpml_warnings INTERFACE -Wall -Wextra)
endif()

add_subdirectory(global)  # mpml_util + mpml_settings + mpml_logger + mpml_dirs
add_subdirectory(patch)   # mpml core (patcher, pck, gd check)
add_subdirectory(main)    # launcher

if(WIN32)
  add_subdirectory(proxy) # winmm.dll proxy (MinHook CreateFileW redirect)
endif()

set(MPML_DESC "Machine Party mod loader")
set(MPML_ORIG "MachinePartyModLoader.exe")
configure_file("Scripts/version_info.txt.in" "${CMAKE_BINARY_DIR}/version_loader.rc" @ONLY)

set(MPML_DESC "Machine Party mod loader (winmm proxy)")
set(MPML_ORIG "winmm.dll")
configure_file("Scripts/version_info.txt.in" "${CMAKE_BINARY_DIR}/version_proxy.rc" @ONLY)
