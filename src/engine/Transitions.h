#pragma once
// =============================================================================
// Transitions.h — StackChan-Companion (engine)
// =============================================================================
// Transition between two eye geometries (EyeConfig → EyeConfig).
// Extended port of esp32-eyes/EyeTransition.h — this file stays under
// AGPL-3.0:
//   Copyright (c) 2020 Luis Llamas (www.luisllamas.es)
//
// Methods: LINEAR (original), EASE_IN_OUT (3t²-2t³), SPRING (damped spring
// 1-e^(-d·k·t)·cos(k·t), clamped to 1).
//
// Notable points of the port:
//   - Clock injected (natively testable).
//   - SAFE-STATE CONTRACT (the "bands at boot" fix, §2.1): at construction,
//     Destin = _startSnapshot = *Origin. A transition that was never
//     configured therefore interpolates towards the CURRENT state (no-op) —
//     never again towards the undefined geometry of an uninitialized Destin.
//   - Restart() implicitly rules out phantom Destins: SetDestin() is the only
//     way in and takes the snapshot itself.
// =============================================================================

#include <cstdint>
#include <cmath>
#include "Animations.h"
#include "EyeConfig.h"

namespace sce {

enum TransitionMethod {
    LINEAR      = 0,
    EASE_IN_OUT = 1,
    SPRING      = 2,
};

struct TransitionConfig {
    TransitionMethod method   = EASE_IN_OUT;
    uint32_t         duration = 220;
    float            springK  = 12.0f;
    float            springD  = 0.65f;

    TransitionConfig() = default;
    TransitionConfig(TransitionMethod m, uint32_t d,
                     float k = 12.0f, float damp = 0.65f)
        : method(m), duration(d), springK(k), springD(damp) {}
};

namespace DefaultTransitions {
    // Hardware-validated values
    static const TransitionConfig CALM  (EASE_IN_OUT, 300);
    static const TransitionConfig NORMAL(EASE_IN_OUT, 220);
    static const TransitionConfig STRONG(SPRING, 180, 14.0f, 0.60f);
    static const TransitionConfig SOFT  (EASE_IN_OUT, 400);
}

class EyeTransition {
public:
    // `origin`: the live geometry (rewritten every frame by apply()).
    // Safe state: Destin = snapshot = current state → no-op transition until
    // SetDestin() has been called.
    EyeTransition(const Clock& clock, EyeConfig* origin)
        : _origin(origin), _animation(clock, 500) {
        if (_origin) {
            _startSnapshot = *_origin;
            Destin         = *_origin;
        }
    }

    EyeConfig        Destin;   // target — set through SetDestin() only

    // ------------------------------------------------------------------
    // Configures and starts a transition towards `destin`.
    // Snapshot of the current state taken HERE (no cumulative drift).
    // ------------------------------------------------------------------
    void SetDestin(const EyeConfig& destin, const TransitionConfig& cfg) {
        if (!_origin) return;
        _config             = cfg;
        _animation.Interval = cfg.duration;
        _startSnapshot      = *_origin;
        Destin              = destin;
        _animation.Restart();
    }

    // Raw 0→1 progress (to sync external effects: color)
    float Progress() const { return _animation.GetValue(); }

    // Call every frame: interpolates _startSnapshot → Destin into *_origin
    void Update() {
        if (!_origin) return;
        apply(applyMethod(_animation.GetValue()));
    }

private:
    EyeConfig*       _origin;
    EyeConfig        _startSnapshot;
    RampAnimation    _animation;
    TransitionConfig _config;

    template <typename T>
    static T lerpVal(T start, T end, float t) {
        return static_cast<T>(start + (end - start) * t);
    }

    void apply(float t) {
        EyeConfig& o = *_origin;
        const EyeConfig& s = _startSnapshot;
        o.OffsetX               = lerpVal(s.OffsetX,               Destin.OffsetX,               t);
        o.OffsetY               = lerpVal(s.OffsetY,               Destin.OffsetY,               t);
        o.Height                = lerpVal(s.Height,                Destin.Height,                t);
        o.Width                 = lerpVal(s.Width,                 Destin.Width,                 t);
        o.Slope_Top             = lerpVal(s.Slope_Top,             Destin.Slope_Top,             t);
        o.Slope_Bottom          = lerpVal(s.Slope_Bottom,          Destin.Slope_Bottom,          t);
        o.Radius_Top            = lerpVal(s.Radius_Top,            Destin.Radius_Top,            t);
        o.Radius_Bottom         = lerpVal(s.Radius_Bottom,         Destin.Radius_Bottom,         t);
        // Outer radii (2026-07-16) — ALWAYS resolved by EyeRig::mirrored
        // (0 → base radius) before reaching here: the lerp starts from the
        // real value, never from a misleading "0 = inherit".
        o.Radius_Top_Outer      = lerpVal(s.Radius_Top_Outer,      Destin.Radius_Top_Outer,      t);
        o.Radius_Bottom_Outer   = lerpVal(s.Radius_Bottom_Outer,   Destin.Radius_Bottom_Outer,   t);
        o.OuterIsLeft           = Destin.OuterIsLeft;   // constant per eye
        o.Inverse_Radius_Top    = lerpVal(s.Inverse_Radius_Top,    Destin.Inverse_Radius_Top,    t);
        o.Inverse_Radius_Bottom = lerpVal(s.Inverse_Radius_Bottom, Destin.Inverse_Radius_Bottom, t);
        o.Inverse_Offset_Top    = lerpVal(s.Inverse_Offset_Top,    Destin.Inverse_Offset_Top,    t);
        o.Inverse_Offset_Bottom = lerpVal(s.Inverse_Offset_Bottom, Destin.Inverse_Offset_Bottom, t);
    }

    float applyMethod(float t) const {
        switch (_config.method) {
        case LINEAR:
            return t;
        case EASE_IN_OUT:
            return t * t * (3.0f - 2.0f * t);
        case SPRING: {
            float s = t * 3.0f;
            float r = 1.0f - expf(-_config.springD * _config.springK * s)
                            * cosf(_config.springK * s);
            // clamp: the overshoot created a visible micro-jump
            return clampVal(r, 0.0f, 1.0f);
        }
        default:
            return t;
        }
    }

    static float clampVal(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
};

} // namespace sce
