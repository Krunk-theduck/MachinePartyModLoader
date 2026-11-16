add_library(mpml_patch STATIC patch.cpp pck.cpp md5.cpp gdcheck.cpp)
target_include_directories(mpml_patch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mpml_patch PUBLIC asp mpml_util mpml_settings miniz mpml_assets)
target_link_libraries(mpml_patch PRIVATE mpml_warnings)