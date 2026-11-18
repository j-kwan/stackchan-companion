#pragma once
// =============================================================================
// ButtonFsm.h — the project's ONE multi-button state machine
// =============================================================================
// Rule 17's shape, applied to buttons: this bank was born in
// firmware/flight-radar/input.h and the space bin then grew its OWN
// single-button FSM beside it — three independent instances that, with A+C
// held, fired TWO long actions, and that lacked the boot priming (a button
// held while the Fire powers up fired its long action ~700 ms into boot).
// Two implementations of "when does a press speak" is exactly the assumed-twin
// drift A2.23 documents for YAML, so the mechanics now live here, once.
//
// `firmware/common/` is the directory BOTH bins already include (Yaml.h,
// PsJson.h, SunClock.h, FirmwareInfo.h), which is what lets this be shared
// without a dependency from one bin into the other.
//
// PURE by construction: no Arduino, no M5, no globals — time is a parameter.
// That is what makes it natively testable, and it is tested twice over:
// test_input drives it through the radar's vocabulary, test_spaceinput
// through space's. Each bin keeps its own vocabulary (fr::UiEvent,
// spa::UiEvent) in its own input.h; only the MECHANICS are shared, because
// the mechanics are where the bugs were.
// =============================================================================

#include <stdint.h>

namespace sce {

// A Fire/Core classic has three momentary buttons, A B C, left to right.
inline constexpr uint8_t BTN_COUNT = 3;
inline constexpr uint8_t BTN_A = 0, BTN_B = 1, BTN_C = 2;

struct BtnEvent {
    uint8_t btn;        // BTN_A / BTN_B / BTN_C
    bool    isLong;     // held past LONG_MS
};

// Debounced short/long press detector, one instance for all three buttons.
//
// TWO DECISIONS WORTH STATING, both copied from the touch path so that the
// two backends FEEL the same rather than merely doing the same:
//   - the long press fires ON THE HOLD, not on release, so the screen answers
//     while the finger is still down;
//   - having fired, the release that follows must NOT also count as a short
//     press. That is what `_fired` is for, and it is the bug every naive
//     implementation of this ships with.
class ButtonFsm {
  public:
    // 700 ms, the same threshold as the touch hold (HOLD_MS in the bins).
    // Forcing a refresh is a deliberate act; a sleeve should not trigger it.
    static constexpr uint32_t LONG_MS = 700;
    // Mechanical bounce on these switches is a few ms; 25 is comfortable and
    // still far below the fastest deliberate double press.
    static constexpr uint32_t DEBOUNCE_MS = 25;

