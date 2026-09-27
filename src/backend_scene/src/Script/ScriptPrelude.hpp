#pragma once
// JavaScript side of the SceneScript host: the global environment every script
// sees (Vec2/Vec3, engine, input, thisScene, console, createScriptProperties)
// and the built-in modules WEMath / WEVector / WEColor.
//
// Host hooks (defined from C++ before this runs):
//   __weLog(level, text)         console output
//   __weGetLayer(name)           layer object or null
//   __weLayerProto               prototype receiving the native layer accessors
//
// Functions the host calls:
//   __weTick(dt, t, cx, cy, wx, wy, tod)   refresh engine/input for the frame
//   __weRun(layer, fn, value)               call fn with thisLayer bound

namespace wallpaper::scriptjs
{

inline constexpr const char* kPrelude = R"JS(
'use strict';

function __num(v) { return typeof v === 'number' && Number.isFinite(v) ? v : 0; }

class Vec2 {
    constructor(x, y) {
        if (x !== null && typeof x === 'object') { this.x = __num(x.x); this.y = __num(x.y); }
        else if (y === undefined) { this.x = __num(x); this.y = __num(x); }
        else { this.x = __num(x); this.y = __num(y); }
    }
    static _c(o) { return typeof o === 'number' ? new Vec2(o, o) : o; }
    copy() { return new Vec2(this.x, this.y); }
    add(o) { o = Vec2._c(o); return new Vec2(this.x + o.x, this.y + o.y); }
    subtract(o) { o = Vec2._c(o); return new Vec2(this.x - o.x, this.y - o.y); }
    multiply(o) { o = Vec2._c(o); return new Vec2(this.x * o.x, this.y * o.y); }
    divide(o) { o = Vec2._c(o); return new Vec2(o.x ? this.x / o.x : 0, o.y ? this.y / o.y : 0); }
    negate() { return new Vec2(-this.x, -this.y); }
    dot(o) { return this.x * o.x + this.y * o.y; }
    length() { return Math.hypot(this.x, this.y); }
    lengthSquared() { return this.x * this.x + this.y * this.y; }
    distance(o) { return Math.hypot(this.x - o.x, this.y - o.y); }
    normalize() { const l = this.length(); return l > 0 ? this.divide(l) : new Vec2(0, 0); }
    lerp(o, t) { return new Vec2(this.x + (o.x - this.x) * t, this.y + (o.y - this.y) * t); }
    equals(o) { return !!o && this.x === o.x && this.y === o.y; }
    toString() { return this.x + ' ' + this.y; }
}

class Vec3 {
    constructor(x, y, z) {
        if (x !== null && typeof x === 'object') {
            this.x = __num(x.x); this.y = __num(x.y); this.z = __num(x.z);
        } else if (y === undefined) { this.x = __num(x); this.y = __num(x); this.z = __num(x); }
        else { this.x = __num(x); this.y = __num(y); this.z = __num(z); }
    }
    static _c(o) { return typeof o === 'number' ? new Vec3(o, o, o) : o; }
    copy() { return new Vec3(this.x, this.y, this.z); }
    add(o) { o = Vec3._c(o); return new Vec3(this.x + o.x, this.y + o.y, this.z + __num(o.z)); }
    subtract(o) { o = Vec3._c(o); return new Vec3(this.x - o.x, this.y - o.y, this.z - __num(o.z)); }
    multiply(o) { o = Vec3._c(o); return new Vec3(this.x * o.x, this.y * o.y, this.z * __num(o.z)); }
    divide(o) {
        o = Vec3._c(o);
        return new Vec3(o.x ? this.x / o.x : 0, o.y ? this.y / o.y : 0, o.z ? this.z / o.z : 0);
    }
    negate() { return new Vec3(-this.x, -this.y, -this.z); }
    dot(o) { return this.x * o.x + this.y * o.y + this.z * __num(o.z); }
    cross(o) {
        return new Vec3(this.y * o.z - this.z * o.y, this.z * o.x - this.x * o.z,
                        this.x * o.y - this.y * o.x);
    }
    length() { return Math.hypot(this.x, this.y, this.z); }
    lengthSquared() { return this.x * this.x + this.y * this.y + this.z * this.z; }
    distance(o) { return Math.hypot(this.x - o.x, this.y - o.y, this.z - __num(o.z)); }
    normalize() { const l = this.length(); return l > 0 ? this.divide(l) : new Vec3(0, 0, 0); }
    lerp(o, t) {
        return new Vec3(this.x + (o.x - this.x) * t, this.y + (o.y - this.y) * t,
                        this.z + (o.z - this.z) * t);
    }
    equals(o) { return !!o && this.x === o.x && this.y === o.y && this.z === o.z; }
    toString() { return this.x + ' ' + this.y + ' ' + this.z; }
}

class Vec4 {
    constructor(x, y, z, w) {
        if (x !== null && typeof x === 'object') {
            this.x = __num(x.x); this.y = __num(x.y); this.z = __num(x.z); this.w = __num(x.w);
        } else if (y === undefined) { this.x = this.y = this.z = this.w = __num(x); }
        else { this.x = __num(x); this.y = __num(y); this.z = __num(z); this.w = __num(w); }
    }
    copy() { return new Vec4(this.x, this.y, this.z, this.w); }
    toString() { return this.x + ' ' + this.y + ' ' + this.z + ' ' + this.w; }
}

globalThis.Vec2 = Vec2;
globalThis.Vec3 = Vec3;
globalThis.Vec4 = Vec4;

// "r g b" strings (colour properties) -> Vec3
function __parseVec(s, n) {
    if (typeof s !== 'string') return s;
    const p = s.trim().split(/\s+/).map(Number);
    if (p.some(v => !Number.isFinite(v))) return s;
    if (n === 2 || p.length === 2) return new Vec2(p[0], p[1]);
    if (n === 4 || p.length === 4) return new Vec4(p[0], p[1], p[2], p[3]);
    return new Vec3(p[0], p[1], p[2]);
}

// ---- script properties -------------------------------------------------
// Values come from the binding's "scriptproperties" (set in __weProps by the
// host right before the module is evaluated); the builder supplies defaults.
globalThis.__weProps = {};
globalThis.createScriptProperties = function () {
    const values = {};
    const kinds = {};
    const b = {};
    const adder = (kind) => function (def) {
        if (def && typeof def.name === 'string') {
            values[def.name] = def.value;
            kinds[def.name] = kind;
        }
        return b;
    };
    b.addSlider = adder('slider');
    b.addCheckbox = adder('checkbox');
    b.addBool = adder('checkbox');
    b.addText = adder('text');
    b.addTextInput = adder('text');
    b.addColor = adder('color');
    b.addCombo = adder('combo');
    b.addFile = adder('file');
    b.addDirectory = adder('directory');
    b.addSound = adder('file');
    b.addLayer = adder('layer');
    b.finish = function () {
        const props = globalThis.__weProps || {};
        for (const k of Object.keys(props)) values[k] = props[k];
        for (const k of Object.keys(values)) {
            const kind = kinds[k];
            let v = values[k];
            if (kind === 'color') v = __parseVec(v, 3);
            else if (kind === 'slider' && typeof v === 'string') {
                const n = Number(v);
                if (Number.isFinite(n)) v = n;
            } else if (kind === 'checkbox' && typeof v === 'string') v = v === 'true';
            values[k] = v;
        }
        return values;
    };
    return b;
};

// ---- engine / input ----------------------------------------------------
globalThis.engine = {
    frametime: 0,
    runtime: 0,
    timeOfDay: 0,
    canvasSize: new Vec2(0, 0),
    screenSize: new Vec2(0, 0),
    userProperties: {},
    isMobile: false,
    isWallpaperVisible() { return true; },
    registerAudioBuffers() {},
    unregisterAudioBuffers() {},
    setDynamicPerformanceMode() {},
};

globalThis.input = {
    cursorPosition: new Vec2(0, 0),
    cursorWorldPosition: new Vec3(0, 0, 0),
    cursorDelta: new Vec2(0, 0),
    cursorVisible: true,
    cursorDown: false,
};

globalThis.shared = {};

globalThis.console = {
    log(...a) { __weLog(0, a.map(String).join(' ')); },
    info(...a) { __weLog(0, a.map(String).join(' ')); },
    debug(...a) { __weLog(0, a.map(String).join(' ')); },
    warn(...a) { __weLog(1, a.map(String).join(' ')); },
    error(...a) { __weLog(2, a.map(String).join(' ')); },
};

// ---- layers ------------------------------------------------------------
// Animations (puppet / timeline) are not driven by the renderer yet: hand out
// inert handles so scripts that fetch them keep running.
function __animStub(name) {
    return {
        name, rate: 1, paused: true, progress: 0,
        play() {}, stop() {}, pause() {}, reset() {}, setTime() {},
        isPlaying() { return false; }, isPaused() { return true; },
    };
}

Object.assign(globalThis.__weLayerProto, {
    getAnimation(name) { return __animStub(String(name)); },
    getName() { return this.name; },
    isVisible() { return this.visible; },
    // sound layers
    play() {}, stop() {}, pause() {},
    // parent-relative helpers some scripts expect
    getParent() { return null; },
    toString() { return 'Layer(' + this.name + ')'; },
});

globalThis.thisScene = {
    getLayer(name) { return __weGetLayer(String(name)); },
    getLayerByName(name) { return __weGetLayer(String(name)); },
    getUserProperty(name) { return globalThis.engine.userProperties[name]; },
    enumerateLayers() { return []; },
    enumerateLayersByTag() { return []; },
    getLayerDepth() { return 0; },
};
globalThis.thisLayer = null;

// ---- host entry points -------------------------------------------------
globalThis.__weTick = function (dt, t, cx, cy, wx, wy, tod) {
    const e = globalThis.engine, i = globalThis.input;
    e.frametime = dt;
    e.runtime = t;
    e.timeOfDay = tod;
    i.cursorDelta = new Vec2(cx - i.cursorPosition.x, cy - i.cursorPosition.y);
    i.cursorPosition = new Vec2(cx, cy);
    i.cursorWorldPosition = new Vec3(wx, wy, 0);
};

globalThis.__weRun = function (layer, fn, value) {
    globalThis.thisLayer = layer;
    const r = fn(value);
    return r === undefined ? value : r;
};

globalThis.__weEvent = function (layer, fn, ev) {
    globalThis.thisLayer = layer;
    fn(ev);
};
)JS";

