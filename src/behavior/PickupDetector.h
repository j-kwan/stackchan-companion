#pragma once
// =============================================================================
// PickupDetector.h — StackChan-Companion (behavior)
// =============================================================================
// "I am being picked up / put down" detection (ROADMAP §2.2 PickupReaction,
// user request 2026-07-06).
//
// Chosen signature: |accel| departs from 1 g for a sustained time (the lift
// impulse then the carrying) WITHOUT a dominant gyro shake (otherwise it is a
// shake → Scared, not a pickup). Put down: |accel| ≈ 1 g steady for 1 s.
//
//   GROUNDED --(dev > pickup_dev_g held pickup_hold_ms, gyro < shake)--> LIFTED
//   LIFTED   --(dev < 0.05 g steady for 1000 ms)-----------------------> GROUNDED
//
// The Brain consumes the transitions: LIFTED → Curious (+ in P4: head raised
// by ~10° then servo torque released — "the feet dangle in mid-air");
// GROUNDED → blink + settling + servo re-engagement (P4).
//
// Lift thresholds PROMOTED into Tuning (2026-07-10, user feedback "not
// sensitive enough"): pickup_dev_g (default 0.08 g) + pickup_hold_ms
// (120 ms), hot-tunable through POST /api/tuning, persisted to SD.
//
// PURITY: Clock + Tuning injected — tested natively (test_vor/test_pickup).
// =============================================================================

#include <cstdint>
#include <cmath>
#include "../engine/Clock.h"
#include "../engine/Tuning.h"

namespace sce {

class PickupDetector {
public:
    PickupDetector(const Clock& clock, const Tuning& tuning)
        : _clock(clock), _tuning(tuning) {}

    // Transition events — true ONLY on the tick where the change happens
    struct Events {
        bool lifted  = false;   // has just been picked up
        bool putDown = false;   // has just been put down
    };

    // ------------------------------------------------------------------
    // Tick (100 Hz). accelMagG: |accel| in g; gyroMagDegS: |gyro| in °/s
    // (guard against confusing this with a shake).
    // ------------------------------------------------------------------
    Events update(float accelMagG, float gyroMagDegS) {
        uint32_t now = _clock.ms();
        float dev = fabsf(accelMagG - 1.0f);
        Events ev;

        switch (_state) {
        case GROUNDED:
            // Sustained departure from 1 g, with no dominant shake
            if (dev > _tuning.pickup_dev_g && gyroMagDegS < LIFT_GYRO_MAX_DEGS) {
                if (!_liftSinceMs) _liftSinceMs = now;
                if (now - _liftSinceMs >= (uint32_t)_tuning.pickup_hold_ms) {
                    _state      = LIFTED;
                    _calmSinceMs = 0;
                    ev.lifted   = true;
                }
            } else {
                _liftSinceMs = 0;
            }
            break;

        case LIFTED:
            // A prolonged return to gravitational calm = put down
            if (dev < DOWN_DEV_G) {
                if (!_calmSinceMs) _calmSinceMs = now;
                if (now - _calmSinceMs >= DOWN_HOLD_MS) {
                    _state       = GROUNDED;
                    _liftSinceMs = 0;
                    ev.putDown   = true;
                }
            } else {
                _calmSinceMs = 0;
            }
            break;
        }
        return ev;
    }

    bool isLifted() const { return _state == LIFTED; }

private:
    enum State { GROUNDED, LIFTED };

    // Fixed thresholds (lift: see Tuning pickup_dev_g/pickup_hold_ms)
    static constexpr float    LIFT_GYRO_MAX_DEGS = 60.0f;  // above = shake
    static constexpr float    DOWN_DEV_G         = 0.05f;  // calm
    static constexpr uint32_t DOWN_HOLD_MS       = 1000;   // steady for 1 s

    const Clock&  _clock;
    const Tuning& _tuning;
    State        _state       = GROUNDED;
    uint32_t     _liftSinceMs = 0;
    uint32_t     _calmSinceMs = 0;
};

} // namespace sce
