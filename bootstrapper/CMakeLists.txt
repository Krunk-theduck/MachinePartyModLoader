set(EMBED_CONTENT "#include \"embedded.h\"\n")
set(EMBED_HEADER "#pragma once\n#include <cstddef>\n\n")

function(mpml_embed file symbol)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${file}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

    set(EMBED_CONTENT
        "${EMBED_CONTENT}extern const unsigned char ${symbol}[] = {${bytes}0x00};\nextern const size_t ${symbol}Size = sizeof(${symbol}) - 1;\n"
        PARENT_SCOPE
    )

    set(EMBED_HEADER
        "${EMBED_HEADER}extern const unsigned char ${symbol}[];\nextern const size_t ${symbol}Size;\n"
        PARENT_SCOPE
    )

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${file}"
    )
endfunction()

mpml_embed(bootstrap.gd      kBootstrapGd)
mpml_embed(example_mod.gd    kExampleModGd)
mpml_embed(example_mod.json kExampleModJson)
mpml_embed(example_gd.gd    kExampleLooseGd)

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/embedded.cpp"
    CONTENT "${EMBED_CONTENT}"
)

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/embedded.h"
    CONTENT "${EMBED_HEADER}"
)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded.h"
    PROPERTIES GENERATED TRUE
)

add_library(mpml_assets STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/embedded.cpp"
)

target_include_directories(mpml_assets PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)