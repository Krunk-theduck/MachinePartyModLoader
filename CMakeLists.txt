cmake_minimum_required(VERSION 3.20)
project(MPML C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT on MSVC: no vcruntime DLL dependency, and the proxy DLL can run before anything else loads.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  enable_language(RC) # version resources for the launcher and the proxy DLL
endif()

add_subdirectory(asp2)    # asp2::fs - the file API everything uses
add_subdirectory(Global)  # mpml_logger - shared by launcher + proxy
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
