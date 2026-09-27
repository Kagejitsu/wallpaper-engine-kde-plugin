#pragma once
#include <unordered_map>

#include "SceneTexture.h"
#include "SceneRenderTarget.h"
#include "SceneNode.h"
#include "SceneLight.hpp"

#include "Core/NoCopyMove.hpp"

namespace wallpaper
{
class ParticleSystem;
class IShaderValueUpdater;
class IImageParser;

namespace fs
{
class VFS;
}
class ScriptEngine;
namespace audio
{
class AudioCapture;
}

class Scene : NoCopy, NoMove {
public:
    Scene();
    ~Scene();

    std::unordered_map<std::string, SceneTexture>      textures;
    std::unordered_map<std::string, SceneRenderTarget> renderTargets;

    // static-pass caching: whether a render target is written only by passes
    // whose output never changes after the first frame. Filled during prepare
    // (topological order). cache_passes gates the whole optimization.
    bool                                  cache_passes { true };
    std::unordered_map<std::string, bool> rt_frame_static;
    // how many passes of the current graph write each render target
    std::unordered_map<std::string, int> rt_writer_count;

    std::unordered_map<std::string, std::shared_ptr<SceneCamera>> cameras;
    std::unordered_map<std::string, std::vector<std::string>>     linkedCameras;

    std::vector<std::unique_ptr<SceneLight>> lights;

    std::shared_ptr<SceneNode>           sceneGraph;
    // transform-only nodes (WE group objects) that are not drawn but parent others
    std::vector<std::shared_ptr<SceneNode>> auxNodes;
    // SceneScript engine, if the scene has any script bindings
    std::shared_ptr<ScriptEngine> scriptEngine;
    // system-audio spectrum, acquired lazily when a shader or script asks for it
    std::shared_ptr<audio::AudioCapture> audioCapture;
    std::unique_ptr<IShaderValueUpdater> shaderValueUpdater;
    std::unique_ptr<IImageParser>        imageParser;
    std::unique_ptr<fs::VFS>             vfs;

    std::string scene_id { "unknown_id" };

    bool first_frame_ok { false };

    SceneMesh default_effect_mesh;

    std::unique_ptr<ParticleSystem> paritileSys;

    SceneCamera* activeCamera;

    i32                  ortho[2] { 1920, 1080 }; // w, h
    std::array<float, 3> clearColor { 1.0f, 1.0f, 1.0f };
    // 0 when the bottom layer is played underneath by the host (video underlay)
    float clearAlpha { 1.0f };

    double elapsingTime { 0.0f }, frameTime { 0.0f };
    void   PassFrameTime(double t) {
        frameTime = t;
        elapsingTime += t;
    }

    void UpdateLinkedCamera(const std::string& name) {
        if (linkedCameras.count(name) != 0) {
            auto& cams = linkedCameras.at(name);
            for (auto& cam : cams) {
                if (cameras.count(cam) != 0) {
                    cameras.at(cam)->Clone(*cameras.at(name));
                    cameras.at(cam)->Update();
                }
            }
        }
    }
};
} // namespace wallpaper
