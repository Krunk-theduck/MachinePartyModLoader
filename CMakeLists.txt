cmake_minimum_required(VERSION 3.24)
project(MPML C CXX)

# asp2 uses C++23 features in its public headers (deducing this), so every
# target that touches it builds as C++23. MSVC: VS 2022 17.13+ recommended.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT on MSVC: no vcruntime DLL dependency, and the proxy DLL can run before anything else loads.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  enable_language(RC) # version resources for the launcher and the proxy DLL
endif()

# ---------------------------------------------------------------------------
# asp2 - https://github.com/dankmeme01/asp2 (include/asp/fs)
#
# The one file API everything uses (asp::fs::Path / File / BufferedFile / ...).
# Fetched at configure time and pinned to a known commit so builds stay
# reproducible. Its own dependencies (fmt, geode::result, nontype_functional)
# are pulled by asp2 itself through CPM. Network access is needed once; CMake
# caches the sources afterwards.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(asp2
  GIT_REPOSITORY https://github.com/dankmeme01/asp2.git
  GIT_TAG        28d057e964dddad5fbadefbdb9ecb70b5d5b03d5
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asp2) # static target: asp

# ---------------------------------------------------------------------------
# miniz - reads .zip mods inside the --compile script check
# (the game itself opens zips at runtime with Godot's ZIPReader).
# miniz 3.0.2's CMakeLists predates CMake 4's minimum-version cutoff, so tell
# newer CMake to accept it. Older CMake simply ignores the variable.
# ---------------------------------------------------------------------------
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
