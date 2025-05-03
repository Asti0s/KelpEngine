#include "Viewer/ShaderCompiler.hpp"
#include "Viewer/Vulkan/Device.hpp"
#include "Viewer/Vulkan/Utils.hpp"

#include <glslang/MachineIndependent/Versions.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

ShaderCompiler::FileIncluder::FileIncluder(std::filesystem::path currentDir) : m_currentDir(std::move(currentDir)) {}

glslang::TShader::Includer::IncludeResult* ShaderCompiler::FileIncluder::includeLocal(const char* headerName, const char* /*includerName*/, size_t /*inclusionDepth*/) {
    const std::filesystem::path filePath = m_currentDir / headerName;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        return new IncludeResult(headerName, ("Failed to open include file: " + filePath.string()).c_str(), 0, nullptr);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string* content = new std::string(buffer.str());

    return new IncludeResult(filePath.string(), content->c_str(), content->length(), content);  // NOLINT(cppcoreguidelines-owning-memory)
}

void ShaderCompiler::FileIncluder::releaseInclude(IncludeResult* result) {
    if (result != nullptr) {
        if (result->userData != nullptr)
            delete static_cast<std::string*>(result->userData); // NOLINT(cppcoreguidelines-owning-memory)
        delete result;  // NOLINT(cppcoreguidelines-owning-memory)
    }
}

VkShaderModule ShaderCompiler::compileShader(const std::shared_ptr<Device>& device, const std::string& path, EShLanguage stage, const char* preamble) {
    glslang::InitializeProcess();

    // Parameters
    constexpr int glslVersion = 460;
    constexpr bool forwardCompatible = false;
    constexpr EProfile profile = ECoreProfile;
    constexpr EShMessages messageFlags = EShMsgDefault;
    FileIncluder includer(std::filesystem::path(path).parent_path());

    // Load shader source
    std::ifstream file(path);
    if (!file.is_open()) {
        glslang::FinalizeProcess();
        throw std::runtime_error("Failed to compile shader \"" + path + "\": file not found");
    }

    const std::string shaderSourceStr((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::array<const char*, 1> shaderSource = { shaderSourceStr.c_str() };

    // Shader config
    glslang::TShader shader(stage);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4);
    shader.setEnvTarget(glslang::EshTargetSpv, glslang::EShTargetSpv_1_6);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, glslVersion);
    shader.setEntryPoint("main");
    shader.setStrings(shaderSource.data(), 1);
    if (preamble != nullptr)
        shader.setPreamble(preamble);

    // Preprocess
    std::string preprocessedShaderStr;
    if (!shader.preprocess(GetDefaultResources(), glslVersion, profile, false, forwardCompatible, messageFlags, &preprocessedShaderStr, includer)) {
        std::string errorMsg = "Failed to preprocess shader \"" + path + "\": " + shader.getInfoLog() + "\nDebug Log: " + shader.getInfoDebugLog();
        glslang::FinalizeProcess();
        throw std::runtime_error(errorMsg);
    }
    const std::array<const char*, 1> preprocessedShader = { preprocessedShaderStr.c_str() };

    // Parse
    shader.setStrings(preprocessedShader.data(), 1);
    if (!shader.parse(GetDefaultResources(), glslVersion, profile, false, forwardCompatible, messageFlags, includer)) {
        std::string errorMsg = "Failed to parse shader \"" + path + "\": " + shader.getInfoLog() + "\nDebug Log: " + shader.getInfoDebugLog();
        glslang::FinalizeProcess();
        throw std::runtime_error(errorMsg);
    }

    // Link
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messageFlags)) {
        std::string errorMsg = "Failed to link shader \"" + path + "\": " + program.getInfoLog() + "\nDebug Log: " + program.getInfoDebugLog();
        glslang::FinalizeProcess();
        throw std::runtime_error(errorMsg);
    }

    // Compile to SPIR-V
    std::vector<uint32_t> spirvCode;
    glslang::SpvOptions options{ .validate = true };
    spv::SpvBuildLogger logger; // Optional logger
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirvCode, &logger, &options);

    // Optional: Log SPIR-V generation messages
    const std::string messages = logger.getAllMessages();
    if (!messages.empty())
        std::cerr << "SPIR-V Generation Messages for \"" << path << "\":\n" << messages << std::endl;

    glslang::FinalizeProcess();

    // Shader module creation
    const VkShaderModuleCreateInfo shaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvCode.size() * sizeof(uint32_t),
        .pCode = spirvCode.data(),
    };

    VkShaderModule shaderModule{};
    VK_CHECK(vkCreateShaderModule(device->getHandle(), &shaderModuleCreateInfo, nullptr, &shaderModule));

    return shaderModule;
}
