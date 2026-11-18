#pragma once
// =============================================================================
// Animations.h — StackChan-Companion (engine)
// =============================================================================
// Time-driven 0→1 value generators.
// Ported from esp32-eyes/Animations.h — this file stays under AGPL-3.0:
//   Copyright (c) 2023 Alastair Aitchison, Playful Technology
//   Copyright (c) 2020 Luis Llamas (www.luisllamas.es)
//
// Time is INJECTED (Clock, see CONVENTIONS §5) instead of a hard-coded
// millis() → the transition chains are testable natively with FakeClock.
// The original's virtual IAnimation interface is gone (no polymorphic call
// anywhere in the code — the vtable cost bought nothing).
//
// Classes:
//   RampAnimation           : linear 0→1 over Interval ms, then stays at 1
//   TrapeziumAnimation      : rise → plateau → fall (one-shot)
//   TrapeziumPulseAnimation : repeating trapezium with delays (variations)
//
// CONSUMERS — both inside engine/, and nowhere else includes this header:
//   Transitions.h : a RampAnimation supplies the 0→1 progress that an
//                   EyeConfig → EyeConfig interpolation is read at.
//   EyeRig.h      : TrapeziumPulseAnimations drive the variation stages (TWO
//                   chained per EyeRig — each eye owns its instances, so
//                   left/right phases stay independent when wanted; Normal
//                   and Sleepy deliberately keep both eyes in phase).
// These are pure VALUE generators: they own no geometry, no state of the face
// and no eyelid. The eyelids in particular are NOT here — the original blink
// stage was a trapezium and was removed; closing now travels on the `lid`
// channel, produced by behavior/BlinkController.h (A2.15: the Brain is the
// only source of smoothing for the continuous channels).
// =============================================================================

#include <cstdint>
#include "Clock.h"

namespace sce {

// ------------------------------------------------------------------
// Base: injected clock + start time. Restart() = take a new snapshot.
// ------------------------------------------------------------------
class AnimationBase {
public:
    AnimationBase(const Clock& clock, uint32_t interval)
        : Interval(interval), _clock(clock), _startMs(clock.ms()) {}

    uint32_t Interval;

    void     Restart()          { _startMs = _clock.ms(); }
    uint32_t GetElapsed() const { return _clock.ms() - _startMs; }  // wrap-safe

protected:
    const Clock& _clock;
    uint32_t     _startMs;
};

// -------------------------------------------------------------------------
// RampAnimation: linear 0.0 → 1.0 over Interval ms, then stays at 1.0
// -------------------------------------------------------------------------
class RampAnimation : public AnimationBase {
public:
    RampAnimation(const Clock& clock, uint32_t interval)
        : AnimationBase(clock, interval) {}

    float GetValue() const {
        uint32_t e = GetElapsed();
        if (Interval == 0 || e >= Interval) return 1.0f;
        return (float)e / (float)Interval;
    }
};

// -------------------------------------------------------------------------
// TrapeziumAnimation: rise t0 → plateau t1 → fall t2 (one-shot, 0 after)
// -------------------------------------------------------------------------
class TrapeziumAnimation : public AnimationBase {
public:
    TrapeziumAnimation(const Clock& clock,
                       uint32_t t0, uint32_t t1, uint32_t t2)
        : AnimationBase(clock, t0 + t1 + t2), _t0(t0), _t1(t1), _t2(t2) {}

    float GetValue() const {
        uint32_t e = GetElapsed();
        if (e > Interval) return 0.0f;
        if (_t0 && e < _t0)       return (float)e / (float)_t0;
        if (e < _t0 + _t1)        return 1.0f;
        if (_t2 == 0)             return 0.0f;
        return 1.0f - ((float)e - _t1 - _t0) / (float)_t2;
    }

    uint32_t _t0, _t1, _t2;
};

// -------------------------------------------------------------------------
// TrapeziumPulseAnimation: repeating trapezium — delay t0, rise t1,
// plateau t2, fall t3, delay t4. Used by the eye variations (Normal
// breathing, Glee bounce, Angry tremble...).
// -------------------------------------------------------------------------
class TrapeziumPulseAnimation : public AnimationBase {
public:
    TrapeziumPulseAnimation(const Clock& clock,
                            uint32_t t0, uint32_t t1, uint32_t t2,
                            uint32_t t3, uint32_t t4)
        : AnimationBase(clock, t0 + t1 + t2 + t3 + t4),
          _t0(t0), _t1(t1), _t2(t2), _t3(t3), _t4(t4) {}

    float GetValue() const {
        if (Interval == 0) return 0.0f;
        uint32_t e = GetElapsed() % Interval;
        if (e < _t0)                    return 0.0f;
        if (_t1 && e < _t0 + _t1)       return (float)(e - _t0) / (float)_t1;
        if (e < _t0 + _t1 + _t2)        return 1.0f;
        if (_t3 && e < _t0 + _t1 + _t2 + _t3)
            return 1.0f - ((float)e - _t2 - _t1 - _t0) / (float)_t3;
        return 0.0f;
    }

    // Symmetric triangle: rise t/2, fall t/2, delay between pulses
    void SetTriangle(uint16_t t, uint16_t delayMs) {
        _t0 = 0; _t1 = t / 2; _t2 = 0; _t3 = _t1; _t4 = delayMs;
        Interval = _t0 + _t1 + _t2 + _t3 + _t4;
    }

    void SetInterval(uint16_t t0, uint16_t t1, uint16_t t2,
                     uint16_t t3, uint16_t t4) {
        _t0 = t0; _t1 = t1; _t2 = t2; _t3 = t3; _t4 = t4;
        Interval = _t0 + _t1 + _t2 + _t3 + _t4;
    }

    uint32_t _t0, _t1, _t2, _t3, _t4;
};

} // namespace sce
