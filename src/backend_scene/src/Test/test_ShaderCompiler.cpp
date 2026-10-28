#include <doctest.h>

#include "Vulkan/ShaderComp.hpp"
#include <array>
#include <cstdint>

using namespace wallpaper;
using namespace wallpaper::vulkan;

TEST_CASE("Compiler keeps vertex and fragment input environments separate") {
    REQUIRE(glslang::InitializeProcess());
    std::array<ShaderCompUnit, 2> units {
        ShaderCompUnit { .stage = EShLangVertex, .src = R"(#version 450
void main() { gl_Position = vec4(float(gl_VertexIndex), 0, 0, 1); }
)" },
        ShaderCompUnit { .stage = EShLangFragment, .src = R"(#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(gl_FragCoord.xy, 0, 1); }
)" },
    };
    ShaderCompOpt opt;
    opt.client_ver           = glslang::EShTargetVulkan_1_1;
    opt.relaxed_rules_vulkan = true;
    std::vector<Uni_ShaderSpv> shaders;
    const bool                 compiled = CompileAndLinkShaderUnits(units, opt, shaders);
    CHECK(compiled);
    if (compiled && shaders.size() == 2) {
        CHECK(shaders[0]->stage == ShaderType::VERTEX);
        CHECK(shaders[1]->stage == ShaderType::FRAGMENT);
        bool        has_frag_coord = false;
        const auto& words          = shaders[1]->spirv;
        for (size_t i = 5; i < words.size();) {
            const uint32_t count = words[i] >> 16;
            REQUIRE(count > 0);
            REQUIRE(i + count <= words.size());
            // OpDecorate target BuiltIn value
            if ((words[i] & 0xffff) == 71 && count >= 4 && words[i + 2] == 11) {
                CHECK(words[i + 3] != 42); // VertexIndex
                CHECK(words[i + 3] != 43); // InstanceIndex
                has_frag_coord |= words[i + 3] == 15;
            }
            i += count;
        }
        CHECK(has_frag_coord);
    } else {
        CHECK(shaders.size() == 2);
    }
    glslang::FinalizeProcess();
}
