#include "ScriptEngine.hpp"
#include "ScriptPrelude.hpp"

#include "Scene/SceneNode.h"
#include "Utils/Logging.h"

#include <quickjs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <numbers>

using namespace wallpaper;

namespace
{
constexpr size_t kMemoryLimit    = 64u << 20; // per scene
constexpr size_t kStackLimit     = 1u << 20;
constexpr double kTickBudgetSec  = 0.004; // all scripts together, per frame
constexpr double kSetupBudgetSec = 0.5;   // module evaluation + init()

// scene.json stores radians; SceneScript exposes degrees
constexpr double kRad2Deg = 180.0 / std::numbers::pi;
constexpr double kDeg2Rad = std::numbers::pi / 180.0;

double NowSec() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

enum class PropKind { Vec3, Bool, Number };

bool KindOf(std::string_view property, PropKind& kind) {
    if (property == "origin" || property == "angles" || property == "scale") {
        kind = PropKind::Vec3;
        return true;
    }
    if (property == "visible") {
        kind = PropKind::Bool;
        return true;
    }
    if (property == "alpha") {
        kind = PropKind::Number;
        return true;
    }
    return false;
}

// layer accessor ids (magic)
enum LayerProp : int { LP_ORIGIN, LP_ANGLES, LP_SCALE, LP_ALPHA, LP_VISIBLE, LP_SIZE, LP_NAME, LP_ID };

struct Vec3d {
    double x { 0 }, y { 0 }, z { 0 };
};

// quickjs-ng class ids are runtime-independent once allocated
JSClassID g_layer_class_id = 0;

ScriptEngine* EngineOf(JSContext* ctx) {
    return static_cast<ScriptEngine*>(JS_GetContextOpaque(ctx));
}

bool ReadVec3(JSContext* ctx, JSValueConst v, Vec3d& out) {
    if (! JS_IsObject(v)) return false;
    const char* names[3] = { "x", "y", "z" };
    double*     dst[3]   = { &out.x, &out.y, &out.z };
    for (int i = 0; i < 3; i++) {
        JSValue p  = JS_GetPropertyStr(ctx, v, names[i]);
        double  d  = 0;
        int     rc = JS_ToFloat64(ctx, &d, p);
        JS_FreeValue(ctx, p);
        if (rc < 0) return false;
        *dst[i] = std::isfinite(d) ? d : 0.0;
    }
    return true;
}

JSValue NewVecN(JSContext* ctx, const char* ctor_name, const double* v, int n) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor   = JS_GetPropertyStr(ctx, global, ctor_name);
    JSValue argv[4];
    for (int i = 0; i < n; i++) argv[i] = JS_NewFloat64(ctx, v[i]);
    JSValue r = JS_CallConstructor(ctx, ctor, n, argv);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, global);
    return r;
}
JSValue NewVec3(JSContext* ctx, double x, double y, double z) {
    const double v[3] = { x, y, z };
    return NewVecN(ctx, "Vec3", v, 3);
}
JSValue NewVec2(JSContext* ctx, double x, double y) {
    const double v[2] = { x, y };
    return NewVecN(ctx, "Vec2", v, 2);
}

// ---- native functions exposed to the prelude -----------------------------

JSValue js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    int32_t level = 0;
    JS_ToInt32(ctx, &level, argv[0]);
    const char* s = JS_ToCString(ctx, argv[1]);
    if (s != nullptr) {
        if (level >= 2)
            LOG_ERROR("script: %s", s);
        else
            LOG_INFO("script: %s", s);
        JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

JSValue js_get_layer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* eng = EngineOf(ctx);
    if (eng == nullptr || argc < 1) return JS_NULL;
    const char* s = JS_ToCString(ctx, argv[0]);
    if (s == nullptr) return JS_NULL;
    int idx = eng->FindLayer(s);
    JS_FreeCString(ctx, s);
    if (idx < 0) return JS_NULL;
    return eng->LayerObject(idx);
}

ScriptEngine::Layer* LayerOf(JSContext* ctx, JSValueConst this_val) {
    auto* eng = EngineOf(ctx);
    if (eng == nullptr) return nullptr;
    auto raw = (intptr_t)JS_GetOpaque(this_val, g_layer_class_id);
    if (raw <= 0) return nullptr; // opaque stores index + 1
    return eng->LayerAt((int)raw - 1);
}

