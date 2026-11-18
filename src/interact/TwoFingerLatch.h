#pragma once
// =============================================================================
// TwoFingerLatch.h — StackChan-Companion (interact) — PURE, tested natively
// =============================================================================
// The state machine behind the two-finger hold, with no M5Unified in it: fed a
// POINT COUNT and a clock, it answers with events. TouchGestures keeps the
// panel, the coordinates and the callbacks — this keeps the timing, which is
// the part that was wrong and the part a test can reach.
//
// WHY IT IS ITS OWN FILE. Measured on the CoreS3 panel during ONE deliberate
// 8.9 s two-finger hold: the ">= 2 points" condition started FIVE separate
// times and the glass read completely EMPTY in between. The gesture therefore
// latched twice and fired twice — and since it TOGGLES a setting, the second
// answer undid the first and the robot looked inert. The report was "it does
// not work"; the truth was "it works an even number of times", which is the
// harder bug to see because every individual part of it behaves.
//
// THE CURE IS SYMMETRY. Arming was already debounced — a second point has to
// persist before it is believed, because the panel's release tail briefly
// reports one finger as two. Releasing was not: a single pass reading zero
// ended the gesture. So the very dropout that arming was built to survive
// ended the contact instead, and the next pass started a second gesture inside
// the first. Here BOTH edges are debounced and the fire is latched, so one
// gesture answers exactly once however ragged the panel's view of it.
//
// The counterpart of that tolerance: `engaged()` stays true across a dropout,
// so the one-finger path stays suppressed for the whole gesture rather than
// waking up between two reports and dispatching a tap underneath it.
// =============================================================================

#include <stdint.h>

namespace sce {

class TwoFingerLatch {
public:
    // A second point must persist this long to be believed. The panel's
    // release tail is two updates; a real second finger is about to stay for
    // seconds, so nothing honest is lost.
    uint32_t armMs   = 150;
    // ...and the glass must read EMPTY this long before the gesture is over.
    // The dropouts measured inside a single hold are one or two passes; this
    // is far longer than any of them and far shorter than the pause between
    // two deliberate gestures.
    uint32_t clearMs = 400;
    // How long the two fingers must stay before the gesture answers. Long
    // enough that no ordinary handling of the robot reaches it by accident,
    // short enough to be worth waiting out once you know it exists.
    uint32_t holdMs  = 3000;

    enum class Event : uint8_t {
        None,      // nothing to do this pass
        Latched,   // the gesture just took the screen — sample positions now
        Fire,      // the hold completed. ONCE per gesture, never more.
    };

    // Feed it the number of touch points and the clock, once per pass.
    Event update(uint8_t points, uint32_t nowMs) {
        if (points == 0) { if (!_empty) { _empty = true; _emptyAt = nowMs; } }
        else             { _empty = false; }

        // THE TOLERANCE IS GRANTED TO AN ESTABLISHED GESTURE, NOT TO A
        // CANDIDATE, and that asymmetry is the whole design. Being generous
        // to a candidate is how the panel's release tail — which reports one
        // lifting finger as two for a couple of passes — used to latch the
        // two-finger branch and swallow every band gesture that followed.
        if (!_latched) {
            if (points >= 2) { if (!_two) { _two = true; _twoAt = nowMs; } }
            else             { _two = false; }
            if (_two && nowMs - _twoAt >= armMs) {
                _latched = true;
                return Event::Latched;
            }
            return Event::None;
        }

        // Latched: a single point, or none for a moment, is the panel losing
        // sight of fingers that never left. Only a glass that STAYS empty
        // ends the gesture — and ending it is the only thing that re-arms the
        // fire, so one hold answers once however ragged the panel's view.
        if (_empty && nowMs - _emptyAt >= clearMs) { reset(); return Event::None; }
        if (!_fired && _eligible && nowMs - _twoAt >= holdMs) {
            _fired = true;
            return Event::Fire;
        }
        return Event::None;
    }

    // Whether the two points are where the gesture requires them. Decided by
    // the CALLER, which owns the coordinates. It gates FIRING and not
    // latching, deliberately: a two-finger contact takes the screen over
    // wherever it lands, or the one-finger path would keep dispatching taps
    // underneath a gesture that is merely aimed badly.
    void setEligible(bool v) { _eligible = v; }

    // True while the gesture owns the screen. Survives the dropouts — that is
    // the whole point.
    bool engaged() const { return _latched; }
    // True once this gesture has answered. Exposed for the caller's own
    // bookkeeping and for the tests to read without provoking a second event.
    bool fired() const { return _fired; }

    void reset() {
        _two = _empty = _latched = _fired = _eligible = false;
        _twoAt = _emptyAt = 0;
    }

private:
    bool     _two      = false;   // a second point has been seen
    bool     _empty    = false;   // the glass currently reads zero
    bool     _latched  = false;   // the gesture owns the screen
    bool     _fired    = false;   // ...and has already answered
    bool     _eligible = false;   // the caller approved the positions
    uint32_t _twoAt    = 0;       // when the second point landed
    uint32_t _emptyAt  = 0;       // when the glass went empty
};

} // namespace sce
