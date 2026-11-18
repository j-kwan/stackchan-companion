#pragma once
// =============================================================================
// input.h — space: the interaction VOCABULARY (PURE)
// =============================================================================
// Same shape as firmware/flight-radar/input.h, and for the same reason: the
// touch panel and (on a board that has them) the buttons are two PRODUCERS of
// semantic events, and `applyEvent()` in main.cpp is the only CONSUMER. Adding
// a third producer — the dock timer — then costs one call, not a branch in
// every gesture handler.
//
// No Arduino, no M5: the button state machine takes its time in milliseconds
// as a parameter, so it is natively testable (suite test_spaceinput).
// =============================================================================

#include <stdint.h>
#include "../common/Gesture.h"   // ONE swipe classifier, shared

// The button MECHANICS (debounce, boot priming, chord, one-event-per-call)
// are shared with flight-radar — rule 17: one implementation, no assumed
// twins. This file keeps only space's VOCABULARY: which UiEvent a button
// means.
#include "../common/ButtonFsm.h"

namespace spa {

enum class UiEvent : uint8_t {
    None,
    NextView,      // ring: ISS → PASSES → MOON → SKY → LAUNCHES → ISS
    PrevItem,      // previous pass / previous launch (list cursor)
    NextItem,
    Back,          // leave a modal (the polar chart), otherwise ignored
    Refresh,       // force a fetch of the current view's source
    Settings,      // show the IP + point at http://<ip>/config
    Select,        // open the detail of the item under the cursor
};

// The five views, in ring order. The ORDER carries meaning, as on the radar:
// where the station is NOW (ISS), when it comes back (PASSES), then the sky
// that needs no instrument (MOON, SKY), then what leaves the ground next
// (LAUNCHES) — from the nearest object to the furthest intent.
enum class View : uint8_t { Iss, Passes, Moon, Sky, Launches, COUNT };

inline View nextView(View v) {
    return (View)(((uint8_t)v + 1) % (uint8_t)View::COUNT);
}

inline const char* viewName(View v) {
    switch (v) {
        case View::Iss:      return "ISS";
        case View::Passes:   return "PASSES";
        case View::Moon:     return "MOON";
        case View::Sky:      return "SKY";
        default:             return "LAUNCHES";
    }
}

// =============================================================================
// Buttons (boards that have them — the Fire profile)
// =============================================================================
// Three buttons, short press and long press. The mapping is a pure function so
// it can be read, reviewed and tested in one place instead of being scattered
// across the drawing code.
//
// IT DOES NOT DEPEND ON THE VIEW, and that is the same rule the swipes follow
// below: a control whose meaning changes with the screen is a control you have
// to think about. A/C always walk the list in front of you, B always advances
// the ring, and the long presses are the three things you need occasionally.
// On a view with no list (ISS, Moon) A and C simply do nothing — an inert
// button is honest, a button that silently means something else is not.
//
// The earlier mapping made C short mean Select on the two list views, which
// left those views with NO forward step: the cursor could only be walked
// backwards, on precisely the screens built around a cursor.
//
//   A short  previous item        A long  show the settings URL
//   B short  next view            B long  force a refresh
//   C short  next item            C long  open the item's detail
//
// Leaving a modal needs no button of its own: B closes it (see applyEvent),
// which is also what an upward swipe does. One way out, not two.
enum class Button : uint8_t { A, B, C };

inline UiEvent buttonEvent(Button b, bool longPress) {
    if (longPress) {
        switch (b) {
            case Button::A: return UiEvent::Settings;
            case Button::B: return UiEvent::Refresh;
            default:        return UiEvent::Select;
        }
    }
    switch (b) {
        case Button::A: return UiEvent::PrevItem;
        case Button::B: return UiEvent::NextView;
        default:        return UiEvent::NextItem;
    }
}

// THE BANK, not three independent single-button machines. The first version
// of this file kept its own one-button FSM, instantiated three times in
// loop() — which meant A+C held fired TWO long actions (Settings AND Select),
// and a button held while the board powered up fired its long action ~700 ms
// into boot: it had no priming. The shared bank (firmware/common/ButtonFsm.h)
// has both fixes, natively tested, and its `chord()` is what gives this bin
// the A+C debug overlay the radar already had. Re-exported so the callers say
// `spa::ButtonFsm`, like every other name of this vocabulary.
using sce::BTN_COUNT;
using sce::BTN_A;
using sce::BTN_B;
using sce::BTN_C;
using sce::BtnEvent;
using sce::ButtonFsm;

// The bank speaks in button INDICES (sce::BtnEvent); the mapping above speaks
// in this bin's Button enum. The bridge is a cast, stated once: A/B/C are 0/1/2
// in both, and this is where that equivalence is relied upon.
inline UiEvent buttonEvent(const BtnEvent& e) {
    return buttonEvent((Button)e.btn, e.isLong);
}

// =============================================================================
// Touch gestures → the same vocabulary
// =============================================================================
// The classification is pure; main.cpp supplies the pixels. Swipe DOWN is
// deliberately ABSENT: it belongs to SceGuest (back to the companion) on every
// guest bin, and this one does not bend that contract either.
struct Swipe { int dx = 0, dy = 0; };

// A MODAL DOES NOT CHANGE WHAT A SWIPE MEANS. The first version made a
// horizontal swipe mean "leave" inside a modal and "page" outside it, which
// took an `inModal` parameter and gave the same finger movement two jobs. The
// orbital chart settled it: paging through the planets there is the same
// intent as paging through passes on the list behind it. Leaving a modal is a
// TAP — one gesture, one job, the rule this bin already follows for the swipe
// down. Vertical ties go to the ring, the primary navigation.
// Was this movement a swipe AT ALL, whatever it turned out to mean?
//
// `swipeEvent` returns None for two different situations: "the finger barely
// moved" and "this is a downward swipe, which belongs to SceGuest". A caller
// that reads None as "not a swipe" then goes on to treat a real drag as a tap
// AT THE FINGER'S ORIGIN — opening whatever happened to sit there. The two
// answers need telling apart, and the threshold is the same one, so they are
// two questions about one classification rather than two classifications.
// BOTH DELEGATE. The classification is `sce::gesture`'s and shared with
// every other bin; what stays here is the MAPPING from a direction to this
// bin's vocabulary, which is the only part that is space's own. The threshold
// defaults to the shared one: this bin used 40 with no stated reason, six
// lines from a comment in another bin claiming that guests do not reinvent it.
inline bool isSwipe(const Swipe& s, int threshold = sce::gesture::SWIPE_PX) {
    return sce::gesture::isSwipe(s.dx, s.dy, threshold);
}

inline UiEvent swipeEvent(const Swipe& s, int threshold = sce::gesture::SWIPE_PX) {
    switch (sce::gesture::classify(s.dx, s.dy, threshold)) {
        case sce::gesture::Dir::Up:    return UiEvent::NextView;
        case sce::gesture::Dir::Down:  return UiEvent::None;   // SceGuest's
        case sce::gesture::Dir::Left:  return UiEvent::NextItem;
        case sce::gesture::Dir::Right: return UiEvent::PrevItem;
        default:                       return UiEvent::None;
    }
}

} // namespace spa