    // Feed the RAW pressed state of the three buttons once per loop.
    // Returns true and fills `out` when an event fires. At most ONE event per
    // call: with three buttons and a ~12 ms loop, two firing on the same tick
    // is not something a human does, and serialising them keeps the consumer
    // simple.
    //
    // "At most one" only holds up because a blocked event is DEFERRED and not
    // dropped, and that is the subtle part. A transition is believed only when
    // it can also be REPORTED: if `fired` is already set, the release is left
    // unsettled so the very same transition is reconsidered on the next call.
    // Consuming it (clearing `down`, setting `settled`) while refusing to emit
    // is how the first version of this silently lost a press — `pressed[i]`
    // then equalled `b.raw` forever after and neither branch ever ran again
    // (found in review 08-02, and the test that claimed to cover it did not:
    // its two releases were 110 ms apart, so they never collided).
    bool update(uint32_t nowMs, const bool pressed[BTN_COUNT], BtnEvent* out) {
        bool fired = false;
        for (uint8_t i = 0; i < BTN_COUNT; i++) {
            Btn& b = _b[i];
            // --- FIRST sample: adopt the level, announce nothing.
            // A button already held at power-on would otherwise read as a
            // fresh press and fire its LONG action ~700 ms into boot — on a
            // Fire that means the radius cycling or a forced fetch, neither of
            // which anyone asked for. Marking it `fired` swallows the press
            // that was already in progress; the button speaks again after it
            // has been released once.
            if (!b.primed) {
                b.primed  = true;
                b.raw     = pressed[i];
                b.down    = pressed[i];
                b.fired   = pressed[i];
                b.settled = true;
                b.changedMs = b.downMs = nowMs;
                continue;
            }
            // --- debounce: a change must HOLD for DEBOUNCE_MS to be believed
            if (pressed[i] != b.raw) {
                b.raw = pressed[i];
                b.changedMs = nowMs;
                b.settled = false;
            } else if (!b.settled && nowMs - b.changedMs >= DEBOUNCE_MS) {
                if (b.raw && !b.down) {            // believed press
                    b.settled = true;
                    b.down = true;
                    b.downMs = nowMs;
                    b.fired = false;
                } else if (!b.raw && b.down) {     // believed release
                    if (b.fired) {                 // the hold already spoke
                        b.settled = true;
                        b.down = false;
                    } else if (!fired) {           // SHORT press
                        b.settled = true;
                        b.down = false;
                        b.fired = true;
                        if (out) { out->btn = i; out->isLong = false; }
                        fired = true;
                    }
                    // else: another button spoke this call. Leave `settled`
                    // false — `changedMs` is unchanged, so the debounce window
                    // is still satisfied and this lands again next call.
                } else {
                    b.settled = true;              // no edge after all
                }
            }
            // --- long press, fired while still held. Already deferral-safe:
            // when `fired` blocks it, `down` stays set and `fired` stays clear,
            // so the condition holds again on the next call.
            // `b.raw` GUARDS THE RACE (review 08-04): a release sampled just
            // before the threshold is not BELIEVED yet (debounce running), so
            // `down` is still true — without the raw check the hold fired for
            // a press the user had already ended, and the believed release
            // was then swallowed by `fired`: the deliberate short press came
            // out as the long action it was released to avoid. Requiring the
            // RAW level keeps a real hold firing (raw stays true) and defers
            // a bounce by one debounce window at most.
            if (b.down && b.raw && !b.fired && nowMs - b.downMs >= LONG_MS
                && !fired) {
                b.fired = true;
                if (out) { out->btn = i; out->isLong = true; }
                fired = true;
            }
        }
        return fired;
    }

    // CHORD: true while `a` and `b` are BOTH held.
    //
    // Call it BEFORE update() on each tick. It marks both buttons as having
    // spoken, which is what stops the chord from also emitting their
    // individual actions - press A+C for a debug overlay and you must not get
    // "previous item" and "next item" thrown in when you let go, nor A's long
    // press while you hold it.
    //
    // Deliberately a QUERY and not an event: a chord here means "while held",
    // and an event is a thing that happens once. The caller reads it every
    // frame and the overlay lives exactly as long as the fingers do.
    bool chord(uint8_t a, uint8_t b) {
        if (a >= BTN_COUNT || b >= BTN_COUNT) return false;
        if (!_b[a].down || !_b[b].down) return false;
        _b[a].fired = true;
        _b[b].fired = true;
        return true;
    }

    // True while `btn` is held and its long press has not fired yet - the
    // caller uses this to ARM an on-screen prompt, as the touch path does at
    // 250 ms with "hold to refresh...".
    bool arming(uint32_t nowMs, uint8_t btn, uint32_t armMs) const {
        if (btn >= BTN_COUNT) return false;
        const Btn& b = _b[btn];
        return b.down && !b.fired && nowMs - b.downMs >= armMs;
    }

    // NO reset(). There was one, it had no callers, and it was WRONG: it
    // restored `raw = false` while the switch was still closed, so the next
    // sample read as a brand-new press and fired a long action 700 ms later —
    // the exact scenario it claimed to prevent. Should a modal ever need this,
    // clearing `primed` is the correct implementation, because that path
    // already swallows a press in progress.

  private:
    struct Btn {
        bool     raw = false;        // last sampled level
        bool     down = false;       // debounced level
        bool     settled = true;     // raw level has been stable long enough
        bool     fired = false;      // an event already went out for this press
        bool     primed = false;     // the first sample has been adopted
        uint32_t changedMs = 0;
        uint32_t downMs = 0;
    };
    Btn _b[BTN_COUNT];
};

}  // namespace sce
