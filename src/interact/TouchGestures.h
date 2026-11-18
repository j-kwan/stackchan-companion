#pragma once
// =============================================================================
// TouchGestures.h — StackChan-Companion (interact) — validated on hardware
// =============================================================================
// Touch interaction manager for the CoreS3 (M5Unified).
// Turns raw touchscreen events into high-level gestures consumed by main.cpp
// through CALLBACKS (a single API — the old dual channel of an enum return
// value plus callbacks had no consumer for the return value, and neither did
// onTap/onLongPress/isTouching/contactDuration: pruned in the 07-26 review).
//
// Gestures: 4-direction swipes (flick > flickThreshold, dominant axis);
// taps by horizontal third (left/center/right) RESTRICTED to the eye area
// (y < tapZoneMaxY) — the status bar does not wink, its swipes are routed on
// the main.cpp side (startY() vs EYEZONE_H).
//
// Usage:
//   TouchGestures touch;
//   touch.onSwipeLeft = []() { /* ... */ };
//   // in loop(), after M5.update():
//   touch.update();
// =============================================================================

#include <M5Unified.h>
#include <functional>
#include "../engine/Units.h"
#include "TwoFingerLatch.h"

namespace sce {

class TouchGestures {
public:
    // Threshold in pixels that separates a flick from a tap
    int flickThreshold   = 30;
    // Zone TAPS (onTapLeft/Center/Right = wink/blink) only fire ABOVE
    // tapZoneMaxY (the eye area) — never in the status bar. ONE single
    // boundary, shared with the swipe routing on the main.cpp side (startY()
    // vs EYEZONE_H) — no diverging literal.
    // (The old exclusionZoneY is GONE: the whole screen is active; the
    // SDUpdater launcher is blocking and handles its own touch — no conflict.)
    int tapZoneMaxY      = units::EYEZONE_H;

    // ------------------------------------------------------------------
    // Callbacks — assign them before calling update()
    // ------------------------------------------------------------------
    std::function<void()> onSwipeLeft;
    std::function<void()> onSwipeRight;
    std::function<void()> onSwipeUp;
    std::function<void()> onSwipeDown;
    std::function<void()> onTapLeft;
    std::function<void()> onTapCenter;
    std::function<void()> onTapRight;
    // Tap BELOW tapZoneMaxY (the status band). Separate from the three eye
    // zones on purpose: a band tap is a band ACTION (timer start/pause/ack,
    // 08-04), never a wink — the two vocabularies must not blur. startX()
    // tells the handler where.
    std::function<void()> onTapBand;
    // Deliberate HOLD on the band (timer reset, 08-04): a still contact of
    // 0.7-3 s released in place. The window's upper bound and the
    // no-movement test are what keep the resting-thumb/carried-robot
    // swallow intact — those hold far longer or drift.
    std::function<void()> onHoldBand;
    // Fires once when a band-started contact ends on the LONG path
    // (>= 700 ms), whatever it did — the consumer's per-drag state has
    // to be cleared on every ending, not only on the ones that also
    // produce a tap or a swipe.
    std::function<void()> onBandDragEnd;
    // CONTINUOUS drag for a touch that STARTED in the band (the timer's
    // scroll, user 08-04: hold and slide up = increase, down = decrease).
    // Fires on EVERY update() while the finger is down — (startX, dyTotal),
    // dyTotal NEGATIVE upward; the press frame fires with dyTotal == 0,
    // which is the consumer's reset signal. The finger may leave the band
    // mid-drag (a long slide crosses into the eye zone): what matters is
    // where the gesture STARTED, like every other band routing here.
    // Release still dispatches tap/swipe as usual — a consumer that applied
    // drag steps swallows those itself (it alone knows it consumed them).
    std::function<void(int, int)> onBandDrag;

    // Two fingers held THREE SECONDS on the face. Fires ONCE per contact, on
    // the hold itself and not on the release, so the screen answers while the
    // fingers are still down — the same choice the button bank makes for its
    // long press, and for the same reason: a control you cannot see needs to
    // confirm itself before you give up on it.
    std::function<void()> onTwoFingerHoldEyes;

