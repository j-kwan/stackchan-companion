#pragma once
// =============================================================================
// Sequencer.h — StackChan-Companion (behavior)
// =============================================================================
// Unified dance timeline (ROADMAP §3.1/§3.7): servo + expression + eyelids +
// gaze bias in ONE single keyframe sequence — replaces separate animation and
// servo queues along with their respective falling-edge guards (a whole class
// of desynchronisation bugs between queues).
//
// PRINCIPLES (§3.7):
//   - `holdMs ≥ servoMs` GUARANTEED at playback (on-the-fly clamp in
//     update() + warning counter, so the data can stay const) — the
//     classic failure (servo 1000 ms / keyframe 100 ms = limp wobbling)
//     becomes impossible by construction.
//   - The Sequencer is PURE: it touches neither servo nor screen. update()
//     returns the keyframe to APPLY when the timeline advances; the Brain
//     does the wiring (ServoMotion, applyEmotion, BlinkController).
//   - Gaze during a dance is NOT in the keyframes: the Brain derives it
//     continuously from the COMMANDED servo pose (gazeFromHead) plus the
//     keyframe's optional bias (NOD/SHY: the servo does not go below the
//     horizon, the eyes dive in its place).
//   - REFLEX CONTRACT: abort() cuts the timeline IMMEDIATELY — called by the
//     Brain on shake/pickup. The visual handover is softened by the Brain's
//     blenders (§3.7).
//
// PURITY: Clock injected — tested natively (test_sequencer).
// =============================================================================

#include <cstdint>
#include "../engine/Clock.h"
#include "../engine/Emotions.h"
#include "../engine/Transitions.h"

namespace sce {

// ------------------------------------------------------------------
// One dance keyframe. Angles are OFFSETS from YAW_CENTER/PITCH_NEUTRAL
// (recalibrating means touching Units.h and nothing else).
// ------------------------------------------------------------------
struct DanceKey {
    float    yawOff   = 0.0f;   // ° from YAW_CENTER (+ = viewer's right)
    float    pitchOff = 0.0f;   // ° from PITCH_NEUTRAL (- = head raised)
    uint16_t servoMs  = 300;    // duration of the servo travel
    uint16_t holdMs   = 400;    // total keyframe duration (clamped ≥ servoMs)
    eEmotions emotion = EMOTIONS_COUNT;  // EMOTIONS_COUNT = unchanged
    uint8_t  lidEvent = 0;      // 0=none 1=blink 2=winkG 3=winkD (left/right)
    float    gazeYBias = 0.0f;  // extra vertical bias [-1..1] (NOD/SHY)
};

class Sequencer {
public:
    explicit Sequencer(const Clock& clock) : _clock(clock) {}

    // ------------------------------------------------------------------
    // Loads and starts a sequence (preempts the running one — the Brain has
    // already decided). The holdMs ≥ servoMs clamp is applied ON THE FLY in
    // update() (so the data can stay const/PROGMEM-able).
    // ------------------------------------------------------------------
    void play(const DanceKey* keys, int count) {
        _keys    = keys;
        _count   = count > 0 ? count : 0;
        _index   = -1;              // update() will apply keyframe 0
        _keyAt   = _clock.ms();
        _active  = _count > 0;
    }

    // Immediate cut (reflex / API stop). The Brain handles the handover.
    void abort() { _active = false; _keys = nullptr; }

    bool isActive() const { return _active; }

    // Gaze bias of the current keyframe (0 when no dance is running)
    float gazeYBias() const {
        return (_active && _index >= 0) ? _keys[_index].gazeYBias : 0.0f;
    }

    // ------------------------------------------------------------------
    // Tick: returns the keyframe to APPLY right now (exactly once per
    // keyframe), nullptr otherwise. Reaching the end flips isActive() to
    // false AFTER the last keyframe's hold has elapsed.
    // ------------------------------------------------------------------
    const DanceKey* update() {
        if (!_active) return nullptr;
        uint32_t now = _clock.ms();

        if (_index < 0) {                       // start-up: keyframe 0
            _index   = 0;
            _keyAt   = now;
            _curHold = effectiveHold(_keys[0]); // clamp counted 1× per keyframe
            return current();
        }

        if (now - _keyAt >= _curHold) {
            _index++;
            if (_index >= _count) {             // sequence finished
                _active = false;
                _keys   = nullptr;
                return nullptr;
            }
            _keyAt   = now;
            _curHold = effectiveHold(_keys[_index]);
            return current();
        }
        return nullptr;
    }

    // Number of holdMs<servoMs clamps hit (data-quality telemetry)
    uint32_t clampCount() const { return _clamps; }

private:
    const Clock&    _clock;
    const DanceKey* _keys   = nullptr;
    int             _count  = 0;
    int             _index   = -1;
    uint32_t        _keyAt   = 0;
    uint32_t        _curHold = 0;
    uint32_t        _clamps  = 0;
    bool            _active  = false;

    const DanceKey* current() { return &_keys[_index]; }

    // §3.7 invariant: a keyframe lasts AT LEAST as long as its own movement
    uint32_t effectiveHold(const DanceKey& k) {
        if (k.holdMs < k.servoMs) { _clamps++; return k.servoMs; }
        return k.holdMs;
    }
};

} // namespace sce