JSValue layer_get(JSContext* ctx, JSValueConst this_val, int magic) {
    auto* layer = LayerOf(ctx, this_val);
    if (layer == nullptr) return JS_UNDEFINED;
    auto* node = layer->node;
    switch (magic) {
    case LP_ORIGIN: {
        if (! node) return NewVec3(ctx, 0, 0, 0);
        const auto& t = node->Translate();
        return NewVec3(ctx, t.x(), t.y(), t.z());
    }
    case LP_ANGLES: {
        if (! node) return NewVec3(ctx, 0, 0, 0);
        const auto& r = node->Rotation();
        return NewVec3(ctx, r.x() * kRad2Deg, r.y() * kRad2Deg, r.z() * kRad2Deg);
    }
    case LP_SCALE: {
        if (! node) return NewVec3(ctx, 1, 1, 1);
        const auto& s = node->Scale();
        return NewVec3(ctx, s.x(), s.y(), s.z());
    }
    case LP_ALPHA:
        return JS_NewFloat64(ctx, node && node->Alpha() >= 0.0f ? node->Alpha() : 1.0);
    case LP_VISIBLE: return JS_NewBool(ctx, node ? node->VisibleSelf() : true);
    case LP_SIZE: return NewVec2(ctx, layer->size[0], layer->size[1]);
    case LP_NAME: return JS_NewString(ctx, layer->name.c_str());
    case LP_ID: return JS_NewInt32(ctx, layer->id);
    }
    return JS_UNDEFINED;
}

JSValue layer_set(JSContext* ctx, JSValueConst this_val, JSValueConst val, int magic) {
    auto* layer = LayerOf(ctx, this_val);
    if (layer == nullptr || layer->node == nullptr) return JS_UNDEFINED;
    auto* node = layer->node;
    Vec3d v;
    switch (magic) {
    case LP_ORIGIN:
        if (ReadVec3(ctx, val, v)) node->SetTranslate({ (float)v.x, (float)v.y, (float)v.z });
        break;
    case LP_ANGLES:
        if (ReadVec3(ctx, val, v))
            node->SetRotation(
                { (float)(v.x * kDeg2Rad), (float)(v.y * kDeg2Rad), (float)(v.z * kDeg2Rad) });
        break;
    case LP_SCALE:
        if (ReadVec3(ctx, val, v)) node->SetScale({ (float)v.x, (float)v.y, (float)v.z });
        break;
    case LP_ALPHA: {
        double d = 1.0;
        if (JS_ToFloat64(ctx, &d, val) == 0 && std::isfinite(d))
            node->SetAlpha((float)std::clamp(d, 0.0, 1.0));
        break;
    }
    case LP_VISIBLE: {
        int b = JS_ToBool(ctx, val);
        if (b >= 0) node->SetVisible(b != 0);
        break;
    }
    default: break;
    }
    return JS_UNDEFINED;
}

const JSCFunctionListEntry kLayerProtoFuncs[] = {
    JS_CGETSET_MAGIC_DEF("origin", layer_get, layer_set, LP_ORIGIN),
    JS_CGETSET_MAGIC_DEF("angles", layer_get, layer_set, LP_ANGLES),
    JS_CGETSET_MAGIC_DEF("scale", layer_get, layer_set, LP_SCALE),
    JS_CGETSET_MAGIC_DEF("alpha", layer_get, layer_set, LP_ALPHA),
    JS_CGETSET_MAGIC_DEF("visible", layer_get, layer_set, LP_VISIBLE),
    JS_CGETSET_MAGIC_DEF("size", layer_get, nullptr, LP_SIZE),
    JS_CGETSET_MAGIC_DEF("name", layer_get, nullptr, LP_NAME),
    JS_CGETSET_MAGIC_DEF("id", layer_get, nullptr, LP_ID),
};

JSClassDef kLayerClass = {
    .class_name = "WELayer",
    .finalizer  = nullptr,
};

// the built-in modules
JSModuleDef* LoadBuiltinModule(JSContext* ctx, const char* name) {
    const char*      src = nullptr;
    std::string_view n(name);
    if (n == "WEMath")
        src = scriptjs::kWEMath;
    else if (n == "WEVector")
        src = scriptjs::kWEVector;
    else if (n == "WEColor")
        src = scriptjs::kWEColor;
    if (src == nullptr) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", name);
        return nullptr;
    }
    JSValue v =
        JS_Eval(ctx, src, strlen(src), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(v)) return nullptr;
    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(v));
    JS_FreeValue(ctx, v);
    return m;
}
} // namespace