    // ------------------------------------------------------------------
    // update() — call this after M5.update() in loop()
    // ------------------------------------------------------------------
    void update() {
        if (!M5.Touch.isEnabled()) return;

        // ---- HOW MANY FINGERS ARE REALLY ON THE GLASS -------------------
        // TWO traps here, and the second one was walked into while fixing the
        // first (08-25). Both come from the same fact: M5Unified's DETAIL
        // array is indexed by the panel's HARDWARE touch id (`tp.id` — the
        // high nibble of P1_YH on the FT6336), which ROTATES between
        // contacts, while `getCount()` returns how many of those slots are
        // merely non-`none`.
        //
        // TRAP 1 — the count over-reports. A slot outlives the lift by two
        // updates (one to become `touch_end`, one to clear), so a finger
        // landing shortly after another lifted makes getCount() answer TWO
        // with one finger down. That is what a slow slide does every time the
        // panel drops and re-acquires contact, which is precisely what the
        // timer's scroll is made of: the two-finger branch latched and every
        // band gesture stopped answering at once.
        //
        // TRAP 2 — the slots are NOT PACKED, so `for (i = 0; i < getCount();
        // i++) getDetail(i)` is wrong, and wrong in the worst way. A lone
        // contact the panel numbered 1 gives count 1 while slot 0 is empty:
        // the loop inspects slot 0, finds nothing, and the CONTACT DOES NOT
        // EXIST as far as this class is concerned. Ids rotate, so about every
        // other tap vanished. (getDetail() cannot even reach slot 1 in that
        // state — it clamps any index >= count back to 0.)
        //
        // So the detail array is not used for this at all. `getCount()` says
        // how many slots are active, and the TIMING of what that count means —
        // when to believe a second finger, when to believe it has gone — lives
        // in TwoFingerLatch, which is pure and natively tested. Positions come
        // from `getTouchPointRaw`, whose array IS packed by the panel read:
        // index 0 is the first point reported this pass, whatever id it
        // carries.
        const uint8_t nSlot = M5.Touch.getCount();
        const uint32_t nowMs = millis();

        // ---- TWO FINGERS HELD IN THE EYE ZONE (08-24) --------------------
        // A deliberately awkward gesture for a deliberately rare act: three
        // seconds, two fingers, on the face. Nothing else on this robot asks
        // for two fingers, so it cannot be arrived at by accident — which is
        // the requirement for a control with no on-screen affordance.
        //
        // IT SUPPRESSES THE ONE-FINGER PATH ENTIRELY while it is down, and
        // that is the load-bearing part rather than a nicety: with a second
        // finger on the glass the first one still presses, still moves and
        // still releases, so a two-finger hold would ALSO dispatch a tap or a
        // swipe — the debug row would toggle and something else would happen
        // at the same time, which reads as the gesture doing two things at
        // random. The latch stays engaged across the panel's dropouts, so the
        // release that ends the gesture is swallowed too.
        const auto ev = _latch.update(nSlot, nowMs);
        if (ev == TwoFingerLatch::Event::Latched) {
            // BOTH points must be on the face. Anchoring on finger zero alone
            // would let a thumb resting on the band and one finger on the eyes
            // count as "in the eye zone". Sampling is DEFERRED to a pass that
            // really has two points: the latch may arm on the pass where the
            // panel momentarily shows one, and `getTouchPointRaw` clamps a
            // second index it cannot serve back onto the first — which would
            // silently judge one finger twice.
            _needEyes = true;
            // THE ABANDONED ONE-FINGER CONTACT STILL HAS TO END. Its consumer
            // holds a "this drag already scrolled" flag that only a dispatched
            // ending clears; walking away without one latches it and the next
            // band tap is eaten — the exact 08-04 regression the long-contact
            // branch below exists to prevent, arrived at from the other side.
            if (_touch_active && onBandDragEnd && _start_y >= tapZoneMaxY)
                onBandDragEnd();
        } else if (ev == TwoFingerLatch::Event::Fire) {
            if (onTwoFingerHoldEyes) onTwoFingerHoldEyes();
        }
        if (_needEyes && nSlot >= 2) {
            auto& r0 = M5.Touch.getTouchPointRaw(0);
            auto& r1 = M5.Touch.getTouchPointRaw(1);
            _latch.setEligible(r0.y < tapZoneMaxY && r1.y < tapZoneMaxY);
            _needEyes = false;
        }
        if (_latch.engaged()) {
            _touch_active = false;   // the one-finger gesture is abandoned
            return;
        }
        _needEyes = false;           // the gesture ended before it was judged

        if (nSlot == 0) {
            if (_touch_active) endContact();
            return;
        }

        // POSITION FROM THE RAW ARRAY, never from getDetail(). `_touch_raw` is
        // PACKED by the panel read, so index 0 is the first point reported
        // this pass whatever hardware id it carries — whereas getDetail(0) is
        // SLOT zero, which for a contact the panel numbered 1 is somebody
        // else's stale data, and which M5Unified's index-clamping accessor
        // gives no way to reach at all while only one point is down.
        auto& raw = M5.Touch.getTouchPointRaw(0);

        // ---- Record where the contact started ----
        if (!_touch_active) {
            _start_x = _last_x = raw.x;
            _start_y = _last_y = raw.y;
            _press_ms     = nowMs;
            _touch_active = true;
        } else {
            _last_x = raw.x;
            _last_y = raw.y;
        }

        // ---- Continuous band drag. The press frame reports dyTotal == 0,
        //      which is the consumer's accumulator reset. ----
        if (onBandDrag && _start_y >= tapZoneMaxY)
            onBandDrag(_start_x, _last_y - _start_y);
    }

