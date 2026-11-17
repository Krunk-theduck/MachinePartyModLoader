include(FetchContent)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        v1.3.3
)

FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
  FetchContent_Populate(minhook)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(HDE_SRC ${minhook_SOURCE_DIR}/src/hde/hde64.c)
else()
  set(HDE_SRC ${minhook_SOURCE_DIR}/src/hde/hde32.c)
endif()

set(MOVSB_SHIM "")
if(NOT MSVC AND NOT MINGW AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
  set(MOVSB_SHIM ${CMAKE_CURRENT_SOURCE_DIR}/movsb_shim.c)
endif()

add_library(minhook_objs STATIC
  ${MOVSB_SHIM}
  ${minhook_SOURCE_DIR}/src/buffer.c
  ${minhook_SOURCE_DIR}/src/hook.c
  ${minhook_SOURCE_DIR}/src/trampoline.c
  ${HDE_SRC}
)

target_include_directories(minhook_objs PUBLIC
  ${minhook_SOURCE_DIR}/include
)

add_library(mpml_proxy SHARED
  dllmain.cpp
)

set(NASM_EXECUTABLE "${CMAKE_SOURCE_DIR}/Scripts/nasm/nasm.exe")

if(NOT EXISTS "${NASM_EXECUTABLE}")
    message(FATAL_ERROR "NASM not found: ${NASM_EXECUTABLE}")
endif()

set(NASM_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/winmm_proxy_nasm.obj")

add_custom_command(
    OUTPUT "${NASM_OBJECT}"
    COMMAND "${NASM_EXECUTABLE}"
            -f win64
            "${CMAKE_CURRENT_SOURCE_DIR}/winmm_proxy.nasm"
            -o "${NASM_OBJECT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/winmm_proxy.nasm"
    VERBATIM
)

set_source_files_properties(
    "${NASM_OBJECT}"
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE
)

target_sources(mpml_proxy PRIVATE
    "${NASM_OBJECT}"
    winmm_proxy.cpp
    winmm_proxy_globals.cpp
    winmm_proxy.def
    "${CMAKE_BINARY_DIR}/version_proxy.rc"
)

target_link_libraries(mpml_proxy PRIVATE
  mpml_patch
  mpml_logger
  minhook_objs
)

set_target_properties(mpml_proxy PROPERTIES
  OUTPUT_NAME "winmm"
  PREFIX ""
)

if(MINGW)
  target_link_options(mpml_proxy PRIVATE -static)
endif()
