# Shared code between the launcher and the proxy DLL.

# mpml_util - the few file helpers asp2 does not ship (exe path, hashing,
# sorted listings, tree removal). Links asp.
add_library(mpml_util STATIC util/util.cpp)
target_link_libraries(mpml_util PRIVATE mpml_warnings)
target_include_directories(mpml_util PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mpml_util PUBLIC asp)

# mpml_settings - reads/writes MachinePartyModLoader/loader.cfg (console=...).
add_library(mpml_settings STATIC settings/settings.cpp)
target_link_libraries(mpml_settings PRIVATE mpml_warnings)
target_link_libraries(mpml_settings PUBLIC mpml_util)

# mpml_logger - the tiny logger shared by the launcher and the proxy DLL.
add_library(mpml_logger STATIC logger/logger.cpp)
target_link_libraries(mpml_logger PRIVATE mpml_warnings)
target_link_libraries(mpml_logger PUBLIC mpml_util)

# mpml_dirs - set of files
add_library(mpml_dirs STATIC Dirs/paths.cpp)
target_link_libraries(mpml_dirs PRIVATE mpml_warnings)
target_include_directories(mpml_dirs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mpml_dirs PUBLIC asp)
target_link_libraries(mpml_util PRIVATE mpml_dirs)