    // START position of the last contact (valid after a tap/swipe) — lets the
    // handler decide based on the zone (e.g. a swipe inside the status bar).
    int startX() const { return _start_x; }
    int startY() const { return _start_y; }

private:
    // ---- End of contact → analyse the gesture ---------------------------
    // Driven by the finger count falling to zero rather than by
    // `getDetail(0).wasReleased()`: a contact the panel numbered 1 lives in a
    // detail slot M5UNIFIED CANNOT ADDRESS through its index-clamping
    // accessor, so a gesture layer keyed on that flag loses whole contacts
    // rather than mis-timing them. The last position is the one this class
    // recorded while the finger was down — the panel's own buffer stops being
    // refreshed at the lift, so keeping it here is not a cache, it is the only
    // copy with a defined lifetime.
    void endContact() {
        _touch_active = false;
        // LONG CONTACT (>= 700 ms) SWALLOWED: a resting thumb (robot being
        // carried, screen being wiped) fires NEITHER tap NOR swipe on
        // release — a long-standing long-press invariant, lost during the
        // pruning and then restored (max review 07-26).
        if (millis() - _press_ms >= 700) {
            if (onHoldBand && _start_y >= tapZoneMaxY &&
                millis() - _press_ms < 3000) {
                int hdx = abs(_last_x - _start_x);
                int hdy = abs(_last_y - _start_y);
                // TIGHTER than flickThreshold, and that is the point
                // (review 08-04): the timer's scroll applies one unit per
                // 20 px, so a slow 25 px slide to shave one second also
                // satisfied a 30 px "still" test and fired the RESET — a
                // gesture meant to change one digit destroyed the paused
                // countdown. A hold must move LESS than a single scroll
                // step, or the two are the same gesture with different
                // outcomes.
                if (hdx <= 8 && hdy <= 8) onHoldBand();
            }
            // THE DRAG STILL ENDS HERE. Without this the consumer's
            // "a scroll already happened" flag was set by onBandDrag and
            // cleared only by a tap/swipe callback — none of which this
            // branch dispatches — so after a >=700 ms slide the flag
            // LATCHED and swallowed the user's next tap: the countdown
            // would not start, and an alarm needed two presses to stop.
            if (onBandDragEnd && _start_y >= tapZoneMaxY) onBandDragEnd();
            return;
        }

        const int dx = _last_x - _start_x;
        const int dy = _last_y - _start_y;
        const int adx = abs(dx), ady = abs(dy);

        // Flick/Swipe — dominant movement
        if (adx > flickThreshold || ady > flickThreshold) {
            if (adx >= ady) {
                if (dx < 0) { if (onSwipeLeft)  onSwipeLeft();  }
                else        { if (onSwipeRight) onSwipeRight(); }
            } else {
                if (dy < 0) { if (onSwipeUp)    onSwipeUp();    }
                else        { if (onSwipeDown)  onSwipeDown();  }
            }
            return;
        }

        // Tap by horizontal zone — ONLY inside the eye area (above
        // tapZoneMaxY): the status bar triggers no wink/blink (it reacts
        // to L/R swipes through startY()).
        if (_start_y < tapZoneMaxY) {
            if      (_start_x < 107) { if (onTapLeft)   onTapLeft();   }
            else if (_start_x < 213) { if (onTapCenter) onTapCenter(); }
            else                     { if (onTapRight)  onTapRight();  }
        } else if (onTapBand) {
            onTapBand();
        }
    }

    bool     _touch_active = false;
    int      _start_x      = 0;
    int      _start_y      = 0;
    int      _last_x       = 0;    // last position seen while the finger was
    int      _last_y       = 0;    // down — see endContact()
    uint32_t _press_ms     = 0;
    // The two-finger hold's TIMING (arm, hold, release) — pure and natively
    // tested in test_twofinger. This class keeps only what needs the panel:
    // the coordinates and the callbacks.
    TwoFingerLatch _latch;
    bool     _needEyes     = false;   // latched, positions not sampled yet
};

} // namespace sce