inline constexpr const char* kWEMath = R"JS(
export function lerp(a, b, t) { return a + (b - a) * t; }
export function mix(a, b, t) { return a + (b - a) * t; }
export function clamp(v, lo, hi) { return Math.min(Math.max(v, lo), hi); }
export function saturate(v) { return Math.min(Math.max(v, 0), 1); }
export function smoothstep(e0, e1, x) {
    const t = clamp((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}
export function smootherstep(e0, e1, x) {
    const t = clamp((x - e0) / (e1 - e0), 0, 1);
    return t * t * t * (t * (t * 6 - 15) + 10);
}
export function frac(x) { return x - Math.floor(x); }
export function sign(x) { return x < 0 ? -1 : x > 0 ? 1 : 0; }
export function random(min = 0, max = 1) { return min + Math.random() * (max - min); }
export function randomInt(min, max) { return Math.floor(min + Math.random() * (max - min + 1)); }
export function degreesToRadians(d) { return d * Math.PI / 180; }
export function radiansToDegrees(r) { return r * 180 / Math.PI; }
export function repeat(t, len) { return t - Math.floor(t / len) * len; }
export function pingPong(t, len) { t = repeat(t, len * 2); return len - Math.abs(t - len); }
export function remap(v, a, b, c, d) { return c + (v - a) * (d - c) / (b - a); }
export function approach(a, b, step) { return a < b ? Math.min(a + step, b) : Math.max(a - step, b); }
export function distance(a, b) { return a.distance ? a.distance(b) : Math.abs(a - b); }
export function lerpVec(a, b, t) { return a.lerp(b, t); }
export function lerpAngle(a, b, t) {
    let d = repeat(b - a, Math.PI * 2);
    if (d > Math.PI) d -= Math.PI * 2;
    return a + d * t;
}
export function nearlyEqual(a, b, eps = 1e-6) { return Math.abs(a - b) <= eps; }
)JS";

inline constexpr const char* kWEVector = R"JS(
const V2 = globalThis.Vec2, V3 = globalThis.Vec3, V4 = globalThis.Vec4;
export { V2 as Vec2, V3 as Vec3, V4 as Vec4 };
export function vec2(x, y) { return new V2(x, y); }
export function vec3(x, y, z) { return new V3(x, y, z); }
export function vec4(x, y, z, w) { return new V4(x, y, z, w); }
export function dot(a, b) { return a.dot(b); }
export function cross(a, b) { return a.cross(b); }
export function length(a) { return a.length(); }
export function normalize(a) { return a.normalize(); }
export function distance(a, b) { return a.distance(b); }
export function lerp(a, b, t) { return a.lerp(b, t); }
export function rotate2D(v, angle) {
    const c = Math.cos(angle), s = Math.sin(angle);
    return new V2(v.x * c - v.y * s, v.x * s + v.y * c);
}
)JS";

