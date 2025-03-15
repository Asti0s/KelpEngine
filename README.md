# KelpEngine

KelpEngine is a real-time ray tracing renderer built using Vulkan and C++. The project aims to leverage modern rendering techniques and hardware acceleration to deliver high-quality real-time ray tracing. KelpEngine utilizes cutting-edge technologies and libraries to achieve its goals, including NVIDIA's RTXDI, RTXGI, SHARC, NRC and DLSS SDKs for advanced ray tracing and denoising.
This project is in its early stages and demos will be provided as development progresses.

## Planned Features

- **Opacity Micro Maps (OMM)**: For efficient handling of alpha textures in ray tracing.
- **ReSTIR DI & GI**: Spatiotemporal reservoir-based importance sampling for direct and global illumination.
- **SHARC & NRC**: Spatial hash radiance caching & Neural radiance caching for real-time global illumination.
- **Ray Reordering**: Optimizing path tracing performance by reordering rays.
- **DLSS Ray Reconstruction**: Advanced denoising using NVIDIA's DLSS Ray Reconstruction technology.

## Technologies Used

- **Vulkan**: Low-level graphics API for high-performance rendering.
- **C++**: Core programming language for the engine.
- **glslang**: Runtime shader compilation.
- **GLFW**: Windowing and input handling.
- **GLM**: Mathematics library for graphics programming.
- **Volk**: Vulkan loader for efficient API usage.
- **stb**: Image loading and parsing.
- **fastgltf**: Fast and efficient GLTF model loading.
- **ImGui**: Immediate mode GUI for user interfaces.
- **VMA**: Vulkan Memory Allocator for efficient memory management.
- **NVIDIA SDKs**: RTXDI, RTXGI, and DLSS for advanced ray tracing and denoising.

## How to Build

To build KelpEngine, follow these steps:

1. **Clone the repository**
   ```bash
   git clone https://github.com/Asti0s/KelpEngine --recursive
   cd KelpEngine
   ```

2. **Create a build directory**
    ```bash
    mkdir build
    cd build
    ```

3. **Generate build files with cmake**
    ```bash
    cmake ..
    ```

4. **Build the project**
    ```bash
    cmake --build . --parallel
    ```

4. **Build the project**
    After the build process completes, you can run the engine from the build directory.
