
cmake_minimum_required(VERSION 3.16)
project(KelpEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(BUILD_SHARED_LIBS FALSE) # Build all libraries as static
set(SPIRV_WERROR OFF) # Disable warnings as errors for SPIRV-Tools (due to a deprecated enum conversion in the code)

# ImGUI sources
add_compile_definitions(IMGUI_IMPL_VULKAN_USE_VOLK)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Submodules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/fastgltf                  ${CMAKE_BINARY_DIR}/fastgltf)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/glfw                      ${CMAKE_BINARY_DIR}/glfw)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/glm                       ${CMAKE_BINARY_DIR}/glm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/SPIRV-Headers             ${CMAKE_BINARY_DIR}/SPIRV-Headers)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/SPIRV-Tools               ${CMAKE_BINARY_DIR}/SPIRV-Tools)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/glslang                   ${CMAKE_BINARY_DIR}/glslang)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/volk                      ${CMAKE_BINARY_DIR}/volk)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator     ${CMAKE_BINARY_DIR}/VulkanMemoryAllocator)

# Sources
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Build
add_executable(KelpEngine ${SOURCES} ${IMGUI_SOURCES})

# Link libraries
target_link_libraries(KelpEngine PRIVATE
    fastgltf
    glfw
    glm
    glslang::glslang
    glslang::glslang-default-resource-limits
    volk
    GPUOpen::VulkanMemoryAllocator
)

# Include directories
target_include_directories(KelpEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
    ${IMGUI_DIR}
)