inline constexpr const char* kWEColor = R"JS(
const V3 = globalThis.Vec3;
export function rgbToHsv(c) {
    const r = c.x, g = c.y, b = c.z;
    const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
    let h = 0;
    if (d > 0) {
        if (max === r) h = ((g - b) / d) % 6;
        else if (max === g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6; if (h < 0) h += 1;
    }
    return new V3(h, max > 0 ? d / max : 0, max);
}
export function hsvToRgb(c) {
    const h = c.x, s = c.y, v = c.z;
    const i = Math.floor(h * 6), f = h * 6 - i;
    const p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (((i % 6) + 6) % 6) {
        case 0: return new V3(v, t, p);
        case 1: return new V3(q, v, p);
        case 2: return new V3(p, v, t);
        case 3: return new V3(p, q, v);
        case 4: return new V3(t, p, v);
        default: return new V3(v, p, q);
    }
}
export function rgbToHsl(c) {
    const r = c.x, g = c.y, b = c.z;
    const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2;
    if (max === min) return new V3(0, 0, l);
    const d = max - min, s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    let h;
    if (max === r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    return new V3(h / 6, s, l);
}
export function hslToRgb(c) {
    const h = c.x, s = c.y, l = c.z;
    if (s === 0) return new V3(l, l, l);
    const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    const f = (t) => {
        t = ((t % 1) + 1) % 1;
        if (t < 1 / 6) return p + (q - p) * 6 * t;
        if (t < 1 / 2) return q;
        if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
        return p;
    };
    return new V3(f(h + 1 / 3), f(h), f(h - 1 / 3));
}
export function hexToRgb(hex) {
    const m = String(hex).replace('#', '');
    const n = parseInt(m.length === 3 ? m.split('').map(c => c + c).join('') : m, 16);
    return new V3(((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255);
}
export function luminance(c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }
)JS";

} // namespace wallpaper::scriptjs
