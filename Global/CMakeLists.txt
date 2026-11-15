# mpml_logger - the tiny logger shared by the launcher and the proxy DLL.
add_library(mpml_logger STATIC logger/logger.cpp)
target_include_directories(mpml_logger PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mpml_logger PUBLIC asp2)
