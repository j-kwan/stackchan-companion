#pragma once
// =============================================================================
// IdleBehavior.h — StackChan-Companion (behavior)
// =============================================================================
// The idle gaze (ROADMAP §3.3 + art direction §3.0-4):
// FIXATION → SACCADE → FIXATION. Replaces a continuous drift (limp) with the
// pattern of real eyes and of the good robots:
//
//   FIXATION: the gaze HOLDS a target (800-4000 ms). Micro-jitter of ±0.02
//             only on long fixations (> 2 s) — otherwise expressive
//             stillness (Cozmo's "holds").
//   SACCADE : SHARP move to a new target (60-100 ms, aggressive ease-out)
//             followed by a springy micro-overshoot of ~2 px —
//             the robotic part (the snap) + the organic part (the bounce).
//
// Targets are biased towards the centre (signed r²): the robot mostly looks
// "straight ahead", off-centre glances are the exception.
//
// Events: onSaccade(amplitude) consumed by the Brain → blink coupling (§3.3)
// and, in P4, head-follow (§3.0-1: the head follows held off-centre
// fixations).
//
// PURITY: Clock + Rng injected — tested natively (test_behavior).
// =============================================================================

#include <cstdint>
#include <cmath>
#include "../engine/Clock.h"
#include "../engine/Rng.h"
#include "../engine/Units.h"
#include "../engine/Tuning.h"

namespace sce {

class IdleBehavior {
public:
    IdleBehavior(const Clock& clock, Rng& rng, const Tuning& tuning)
        : _clock(clock), _rng(rng), _tuning(tuning) {
        _fixationAt  = _clock.ms();
        _fixationDur = fixDur();
    }

    // ------------------------------------------------------------------
    // Tick (100 Hz). Returns true when a saccade has JUST started
    // (its amplitude is then in lastSaccadeAmplitude()).
    // ------------------------------------------------------------------
    bool update() {
        uint32_t now = _clock.ms();
        bool saccadeStarted = false;

        switch (_state) {
        case FIXATION: {
            if (now - _fixationAt >= _fixationDur) {
                // New centre-biased target: r² keeps the sign → small
                // excursions dominate, large ones are rare
                Vec2f target = {
                    biased(_rng.rangef(-1.0f, 1.0f)) * units::GAZE_MAX_X,
                    biased(_rng.rangef(-1.0f, 1.0f)) * units::GAZE_MAX_Y,
                };
                _saccadeFrom   = _gaze;
                _saccadeTo     = target;
                _saccadeAt     = now;
                // Saccade duration: tuning.saccade_ms ±20 % (hot-tunable)
                uint32_t base  = (uint32_t)_tuning.saccade_ms;
                _saccadeDur    = _rng.range(base * 8 / 10, base * 12 / 10);
                _lastAmplitude = (target - _gaze).length();
                _state         = SACCADE;
                saccadeStarted = true;
            } else if (now - _fixationAt > 2000) {
                // Micro-jitter on a long fixation: the discreet "aliveness"
                if (now - _jitterAt > _rng.range(150, 350)) {
                    _jitterAt = now;
                    _jitter = { _rng.rangef(-0.02f, 0.02f),
                                _rng.rangef(-0.015f, 0.015f) };
                }
            }
            break;
        }
        case SACCADE: {
            float t = (float)(now - _saccadeAt) / (float)_saccadeDur;
            if (t >= 1.0f) {
                _gaze     = _saccadeTo;
                _state    = OVERSHOOT;
                _saccadeAt = now;
            } else {
                // Aggressive ease-out (1-(1-t)³): the robotic snap
                float s = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                _gaze = _saccadeFrom + (_saccadeTo - _saccadeFrom) * s;
            }
            _jitter = {};
            break;
        }
        case OVERSHOOT: {
            // Springy micro-bounce ~120 ms: overshoots by ~2 px then returns
            float t = (float)(now - _saccadeAt) / 120.0f;
            if (t >= 1.0f) {
                _gaze        = _saccadeTo;
                _state       = FIXATION;
                _fixationAt  = now;
                _fixationDur = fixDur();
                _jitterAt    = now;
            } else {
                Vec2f dir = _saccadeTo - _saccadeFrom;
                float len = dir.length();
                if (len > 1e-4f) {
                    // 2 px amplitude in gaze units, damped sine
                    float ov = (2.0f / units::GAZE_PX_X)
                             * sinf(t * 3.14159f) * (1.0f - t);
                    _gaze = _saccadeTo + dir * (ov / len);
                }
            }
            break;
        }
        }
        return saccadeStarted;
    }

    // Current gaze (target + jitter) — the GazeArbiter's baseline
    Vec2f gaze() const { return _gaze + _jitter; }

    bool  isSaccading()           const { return _state == SACCADE; }
    float lastSaccadeAmplitude()  const { return _lastAmplitude; }

    // Held off-centre fixation (P4 head-follow: the head follows after ~1 s)
    bool fixationHeld(uint32_t minMs, float minEccentricity) const {
        return _state == FIXATION &&
               _clock.ms() - _fixationAt >= minMs &&
               _gaze.length() >= minEccentricity;
    }

private:
    enum State { FIXATION, SACCADE, OVERSHOOT };

    const Clock&  _clock;
    Rng&          _rng;
    const Tuning& _tuning;

    // Fixation duration drawn from the Tuning register bounds (hot-tunable)
    uint32_t fixDur() {
        uint32_t lo = (uint32_t)_tuning.fixation_min_ms;
        uint32_t hi = (uint32_t)_tuning.fixation_max_ms;
        return _rng.range(lo, hi > lo ? hi : lo);
    }

    State    _state       = FIXATION;
    Vec2f    _gaze{}, _jitter{};
    Vec2f    _saccadeFrom{}, _saccadeTo{};
    uint32_t _fixationAt = 0, _fixationDur = 1500;
    uint32_t _saccadeAt  = 0, _saccadeDur  = 80;
    uint32_t _jitterAt   = 0;
    float    _lastAmplitude = 0.0f;

    // Centre bias: keeps the sign, squashes towards 0 (x → x·|x|)
    static float biased(float x) { return x * fabsf(x); }
};

} // namespace sce
