cmake_minimum_required(VERSION 3.24)
project(MPML C CXX)

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

add_subdirectory(Global)  # mpml_util + mpml_settings + mpml_logger
add_subdirectory(patch)   # mpml core (patcher, pck, gd check)
add_subdirectory(main)    # launcher
if(WIN32)
  add_subdirectory(proxy) # winmm.dll proxy (MinHook CreateFileW redirect)
endif()

# The version lives in exactly one place (mpml::kVersion in patch/version.h);
# regenerate the Windows version resources from it on every configure.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/patch/version.h" MPML_VERSION_H)
string(REGEX MATCH "kVersion\\[\\] *= *\"([0-9]+)\"" MPML_VER_MATCH "${MPML_VERSION_H}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "cannot read the loader version from patch/version.h")
endif()
set(MPML_VER "${CMAKE_MATCH_1}")
set(MPML_VER_DOT "${MPML_VER}.0.0.0")
string(REPLACE "." "," MPML_VER_COMMA "${MPML_VER_DOT}")

set(MPML_DESC "Machine Party mod loader")
set(MPML_ORIG "MachinePartyModLoader.exe")
configure_file("Scripts/version_info.txt.in" "${CMAKE_BINARY_DIR}/version_loader.rc" @ONLY)

set(MPML_DESC "Machine Party mod loader (winmm proxy)")
set(MPML_ORIG "winmm.dll")
configure_file("Scripts/version_info.txt.in" "${CMAKE_BINARY_DIR}/version_proxy.rc" @ONLY)
