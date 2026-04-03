cmake_minimum_required(VERSION 3.24)
project(CymatiX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Vulkan
find_package(Vulkan REQUIRED)

# Third-party via FetchContent 
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# miniaudio (single-header; we use FetchContent_Populate so we can add the
# source dir to includes without running its CMakeLists)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

# KissFFT
FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        131.1.0
    GIT_SHALLOW    TRUE
)
set(KISSFFT_DATATYPE "float" CACHE STRING "" FORCE)
set(KISSFFT_STATIC  ON  CACHE BOOL  "" FORCE)
set(KISSFFT_TEST    OFF CACHE BOOL  "" FORCE)
set(KISSFFT_TOOLS   OFF CACHE BOOL  "" FORCE)
set(KISSFFT_PKGCONFIG   OFF CACHE BOOL  "" FORCE)
set(KISSFFT_INSTALL     OFF CACHE BOOL  "" FORCE)
FetchContent_MakeAvailable(kissfft)

# Shaders
include(cmake/CompileShaders.cmake)

#  Sources 
add_executable(cymatics
    src/main.cpp
    src/app/App.cpp
    src/rhi/VulkanContext.cpp
    src/rhi/Swapchain.cpp
    src/rhi/RenderPass.cpp
    src/rhi/Pipeline.cpp
    src/rhi/CommandPool.cpp
    src/rhi/Sync.cpp
    src/audio/AudioEngine.cpp
    src/audio/FFTProcessor.cpp
    src/visuals/LissajousStyle.cpp
    src/visuals/WaveInterferenceStyle.cpp
    src/visuals/VisualStyleRegistry.cpp
    src/palette/BioluminescentPalette.cpp
)

set_target_properties(cymatics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY    ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG  ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE    ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_SOURCE_DIR}
)

target_include_directories(cymatics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${miniaudio_SOURCE_DIR}
)

target_link_libraries(cymatics PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    kissfft::kissfft-float
)

if(UNIX)
    # miniaudio needs dynamic loader and threads on Linux/macOS
    target_link_libraries(cymatics PRIVATE dl pthread)
endif()

# absolute shader path in at compile time 
# 
target_compile_definitions(cymatics PRIVATE
    "SHADER_DIR=\"${CMAKE_SOURCE_DIR}/shaders/compiled\""
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)

if(WIN32)
    target_compile_definitions(cymatics PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Shaders must be compiled before the executable is linked
add_dependencies(cymatics shaders)
