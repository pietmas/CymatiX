find_program(GLSLC glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED
)

set(SHADER_SRC_DIR  ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR  ${CMAKE_SOURCE_DIR}/shaders/compiled)

file(MAKE_DIRECTORY ${SHADER_BIN_DIR})


set(SHADERS
    ${SHADER_SRC_DIR}/triangle.vert
    ${SHADER_SRC_DIR}/triangle.frag
    ${SHADER_SRC_DIR}/lissajous.vert
    ${SHADER_SRC_DIR}/lissajous.frag
    ${SHADER_SRC_DIR}/wave_interference.vert
    ${SHADER_SRC_DIR}/wave_interference.frag
)

set(SHADER_SPV_FILES)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${SHADER_BIN_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT  ${SPV}
        COMMAND ${GLSLC} ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME} → ${SHADER_NAME}.spv"
    )

    list(APPEND SHADER_SPV_FILES ${SPV})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_SPV_FILES})
