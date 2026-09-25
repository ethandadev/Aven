# Embeds binary files (fonts, default assets) into the engine binary so
# exported games never depend on loose engine data files.
#
# aven_embed_files(<target> FILES <file>...)
# Generates a source exposing aven::embedded::find(name) lookups.

set(AVEN_EMBED_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/AvenEmbedGenerate.cmake)

function(aven_embed_files target)
    cmake_parse_arguments(ARG "" "" "FILES" ${ARGN})
    set(out ${CMAKE_CURRENT_BINARY_DIR}/aven_embedded_data.cpp)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -DOUTPUT=${out} "-DFILES=${ARG_FILES}" -P ${AVEN_EMBED_SCRIPT}
        DEPENDS ${ARG_FILES} ${AVEN_EMBED_SCRIPT}
        COMMENT "Embedding engine data files"
        VERBATIM)
    target_sources(${target} PRIVATE ${out})
endfunction()
