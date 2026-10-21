#include <doctest.h>

#include "ParserTestFs.hpp"
#include "WPShaderParser.hpp"

using namespace wallpaper;

namespace
{
bool CompileFragment(const std::string& source) {
    WPShaderParser::InitGlslang();
    fs::VFS                     vfs;
    WPShaderInfo                info;
    std::vector<ShaderCode>     codes;
    std::array<WPShaderUnit, 1> units { WPShaderUnit { ShaderType::FRAGMENT, source, {} } };
    const bool ok = WPShaderParser::CompileToSpv("test", units, codes, vfs, &info, {});
    WPShaderParser::FinalGlslang();
    return ok && codes.size() == 1 && ! codes[0].empty();
}
} // namespace

TEST_SUITE("WPShaderParser") {
    TEST_CASE("HLSL math overloads and scalar vector declarators compile") {
        CHECK(CompileFragment(R"(
void main() {
    vec2 a = 0.0, b = .5, c = -1e-2;
    vec3 x = pow(vec3(0.5), 2.0);
    vec3 y = max(x, 0.0) + max(0.0, x) + min(x, 1.0) + min(1.0, x);
    float z = max(0, log10(10.0)) + min(1, 0.5);
    gl_FragColor = vec4(y + vec3(a + b + c, z), 1.0);
}
)"));
    }

    TEST_CASE("texture and vector expressions truncate at the initializer boundary") {
        CHECK(CompileFragment(R"(
uniform sampler2D g_Texture0;
varying vec3 v_TexCoord;
void main() {
    vec3 albedo = texSample2D(g_Texture0, v_TexCoord.xy), second = texSample2D(g_Texture0, vec2(0.0));
    vec2 offset = v_TexCoord - CAST2(vec3(0.0));
    vec4 wide = vec4(albedo, 1.0);
    vec2 narrow = wide * 0.5;
    gl_FragColor = vec4(albedo + second + vec3(offset + narrow, 0.0), 1.0);
}
)"));
    }

    TEST_CASE("valid scalar swizzles and nested scalar assignments keep their types") {
        CHECK(CompileFragment(R"(
uniform sampler2D g_Texture0;
float selectValue(float a, float b) { return a + b; }
void main() {
    vec3 wide = vec3(1.0);
    struct Data { float wide; } data;
    data.wide = 0.5;
    vec2 field = data . wide * vec2(1.0);
    vec3 shadowed = vec3(0.0);
    { int shadowed = 1; field += shadowed * vec2(1.0); }
    vec3 a = wide * vec2(0.5).x;
    vec3 b = wide * (vec2(0.5)).x;
    vec3 projected = wide - vec2(0.5) * mat3x2(1.0);
    float s = 0.0;
    vec2 c = vec2(selectValue(s = 1.0, 2.0));
    vec3 d = texture(g_Texture0, vec2(0.0)).gba, e = vec3(1.0);
    gl_FragColor = vec4(a + b + projected + d + e + vec3(c + field, 0.0), 1.0);
}
)"));
    }

    TEST_CASE("CRLF directives and an EOF without newline compile") {
        CHECK(
            CompileFragment("void main() {\r\n#if 1\r\ngl_FragColor = vec4(1.0);\r\n#endif\r\n}"));
    }

    TEST_CASE("includes follow complete structs even with comment braces") {
        fs::VFS           vfs;
        auto              files           = std::make_unique<ParserTestFs>();
        const std::string include         = "float shade(float x) { return x; }\n";
        files->files["/shaders/helper.h"] = { include.begin(), include.end() };
        REQUIRE(vfs.Mount("/assets", std::move(files)));
        WPShaderInfo info;
        const auto   source = WPShaderParser::PreShaderSrc(vfs,
                                                         R"(
#include "helper.h"
#define UNUSED_BRACE {
// {
struct Grid
{
    // } is not the end of this struct.
    vec2 pos;
};
void main() { Grid g; g.pos = vec2(1.0); gl_FragColor = vec4(shade(g.pos.x)); }
)",
                                                         &info,
                                                           {});
        CHECK(CompileFragment(source));
    }
    TEST_CASE("terminal include without a newline is loaded and compiles") {
        fs::VFS           vfs;
        auto              files           = std::make_unique<ParserTestFs>();
        const std::string include         = "#include \"math.h\"";
        const std::string nested          = "float shade(float x) { return x; }";
        files->files["/shaders/helper.h"] = { include.begin(), include.end() };
        files->files["/shaders/math.h"]   = { nested.begin(), nested.end() };
        REQUIRE(vfs.Mount("/assets", std::move(files)));
        WPShaderInfo info;
        std::string  source;
        REQUIRE_NOTHROW(
            source = WPShaderParser::PreShaderSrc(
                vfs,
                "void main() { gl_FragColor = vec4(shade(1.0)); }\n#include \"helper.h\"",
                &info,
                {}));
        CHECK(CompileFragment(source));
    }
}