// ---------------------------------------------------------------------------

struct ScriptEngine::Binding {
    int            layer { -1 };
    std::string    property;
    PropKind       kind { PropKind::Vec3 };
    std::string    source;
    nlohmann::json props;
    JSValue        ns { JS_UNDEFINED };
    JSValue        update { JS_UNDEFINED };
    bool           ok { false };
};

struct ScriptEngine::Impl {
    JSRuntime* rt { nullptr };
    JSContext* ctx { nullptr };

    JSValue run_fn { JS_UNDEFINED };
    JSValue event_fn { JS_UNDEFINED };
    JSValue tick_fn { JS_UNDEFINED };
    // one JS object per registered layer, created lazily
    std::vector<JSValue> layer_objs;

    // run all pending promise jobs (module evaluation is async in quickjs-ng)
    void drainJobs() {
        JSContext* c = nullptr;
        while (JS_IsJobPending(rt)) {
            if (JS_ExecutePendingJob(rt, &c) <= 0) break;
        }
    }
};

int ScriptEngine::InterruptCb(JSRuntime*, void* opaque) {
    auto* self = static_cast<ScriptEngine*>(opaque);
    return NowSec() > self->m_deadline ? 1 : 0;
}

JSModuleDef* ScriptEngine::ModuleLoaderCb(JSContext* ctx, const char* name, void*) {
    return LoadBuiltinModule(ctx, name);
}

ScriptEngine::ScriptEngine(std::array<int, 2> canvas, nlohmann::json user_properties)
    : m_canvas(canvas), m_user_properties(std::move(user_properties)), m_impl(new Impl()) {
    m_ok = initRuntime();
}

ScriptEngine::~ScriptEngine() {
    auto& I = *m_impl;
    if (I.ctx != nullptr) {
        for (auto& b : m_bindings) {
            JS_FreeValue(I.ctx, b->update);
            JS_FreeValue(I.ctx, b->ns);
        }
        for (auto& v : I.layer_objs) JS_FreeValue(I.ctx, v);
        JS_FreeValue(I.ctx, I.run_fn);
        JS_FreeValue(I.ctx, I.event_fn);
        JS_FreeValue(I.ctx, I.tick_fn);
        JS_FreeContext(I.ctx);
    }
    if (I.rt != nullptr) JS_FreeRuntime(I.rt);
}

bool ScriptEngine::initRuntime() {
    auto& I = *m_impl;
    I.rt    = JS_NewRuntime();
    if (I.rt == nullptr) return false;
    JS_SetRuntimeOpaque(I.rt, this);
    JS_SetMemoryLimit(I.rt, kMemoryLimit);
    JS_SetMaxStackSize(I.rt, kStackLimit);
    JS_SetInterruptHandler(I.rt, &ScriptEngine::InterruptCb, this);
    JS_SetModuleLoaderFunc(I.rt, nullptr, &ScriptEngine::ModuleLoaderCb, this);

    I.ctx = JS_NewContext(I.rt);
    if (I.ctx == nullptr) return false;
    JS_SetContextOpaque(I.ctx, this);

    // layer class + prototype (the prelude adds the JS-side methods to it)
    JS_NewClassID(I.rt, &g_layer_class_id);
    if (JS_NewClass(I.rt, g_layer_class_id, &kLayerClass) < 0) return false;
    JSValue proto = JS_NewObject(I.ctx);
    JS_SetPropertyFunctionList(
        I.ctx, proto, kLayerProtoFuncs, sizeof(kLayerProtoFuncs) / sizeof(kLayerProtoFuncs[0]));
    JS_SetClassProto(I.ctx, g_layer_class_id, JS_DupValue(I.ctx, proto));

    JSValue global = JS_GetGlobalObject(I.ctx);
    JS_SetPropertyStr(I.ctx, global, "__weLayerProto", proto); // consumed
    JS_SetPropertyStr(I.ctx, global, "__weLog", JS_NewCFunction(I.ctx, js_log, "__weLog", 2));
    JS_SetPropertyStr(
        I.ctx, global, "__weGetLayer", JS_NewCFunction(I.ctx, js_get_layer, "__weGetLayer", 1));

    beginBudget(kSetupBudgetSec);
    JSValue r = JS_Eval(
        I.ctx, scriptjs::kPrelude, strlen(scriptjs::kPrelude), "<prelude>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) {
        logException("prelude");
        JS_FreeValue(I.ctx, global);
        return false;
    }
    JS_FreeValue(I.ctx, r);

    I.run_fn   = JS_GetPropertyStr(I.ctx, global, "__weRun");
    I.event_fn = JS_GetPropertyStr(I.ctx, global, "__weEvent");
    I.tick_fn  = JS_GetPropertyStr(I.ctx, global, "__weTick");

    // engine.canvasSize / engine.userProperties
    {
        JSValue engine = JS_GetPropertyStr(I.ctx, global, "engine");
        JS_SetPropertyStr(I.ctx, engine, "canvasSize", NewVec2(I.ctx, m_canvas[0], m_canvas[1]));
        JS_SetPropertyStr(I.ctx, engine, "screenSize", NewVec2(I.ctx, m_canvas[0], m_canvas[1]));
        const std::string props = m_user_properties.is_object() ? m_user_properties.dump() : "{}";
        JSValue up = JS_ParseJSON(I.ctx, props.c_str(), props.size(), "<userProperties>");
        if (JS_IsException(up)) {
            logException("userProperties");
            up = JS_NewObject(I.ctx);
        }
        JS_SetPropertyStr(I.ctx, engine, "userProperties", up);
        JS_FreeValue(I.ctx, engine);
    }
    JS_FreeValue(I.ctx, global);
    return true;
}

