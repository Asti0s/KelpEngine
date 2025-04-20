#pragma once

#include "Vulkan/Device.hpp"

#include <glslang/Public/ShaderLang.h>

#include <filesystem>
#include <memory>
#include <string>

class ShaderCompiler {
    public:
        ShaderCompiler() = delete;

        /**
         * @brief Compiles a GLSL shader file into a Vulkan SPIR-V shader module.
         * Handles #include directives relative to the shader file's directory.
         *
         * @param device A shared pointer to the Vulkan logical device.
         * @param path The filesystem path to the GLSL shader source file.
         * @param stage The specific shader stage (e.g., EShLangVertex, EShLangFragment).
         * @param preamble An optional string prepended to the shader source before compilation (e.g., for #defines). Defaults to nullptr.
         * @return A VkShaderModule handle representing the compiled SPIR-V code. The caller is responsible for destroying this module.
         * @throws std::runtime_error if the file cannot be opened or if any compilation stage (preprocessing, parsing, linking, SPIR-V generation) fails.
         */
         static VkShaderModule compileShader(const std::shared_ptr<Device>& device, const std::string& path, EShLanguage stage, const char* preamble = "");


    private:
        class FileIncluder : public glslang::TShader::Includer {
            public:
                explicit FileIncluder(std::filesystem::path currentDir);
                IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override;
                void releaseInclude(IncludeResult* result) override;

            private:
                std::filesystem::path m_currentDir;
        };
};
