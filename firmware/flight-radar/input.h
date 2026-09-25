#pragma once
// input.h - the interaction VOCABULARY of flight-radar, and the button state
// machine that speaks it. PURE: no Arduino, no M5, no globals, time injected.
// Compiled natively by test/test_input, exactly like geo.h and SunClock.h.
//
// WHY THIS EXISTS. Every gesture in this bin used to call its action directly
// from handleTouch() - stepView(), notamStep(), cyclePlane(). That is fine
// while touch is the only input, and it stops being fine the moment a second
// board has to drive the same screens: the M5Stack Fire has three buttons and
// no touchscreen at all. Scattering #ifdefs through the gesture code would
// have left two half-readable input paths in one function.
//
// So the actions get NAMES. handleTouch() and the button reader are two
// PRODUCERS of the same UiEvent; applyEvent() in main.cpp is the single
// CONSUMER. Adding a third producer later (IR remote, web button) costs one
// function, not a rewrite.
//
// The event set is deliberately SMALLER than the touch gesture set: SelectAt
// (tap a blip) and the pinch-to-zoom preview have no button equivalent and
// stay touch-only. On a Fire you cycle through aircraft with A/C instead of
// aiming at them - which on a 100 px radar is arguably the better gesture.

#include <stdint.h>

// The button MECHANICS (debounce, boot priming, chord, one-event-per-call)
// are shared with the space bin — rule 17: one implementation, no assumed
// twins. This file keeps only the radar's VOCABULARY: which UiEvent a button
// means, on which screen.
#include "../common/ButtonFsm.h"

namespace fr {

// The semantic actions. Named after what the USER asked for, never after the
// gesture that asked for it - that is the whole point of the indirection.
enum class UiEvent : uint8_t {
    None = 0,
    NextView,     // one step on the view ring (radar -> METAR -> ... -> radar)
    Back,         // straight back to the radar from a reading screen
    PagePrev,     // previous card of the NOTAM deck
    PageNext,     // next card
    Refresh,      // force a METAR/TAF/NOTAM fetch now, ignoring the spacing
    NetInfo,      // show the IP and point at http://<ip>/config
    ZoomCycle,    // 50 / 100 / 250 / 500 nm, in that order
    TrackPrev,    // previous aircraft in the list
    TrackNext,    // next aircraft
};

// ---------------------------------------------------------------- buttons
// The bank itself — debounce, boot priming, chord(), arming(), the
// one-event-per-call rule — lives in firmware/common/ButtonFsm.h since 08-15:
// space grew a second, single-button FSM that lacked the priming and fired
// two long actions on A+C, which is the assumed-twin drift rule 17 exists to
// forbid. Re-exported here so every existing caller (main.cpp, test_input)
// keeps saying `fr::ButtonFsm` — the vocabulary is the radar's, the mechanics
// are the project's.
using sce::BTN_COUNT;
using sce::BTN_A;
using sce::BTN_B;
using sce::BTN_C;
using sce::BtnEvent;
using sce::ButtonFsm;

// WHICH SCREEN IS ASKING — three states, and NOT the bool this used to take.
//
// The bool could only say "radar" or "not radar", and "not radar" lumps the
// NOTAM deck, which HAS pages, together with the METAR and the TAF, which have
// none: both are one screen of text that ends where it ends (the TAF says
// "[...] TAF truncated" rather than continuing on a card two). So A and C were
// documented here and asserted by the tests as "page the deck" on all three,
// while the single consumer in main.cpp dropped the event on two of them
// (`viewLevel == VIEW_NOTAM`, and rightly so — moving the deck's index from a
// screen that is not the deck is worse). The table promised what the firmware
// refused: two dead keys on two screens out of four (review 08-03).
//
// A table that lies is worse than no table, so the SCREEN is what gets named.
// `Deck` means "a deck with cards IN it": an empty NOTAM screen is `Reading`,
// exactly the condition the touch path already computes
// (`viewLevel == VIEW_NOTAM && notamCount > 0`), so paging is never offered
// over nothing. The caller derives it in ONE place; applyEvent keeps its own
// guard, because touch reaches it too.
enum class Screen : uint8_t {
    Radar = 0,  // the sweep: aircraft to cycle, a radius to change
    Deck,       // a reading screen WITH cards to turn (the non-empty NOTAM deck)
    Reading,    // a reading screen with nothing to page (METAR, TAF, empty NOTAM)
};

// Maps a button press to an action, WHICH DEPENDS ON THE SCREEN. Pure, so the
// mapping table itself is what the native tests assert against.
//
//              | radar             | deck (NOTAM)  | reading (METAR/TAF)
//   A short    | previous aircraft | previous page | back to the radar
//   B short    | next view         | next view     | next view
//   C short    | next aircraft     | next page     | back to the radar
//   A long     | IP + /config      | IP + /config  | IP + /config
//   B long     | force refresh     | force refresh | force refresh
//   C long     | cycle the radius  | (no radius off the radar)
//
// A AND C MEAN "BACK" WHERE THERE IS NOTHING TO PAGE, rather than nothing at
// all, and that is the touch path's own rule brought over: on a reading screen
// a swipe or a tap that is not a page turn already means "back to the radar",
// precisely so that a gesture which does nothing can never leave you stuck.
// Two of the three buttons doing the same thing on one screen costs nothing —
// there is one thing to do there and both hands find it.
//
// B still reaches the radar from anywhere: it cycles THROUGH the ring and the
// radar is on it, so leaving is at most a few presses away on EVERY screen,
// including the deck where A and C are spoken for.
inline UiEvent buttonEvent(const BtnEvent& e, Screen s) {
    if (e.isLong) {
        switch (e.btn) {
            case BTN_A: return UiEvent::NetInfo;
            case BTN_B: return UiEvent::Refresh;
            case BTN_C: return s == Screen::Radar ? UiEvent::ZoomCycle
                                                  : UiEvent::None;
            default:    return UiEvent::None;
        }
    }
    switch (e.btn) {
        case BTN_A:
            return s == Screen::Radar ? UiEvent::TrackPrev
                 : s == Screen::Deck  ? UiEvent::PagePrev
                                      : UiEvent::Back;
        case BTN_B: return UiEvent::NextView;
        case BTN_C:
            return s == Screen::Radar ? UiEvent::TrackNext
                 : s == Screen::Deck  ? UiEvent::PageNext
                                      : UiEvent::Back;
        default:    return UiEvent::None;
    }
}

}  // namespace fr
