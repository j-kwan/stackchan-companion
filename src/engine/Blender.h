#pragma once
// =============================================================================
// Blender.h — StackChan-Companion (engine)
// =============================================================================
// ChannelBlender (ROADMAP §3.7): NO channel ever jumps. When a channel changes
// owner or goes through a discontinuity (end of Sleepy, end of a dance,
// sequence preemption), the value is CROSSFADED from the current state to the
// new target — per-channel durations (gaze 80 ms, lids 120 ms).
//
// Entry points:
//   snapTo(v)            : set the value with no blend (init, continuous cases)
//   blendTo(durMs)       : arm an ease-in-out crossfade from the CURRENT value
//                          (the target is whatever track() feeds next)
//   track(v)             : follows a continuous target (pass-through) UNLESS a
//                          blend is running — the blend absorbs the handover
//
// track() is the main usage mode: the producer (BlinkController for the lids,
// the Brain's gaze path for the idle↔dance handovers around the Sequencer)
// feeds its output every tick; at a known discontinuity it calls
// blendTo(dur) once, and the following ticks keep calling track()
// — the blender interpolates between the frozen old value and the live target.
//
// PURITY: Clock injected — tested natively (test_blender).
// =============================================================================

#include <cstdint>
#include "Clock.h"
#include "Units.h"

namespace sce {

class FloatBlender {
public:
    explicit FloatBlender(const Clock& clock, float initial = 0.0f)
        : _clock(clock), _value(initial) {}

    void snapTo(float v) { _value = v; _blending = false; }

    // Crossfade: freezes the current value as the starting point; the live
    // target is supplied by the following track() calls
    void blendTo(uint32_t durMs) {
        _from     = _value;
        _startMs  = _clock.ms();
        _durMs    = durMs ? durMs : 1;
        _blending = true;
    }

    // Call every tick with the live output of the current producer
    float track(float target) {
        if (_blending) {
            uint32_t e = _clock.ms() - _startMs;
            if (e >= _durMs) {
                _blending = false;
                _value = target;
            } else {
                float t = (float)e / (float)_durMs;
                t = t * t * (3.0f - 2.0f * t);   // ease-in-out
                _value = _from + (target - _from) * t;
            }
        } else {
            _value = target;
        }
        return _value;
    }

    float value()      const { return _value; }
    bool  isBlending() const { return _blending; }

private:
    const Clock& _clock;
    float    _value;
    float    _from     = 0.0f;
    uint32_t _startMs  = 0;
    uint32_t _durMs    = 1;
    bool     _blending = false;
};

// Vec2f version (gaze) — same contract
class Vec2Blender {
public:
    explicit Vec2Blender(const Clock& clock) : _x(clock), _y(clock) {}

    void  snapTo(Vec2f v)          { _x.snapTo(v.x); _y.snapTo(v.y); }
    void  blendTo(uint32_t durMs)  { _x.blendTo(durMs); _y.blendTo(durMs); }
    Vec2f track(Vec2f target)      { return { _x.track(target.x), _y.track(target.y) }; }
    bool  isBlending() const       { return _x.isBlending() || _y.isBlending(); }

private:
    FloatBlender _x, _y;
};

// Per-channel crossfade durations (§3.7)
namespace blend {
inline constexpr uint32_t GAZE_MS = 80;
inline constexpr uint32_t LID_MS  = 120;
}

} // namespace sce