void ScriptEngine::beginBudget(double seconds) { m_deadline = NowSec() + seconds; }

double ScriptEngine::timeOfDay() const {
    std::time_t now = std::time(nullptr);
    std::tm     lt {};
    localtime_r(&now, &lt);
    return ((lt.tm_hour * 60 + lt.tm_min) * 60 + lt.tm_sec) / 86400.0;
}

void ScriptEngine::logException(std::string_view where) {
    auto&       I    = *m_impl;
    JSValue     e    = JS_GetException(I.ctx);
    const char* msg  = JS_ToCString(I.ctx, e);
    std::string text = msg ? msg : "(unknown)";
    if (msg) JS_FreeCString(I.ctx, msg);
    if (JS_IsObject(e)) {
        JSValue stack = JS_GetPropertyStr(I.ctx, e, "stack");
        if (! JS_IsUndefined(stack)) {
            const char* s = JS_ToCString(I.ctx, stack);
            if (s && *s) {
                text += "\n";
                text += s;
            }
            if (s) JS_FreeCString(I.ctx, s);
        }
        JS_FreeValue(I.ctx, stack);
    }
    JS_FreeValue(I.ctx, e);
    LOG_ERROR("script (%.*s): %s", (int)where.size(), where.data(), text.c_str());
}

int ScriptEngine::AddLayer(const Layer& layer) {
    m_layers.push_back(layer);
    m_impl->layer_objs.push_back(JS_UNDEFINED);
    return (int)m_layers.size() - 1;
}

int ScriptEngine::FindLayer(std::string_view name) const {
    for (size_t i = 0; i < m_layers.size(); i++)
        if (m_layers[i].name == name) return (int)i;
    return -1;
}

JSValue ScriptEngine::LayerObject(int index) {
    auto& I = *m_impl;
    if (index < 0 || (size_t)index >= I.layer_objs.size()) return JS_NULL;
    auto& slot = I.layer_objs[(size_t)index];
    if (JS_IsUndefined(slot)) {
        slot = JS_NewObjectClass(I.ctx, (int)g_layer_class_id);
        // opaque holds index + 1 so that 0 stays "unset"
        if (! JS_IsException(slot)) JS_SetOpaque(slot, (void*)(intptr_t)(index + 1));
    }
    return JS_DupValue(I.ctx, slot);
}

void ScriptEngine::disable(Binding& b, std::string_view why) {
    auto* layer = LayerAt(b.layer);
    LOG_ERROR("script on layer '%s' property '%s' disabled: %.*s",
              layer ? layer->name.c_str() : "?",
              b.property.c_str(),
              (int)why.size(),
              why.data());
    b.ok = false;
}

bool ScriptEngine::AddBinding(int layer_index, const ScriptBinding& binding) {
    if (! m_ok) return false;
    auto* layer = LayerAt(layer_index);
    if (layer == nullptr) return false;
    PropKind kind;
    if (! KindOf(binding.property, kind)) {
        LOG_INFO("script: property '%s' on layer '%s' is not scriptable here, skipped",
                 binding.property.c_str(),
                 layer->name.c_str());
        return false;
    }
    auto b      = std::make_unique<Binding>();
    b->layer    = layer_index;
    b->property = binding.property;
    b->kind     = kind;
    b->source   = binding.source;
    b->props    = binding.scriptproperties;
    if (layer->node != nullptr) layer->node->SetDynamic(true);

    bool ok = evalModule(*b);
    m_bindings.push_back(std::move(b));
    return ok;
}

