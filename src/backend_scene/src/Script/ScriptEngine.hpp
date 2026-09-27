#pragma once
// SceneScript: Wallpaper Engine's per-property JavaScript, run with quickjs-ng.
//
// A script is attached to one property of one layer ("origin", "angles",
// "scale", "visible", "alpha"). Each frame the host calls update(value) with
// the property's current value and writes the returned value back. init() runs
// once after the module is evaluated. Event hooks (cursorClick, ...) are
// called when the host reports the event.
//
// All scripts of a scene share one runtime and one global context; per-script
// state (thisLayer, scriptProperties) is swapped in around each call. Scripts
// are untrusted: memory is capped, and a per-tick CPU budget interrupts
// runaway code and disables that binding.
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include <quickjs.h>

namespace wallpaper
{
class SceneNode;

struct ScriptBinding {
    std::string    property;         // origin | angles | scale | visible | alpha
    std::string    source;           // module source
    nlohmann::json scriptproperties; // per-binding overrides, already user-resolved
};

struct ScriptInput {
    // normalised cursor, (0,0) top-left
    std::array<float, 2> cursor { 0.5f, 0.5f };
};

class ScriptEngine {
public:
    struct Layer {
        SceneNode*           node { nullptr }; // null for sound/light layers
        std::string          name;
        int                  id { 0 };
        std::array<float, 2> size { 0.0f, 0.0f };
        int                  parent { -1 }; // layer index of the parent, -1 = none
    };

    ScriptEngine(std::array<int, 2> canvas, nlohmann::json user_properties);
    ~ScriptEngine();
    ScriptEngine(const ScriptEngine&)            = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    // Register every layer before adding bindings: init() may look layers up.
    int  AddLayer(const Layer& layer);
    void SetLayerParent(int layer_index, int parent_index);
    bool AddBinding(int layer_index, const ScriptBinding& binding);

    // Once per frame on the render thread, before uniforms are built.
    void Tick(double frametime, double runtime, const ScriptInput& input);

    // Fire an event hook on every binding that exports it (e.g. "cursorClick").
    void FireEvent(std::string_view name, const ScriptInput& input);

    bool Empty() const { return m_bindings.empty(); }

    // for the JS glue
    Layer* LayerAt(int index) {
        return index >= 0 && (size_t)index < m_layers.size() ? &m_layers[(size_t)index] : nullptr;
    }
    int FindLayer(std::string_view name) const;
    int LayerCount() const { return (int)m_layers.size(); }
    // a new reference to the layer's JS object (caller frees)
    JSValue LayerObject(int index);

private:
    struct Binding;
    struct Impl;

    bool   initRuntime();
    bool   evalModule(Binding& b);
    JSValue currentValue(Binding& b);
    void    applyValue(Binding& b, JSValueConst value);
    void    runBinding(Binding& b, JSValueConst fn, std::string_view what);
    void    callExport(Binding& b, const char* name, JSValue arg, std::string_view what);
    void   disable(Binding& b, std::string_view why);
    void   logException(std::string_view where);
    double timeOfDay() const;
    void   beginBudget(double seconds);

    std::array<int, 2> m_canvas;
    nlohmann::json     m_user_properties;
    std::vector<Layer> m_layers;
    std::vector<std::unique_ptr<Binding>> m_bindings;
    std::unique_ptr<Impl>                 m_impl;
    double                                m_deadline { 0.0 };
    bool                                  m_ok { false };

    // quickjs callbacks
    static int          InterruptCb(JSRuntime*, void* opaque);
    static JSModuleDef* ModuleLoaderCb(JSContext*, const char* name, void* opaque);
};

} // namespace wallpaper
