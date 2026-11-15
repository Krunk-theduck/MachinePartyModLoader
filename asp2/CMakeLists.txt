# asp2 - the one file API everything uses. Headers are included as
# "asp2/fs.h"; the include dir is this folder's parent (the repo root).
add_library(asp2 STATIC fs.cpp)
target_include_directories(asp2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