// Evaluate a binding's module, then run init(value) if it exports one.
bool ScriptEngine::evalModule(Binding& b) {
    auto&   I      = *m_impl;
    auto*   layer  = LayerAt(b.layer);
    JSValue global = JS_GetGlobalObject(I.ctx);
    beginBudget(kSetupBudgetSec);

    // per-script property overrides, read by createScriptProperties().finish()
    {
        const std::string props = b.props.is_object() ? b.props.dump() : "{}";
        JSValue           pv    = JS_ParseJSON(I.ctx, props.c_str(), props.size(), "<props>");
        if (JS_IsException(pv)) {
            logException("scriptproperties");
            pv = JS_NewObject(I.ctx);
        }
        JS_SetPropertyStr(I.ctx, global, "__weProps", pv);
        JS_SetPropertyStr(I.ctx, global, "thisLayer", LayerObject(b.layer));
    }
    JS_FreeValue(I.ctx, global);

    const std::string modname = "layer:" + layer->name + ":" + b.property;
    JSValue           mod     = JS_Eval(I.ctx,
                          b.source.c_str(),
                          b.source.size(),
                          modname.c_str(),
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(mod)) {
        logException(modname);
        disable(b, "compile error");
        return false;
    }
    auto*   m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(mod));
    JSValue r = JS_EvalFunction(I.ctx, mod); // consumes mod
    if (JS_IsException(r)) {
        logException(modname);
        disable(b, "evaluation error");
        return false;
    }
    I.drainJobs();
    bool rejected = false;
    if (JS_PromiseState(I.ctx, r) == JS_PROMISE_REJECTED) {
        JS_Throw(I.ctx, JS_PromiseResult(I.ctx, r));
        logException(modname);
        rejected = true;
    }
    JS_FreeValue(I.ctx, r);
    if (rejected) {
        disable(b, "evaluation error");
        return false;
    }

    b.ns     = JS_GetModuleNamespace(I.ctx, m);
    b.update = JS_GetPropertyStr(I.ctx, b.ns, "update");
    if (! JS_IsFunction(I.ctx, b.update)) {
        JS_FreeValue(I.ctx, b.update);
        b.update = JS_UNDEFINED;
    }
    b.ok = true;

    JSValue init = JS_GetPropertyStr(I.ctx, b.ns, "init");
    if (JS_IsFunction(I.ctx, init)) runBinding(b, init, "init");
    JS_FreeValue(I.ctx, init);
    return b.ok;
}

// Current property value as a JS value
JSValue ScriptEngine::currentValue(Binding& b) {
    auto&   I    = *m_impl;
    auto*   node = LayerAt(b.layer)->node;
    JSValue v    = JS_UNDEFINED;
    switch (b.kind) {
    case PropKind::Vec3: {
        Eigen::Vector3f e = Eigen::Vector3f::Zero();
        if (node != nullptr) {
            if (b.property == "origin")
                e = node->Translate();
            else if (b.property == "angles")
                e = node->Rotation() * (float)kRad2Deg;
            else
                e = node->Scale();
        }
        v = NewVec3(I.ctx, e.x(), e.y(), e.z());
        break;
    }
    case PropKind::Bool: v = JS_NewBool(I.ctx, node ? node->VisibleSelf() : true); break;
    case PropKind::Number:
        v = JS_NewFloat64(I.ctx, node && node->Alpha() >= 0.0f ? node->Alpha() : 1.0);
        break;
    }
    return v;
}

void ScriptEngine::applyValue(Binding& b, JSValueConst v) {
    auto& I    = *m_impl;
    auto* node = LayerAt(b.layer)->node;
    if (node == nullptr) return;
    switch (b.kind) {
    case PropKind::Vec3: {
        Vec3d d;
        if (! ReadVec3(I.ctx, v, d)) break;
        Eigen::Vector3f e { (float)d.x, (float)d.y, (float)d.z };
        if (b.property == "origin")
            node->SetTranslate(e);
        else if (b.property == "angles")
            node->SetRotation(e * (float)kDeg2Rad);
        else
            node->SetScale(e);
        break;
    }
    case PropKind::Bool: {
        int bb = JS_ToBool(I.ctx, v);
        if (bb >= 0) node->SetVisible(bb != 0);
        break;
    }
    case PropKind::Number: {
        double d = 1.0;
        if (JS_ToFloat64(I.ctx, &d, v) == 0 && std::isfinite(d))
            node->SetAlpha((float)std::clamp(d, 0.0, 1.0));
        break;
    }
    }
}

