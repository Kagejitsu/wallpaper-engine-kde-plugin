#include <doctest.h>

#include "Audio/SoundManager.h"
#include "ParserTestFs.hpp"
#include "Particle/ParticleSystem.h"
#include "WPSceneParser.hpp"
#include <nlohmann/json.hpp>

using namespace wallpaper;

namespace
{
std::unique_ptr<ParserTestFs> TestFiles() {
    auto                                      fs      = std::make_unique<ParserTestFs>();
    const std::pair<std::string, std::string> files[] = {
        { "/models/image.json", R"({"material":"materials/image.json","width":64,"height":64})" },
        { "/materials/image.json", R"({"passes":[{"shader":"test"}]})" },
        { "/particles/test.json",
          R"({"material":"materials/image.json","maxcount":2,"starttime":0,"emitter":[{"name":"boxrandom","id":0}]})" },
        { "/shaders/test.vert",
          "attribute vec3 a_Position; void main() { gl_Position = vec4(a_Position, 1.0); }" },
        { "/shaders/test.frag", "uniform vec4 g_Color4; void main() { gl_FragColor = g_Color4; }" },
    };
    for (const auto& [path, content] : files) {
        fs->files[path] = std::vector<uint8_t>(content.begin(), content.end());
    }
    return fs;
}

nlohmann::json TestScene() {
    return nlohmann::json::parse(R"({
        "camera":{"center":"32 32 0","eye":"32 32 1","up":"0 1 0"},
        "general":{"clearcolor":"0 0 0","cameraparallax":false,
                   "cameraparallaxamount":0,"cameraparallaxdelay":1,
                   "cameraparallaxmouseinfluence":0,
                   "orthogonalprojection":{"width":64,"height":64}},
        "objects":[
            {"id":1,"image":"models/image.json","origin":"32 32 0",
             "angles":"0 0 0","scale":"1 1 1",
             "color":{"user":"backgroundcolor","value":"0.7 0.6 0.5"},
             "visible":{"user":"imagevisible","value":true}},
            {"id":2,"particle":"particles/test.json","origin":"32 32 0",
             "angles":"0 0 0","scale":"1 1 1",
             "visible":{"user":"particlesvisible","value":true}}
        ]})");
}

std::shared_ptr<Scene> ParseScene(fs::VFS& vfs, const std::string& overrides) {
    audio::SoundManager sound;
    WPSceneParser       parser;
    return parser.Parse("test", TestScene().dump(), vfs, sound, overrides);
}
} // namespace

TEST_CASE("scene user overrides change image uniforms and keep invisible images offscreen") {
    fs::VFS vfs;
    REQUIRE(vfs.Mount("/assets", TestFiles()));
    auto scene = ParseScene(vfs, R"({"backgroundcolor":"0 0 0","imagevisible":false})");
    REQUIRE(scene);
    auto image = scene->sceneGraph->GetChildren().end();
    for (auto it = scene->sceneGraph->GetChildren().begin();
         it != scene->sceneGraph->GetChildren().end();
         ++it) {
        if ((*it)->HasMaterial() && ! (*it)->Mesh()->Dynamic()) image = it;
    }
    REQUIRE(image != scene->sceneGraph->GetChildren().end());
    CHECK((*image)->IsOffscreen());
    const auto& uniforms = (*image)->Mesh()->Material()->customShader.constValues;
    REQUIRE(uniforms.count("g_Color4") == 1);
    CHECK(uniforms.at("g_Color4")[0] == 0.0f);
    CHECK(uniforms.at("g_Color4")[1] == 0.0f);
    CHECK(uniforms.at("g_Color4")[2] == 0.0f);
    CHECK(scene->paritileSys->subsystems.size() == 1);
}

TEST_CASE("scene user visibility removes particle systems") {
    fs::VFS vfs;
    REQUIRE(vfs.Mount("/assets", TestFiles()));
    auto visible = ParseScene(vfs, "{}");
    REQUIRE(visible);
    REQUIRE(visible->paritileSys->subsystems.size() == 1);
    auto hidden = ParseScene(vfs, R"({"particlesvisible":false})");
    REQUIRE(hidden);
    CHECK(hidden->paritileSys->subsystems.empty());
}

TEST_CASE("loose project metadata supplies defaults for packed scenes and overrides win") {
    fs::VFS vfs;
    REQUIRE(vfs.Mount("/assets", TestFiles()));
    auto              project       = std::make_unique<ParserTestFs>();
    const std::string metadata      = R"({"general":{"properties":{
        "particlesvisible":{"value":false},"backgroundcolor":{"value":"0.2 0.4 0.6"}
    }}})";
    project->files["/project.json"] = { metadata.begin(), metadata.end() };
    REQUIRE(vfs.Mount("/project", std::move(project)));
    auto defaults = ParseScene(vfs, "");
    REQUIRE(defaults);
    CHECK(defaults->paritileSys->subsystems.empty());
    bool found = false;
    for (const auto& node : defaults->sceneGraph->GetChildren()) {
        if (node->HasMaterial() && ! node->Mesh()->Dynamic()) {
            const auto& color = node->Mesh()->Material()->customShader.constValues.at("g_Color4");
            CHECK(color[0] == doctest::Approx(0.2f));
            CHECK(color[1] == doctest::Approx(0.4f));
            CHECK(color[2] == doctest::Approx(0.6f));
            found = true;
        }
    }
    CHECK(found);
    auto override = ParseScene(vfs, R"({"particlesvisible":true})");
    REQUIRE(override);
    CHECK(override->paritileSys->subsystems.size() == 1);
}
