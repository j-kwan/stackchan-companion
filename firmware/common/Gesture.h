#pragma once
// =============================================================================
// Gesture.h — ONE swipe classifier, and the constants a press is made of
// =============================================================================
// WHY IT EXISTS. The same question — "was that a swipe, and which way?" — was
// answered in SEVEN places across four firmwares, with FIVE thresholds and
// THREE different tie-break rules. Two of the divergences were accidental in
// the plainest way: ha-remote wrote "same threshold as flight-radar — a guest
// does not reinvent its own" above a 60, then used 50 six lines below it with
// no comment at all; and the space bin used 40 without ever saying why.
//
// A THRESHOLD IS NOT A PREFERENCE, it is a contract with the gesture ABOVE it.
// SceGuest owns the downward swipe past `EXIT_PX` — that is how any guest bin
// is left — so every bin's own gestures live in the band below it. When the
// two numbers are written in different files nobody compares them, and the gap
// between "my swipe fired" and "the exit fired" becomes a place where a real
// drag is read as a TAP AT THE FINGER'S ORIGIN. That is not hypothetical: it
// is the space bin's 08-14 bug, where a half-hearted exit opened a pass detail
// or picked a planet. Both numbers now sit here, next to each other, and a
// native test asserts the band between them is real.
//
// PURE: no Arduino, no M5 — so it is tested natively like `engine/` and
// `behavior/` (test/test_gesture).
// =============================================================================

#include <stdint.h>

// >>> VENDORABLE BEGIN Gesture <<<
// NEEDS: <stdint.h>
namespace sce {
namespace gesture {

// ---- THE TWO DISTANCES -----------------------------------------------------
// A bin's own swipe. 60 px on a 320x240 panel is about a fifth of the screen:
// far enough that a shaky tap is not one, short enough that a thumb can do it
// without crossing the panel. It is the radar's value, the one earned on
// hardware first and the one the other bins claimed to be copying.
inline constexpr int SWIPE_PX = 60;

// SceGuest's exit (downward, vertically dominant). NOT the same kind of
// number: it is deliberately far above SWIPE_PX so leaving a bin cannot be
// done by accident, and every bin's own gestures must fit underneath.
inline constexpr int EXIT_PX = 100;

// ---- THE PRESS -------------------------------------------------------------
// A long press is 700 ms everywhere and always was; what diverged is what
// counts as "the finger stayed put" (12 px on the radar, 20 in space) and
// whether anything on screen says the press was SEEN. The radar grew that
// acknowledgement on a user report: with no feedback you lift the finger and
// cancel your own gesture.
inline constexpr uint32_t LONG_MS = 700;
inline constexpr uint32_t LONG_ARM_MS = 250;   // when the screen should answer
inline constexpr int      LONG_SLOP_PX = 12;   // drift still counted as still

// ---- THE CLASSIFICATION ----------------------------------------------------
enum class Dir : uint8_t { None, Up, Down, Left, Right };

inline int absi(int v) { return v < 0 ? -v : v; }

// Did the finger travel far enough to mean anything, whatever it meant?
//
// Asked separately from `classify` on purpose. A caller that only checks the
// DIRECTION gets `None` for two unrelated situations — "barely moved" and "a
// direction this screen does nothing with" — and if it then falls through to
// its tap branch, a real drag is resolved at the point where the finger went
// DOWN. One classification, two questions about it.
inline bool isSwipe(int dx, int dy, int px = SWIPE_PX) {
    return absi(dx) >= px || absi(dy) >= px;
}

// ONE tie-break rule, and it favours the VERTICAL axis. An exact diagonal has
// to go somewhere; vertical is where the primary navigation lives in every bin
// that has one (the view ring), so a 45-degree drag pages views rather than
// doing nothing. The radar used to answer "neither" and the space bin already
// answered "vertical" — the rule was never the disagreement, only its absence.
inline Dir classify(int dx, int dy, int px = SWIPE_PX) {
    if (!isSwipe(dx, dy, px)) return Dir::None;
    if (absi(dy) >= absi(dx)) return dy < 0 ? Dir::Up : Dir::Down;
    return dx < 0 ? Dir::Left : Dir::Right;
}

}  // namespace gesture
}  // namespace sce
// >>> VENDORABLE END Gesture <<<