// value = fn(value) with thisLayer bound; an exception disables the binding
void ScriptEngine::runBinding(Binding& b, JSValueConst fn, std::string_view what) {
    auto&   I       = *m_impl;
    JSValue layer   = LayerObject(b.layer);
    JSValue value   = currentValue(b);
    JSValue argv[3] = { layer, fn, value };
    JSValue r       = JS_Call(I.ctx, I.run_fn, JS_UNDEFINED, 3, argv);
    if (JS_IsException(r)) {
        logException(std::string(what) + " on " + LayerAt(b.layer)->name);
        disable(b, "runtime error");
    } else {
        applyValue(b, r);
    }
    JS_FreeValue(I.ctx, r);
    JS_FreeValue(I.ctx, value);
    JS_FreeValue(I.ctx, layer);
}

void ScriptEngine::Tick(double frametime, double runtime, const ScriptInput& input) {
    if (! m_ok || m_bindings.empty()) return;
    auto& I = *m_impl;
    JS_UpdateStackTop(I.rt); // we may be on a different thread than the parser
    beginBudget(kTickBudgetSec);

    // engine.* / input.* for this frame. World space is y-up with the origin at
    // the canvas' bottom-left, like the layer origins in scene.json.
    const double cx = input.cursor[0] * m_canvas[0];
    const double cy = input.cursor[1] * m_canvas[1];
    {
        JSValue argv[7] = {
            JS_NewFloat64(I.ctx, frametime), JS_NewFloat64(I.ctx, runtime),
            JS_NewFloat64(I.ctx, cx),        JS_NewFloat64(I.ctx, cy),
            JS_NewFloat64(I.ctx, cx),        JS_NewFloat64(I.ctx, m_canvas[1] - cy),
            JS_NewFloat64(I.ctx, timeOfDay()),
        };
        JSValue r = JS_Call(I.ctx, I.tick_fn, JS_UNDEFINED, 7, argv);
        if (JS_IsException(r)) logException("tick");
        JS_FreeValue(I.ctx, r);
    }

    for (auto& b : m_bindings) {
        if (! b->ok || JS_IsUndefined(b->update)) continue;
        runBinding(*b, b->update, "update");
    }
    I.drainJobs();
}

void ScriptEngine::FireEvent(std::string_view name, const ScriptInput& input) {
    if (! m_ok || m_bindings.empty()) return;
    auto& I = *m_impl;
    JS_UpdateStackTop(I.rt);
    beginBudget(kTickBudgetSec);
    const std::string fname(name);
    const double      cx = input.cursor[0] * m_canvas[0];
    const double      cy = input.cursor[1] * m_canvas[1];

    for (auto& b : m_bindings) {
        if (! b->ok) continue;
        JSValue fn = JS_GetPropertyStr(I.ctx, b->ns, fname.c_str());
        if (JS_IsFunction(I.ctx, fn)) {
            JSValue ev = JS_NewObject(I.ctx);
            JS_SetPropertyStr(I.ctx, ev, "position", NewVec2(I.ctx, cx, cy));
            JS_SetPropertyStr(I.ctx, ev, "worldPosition", NewVec3(I.ctx, cx, m_canvas[1] - cy, 0));
            JS_SetPropertyStr(I.ctx, ev, "button", JS_NewInt32(I.ctx, 0));
            JSValue layer   = LayerObject(b->layer);
            JSValue argv[3] = { layer, fn, ev };
            JSValue r       = JS_Call(I.ctx, I.event_fn, JS_UNDEFINED, 3, argv);
            if (JS_IsException(r)) {
                logException(fname + " on " + LayerAt(b->layer)->name);
                disable(*b, "runtime error");
            }
            JS_FreeValue(I.ctx, r);
            JS_FreeValue(I.ctx, ev);
            JS_FreeValue(I.ctx, layer);
        }
        JS_FreeValue(I.ctx, fn);
    }
    I.drainJobs();
}
