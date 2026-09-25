#pragma once
// =============================================================================
// input.h — led-fluid: the interaction VOCABULARY (PURE)
// =============================================================================
// Same shape as firmware/flight-radar/input.h and firmware/space/input.h, for
// the same reason: the touch panel and (on a board that has them) the three
// buttons are two PRODUCERS of semantic events, and `applyEvent()` in main.cpp
// is the only CONSUMER. Without that split, adding buttons means a second
// branch inside every gesture handler, and the two backends drift.
//
// THIS BIN'S UI WAS BUILT ON SLIDING, which is why it had no Fire port for so
// long: a swipe opens a panel, a drag moves a slider, a tap pokes the liquid at
// the point you touched. None of that survives having no touch panel — so the
// port is not "the same controls on buttons", it is a statement of what each
// control MEANT, mapped onto what a Fire has. The events below are that
// statement, and the tests pin it.
//
// ONE EVENT STILL CARRIES PIXELS: poking the fluid needs a point, and a button
// has none. `Act` in the fluid view therefore pokes the CENTRE, while a tap
// pokes where the finger landed. That is a real difference between the two
// boards rather than a shortfall to be papered over — the vocabulary says so
// out loud instead of pretending the backends are identical.
//
// No Arduino, no M5: the button state machine takes its time in milliseconds as
// a parameter, so it is natively testable (suite test_fluidinput).
// =============================================================================

#include <stdint.h>
#include "../common/Gesture.h"     // ONE swipe classifier, shared
#include "../common/ButtonFsm.h"   // ONE button bank, shared and tested (rule 17)
#include "panels.h"                // the row/tab model the cursor walks

namespace flu {

// The three screens. Declared HERE rather than in main.cpp so that the pure
// half can talk about them: a mapping from a button to an action is only
// reviewable if it can name the screen it applies to.
enum class View : uint8_t { Fluid, Settings, Colour };

enum class UiEvent : uint8_t {
    None,
    Prev,           // walk backwards through whatever is in front of you
    Next,           // and forwards
    Act,            // the view's PRIMARY action — see the table below
    Back,           // leave a panel; in the fluid view, nothing
    OpenSettings,
    OpenColour,
};

// =============================================================================
// Buttons (boards that have them — the Fire profile)
// =============================================================================
// THE MAPPING DOES NOT DEPEND ON THE VIEW, which is the rule the other two bins
// already follow: a control whose meaning changes with the screen is a control
// you have to think about. A and C always walk the thing in front of you, B
// always acts on it, and the long presses are the two places you can go.
//
//   A short  previous / decrease      A long  open the colour rectangle
//   B short  act                      B long  back to the fluid
//   C short  next / increase          C long  open the physics panel
//
// What "walk" and "act" mean per screen is `applyEvent`'s business, and it is
// one line each:
//
//   Fluid     Prev/Next  nothing            Act  poke the middle of the liquid
//   Settings  Prev/Next  the value under the cursor
//                                           Act  move the cursor to the next row
//   Colour    Prev/Next  the hue            Act  step the saturation
//
// Decreasing a value and selecting the previous row are the same ROLE — "walk
// backwards through what is in front of you" — which is why one pair of buttons
// covers both without either meaning two things.
enum class Button : uint8_t { A, B, C };

inline UiEvent buttonEvent(Button b, bool longPress) {
    if (longPress) {
        switch (b) {
            case Button::A: return UiEvent::OpenColour;
            case Button::B: return UiEvent::Back;
            default:        return UiEvent::OpenSettings;
        }
    }
    switch (b) {
        case Button::A: return UiEvent::Prev;
        case Button::B: return UiEvent::Act;
        default:        return UiEvent::Next;
    }
}

// The bank speaks in button INDICES (sce::BtnEvent); the mapping above speaks
// in this bin's Button enum. The bridge is a cast, stated once: A/B/C are 0/1/2
// in both, and this is where that equivalence is relied upon.
using sce::BTN_COUNT;
using sce::BTN_A;
using sce::BTN_B;
using sce::BTN_C;
using sce::BtnEvent;
using sce::ButtonFsm;

inline UiEvent buttonEvent(const BtnEvent& e) {
    return buttonEvent((Button)e.btn, e.isLong);
}

// =============================================================================
// Touch gestures → the same vocabulary
// =============================================================================
// The classification is pure; main.cpp supplies the pixels. Swipe DOWN is
// deliberately ABSENT: it belongs to SceGuest (back to the companion) on every
// guest bin, and this one does not bend that contract either.
//
// The swipes keep the meaning they have always had on this bin — right opens
// the physics panel, left opens the colours, and either one leaves a panel —
// so the port changes nothing for somebody holding a CoreS3.
struct Swipe { int dx = 0, dy = 0; };

inline bool isSwipe(const Swipe& s, int threshold = sce::gesture::SWIPE_PX) {
    return sce::gesture::isSwipe(s.dx, s.dy, threshold);
}

inline UiEvent swipeEvent(View v, const Swipe& s,
                          int threshold = sce::gesture::SWIPE_PX) {
    const auto dir = sce::gesture::classify(s.dx, s.dy, threshold);
    if (dir == sce::gesture::Dir::Down) return UiEvent::None;   // SceGuest's
    if (v == View::Fluid) {
        if (dir == sce::gesture::Dir::Right) return UiEvent::OpenSettings;
        if (dir == sce::gesture::Dir::Left)  return UiEvent::OpenColour;
        return UiEvent::None;
    }
    // Inside a panel either horizontal swipe leaves it — one way out, and the
    // finger does not have to remember which way it came in.
    if (dir == sce::gesture::Dir::Left || dir == sce::gesture::Dir::Right)
        return UiEvent::Back;
    return UiEvent::None;
}

// =============================================================================
// The settings cursor — only a button build has one
// =============================================================================
// A finger points at the row it wants; a button has to carry a cursor, and the
// cursor has to know that the two tabs are not the same length (seven physics
// sliders, six render switches). Walking off the end of one tab moves to the
// other rather than stopping: with two tabs and no dedicated tab button, that
// wrap IS the tab control.
//
// PURE and tested, because an off-by-one here is a row that cannot be reached
// at all — the kind of bug that hides until somebody wants the last switch.
inline int rowsOnTab(uint8_t tab) {
    int n = 0;
    for (int i = 0; i < sce::ui::paramCount(); i++)
        if (sce::ui::params()[i].tab == tab) n++;
    return n;
}

// Index into ui::params() of the `slot`-th row shown on `tab`, or -1.
inline int paramOnTab(uint8_t tab, int slot) {
    int n = 0;
    for (int i = 0; i < sce::ui::paramCount(); i++)
        if (sce::ui::params()[i].tab == tab && n++ == slot) return i;
    return -1;
}

struct Cursor { uint8_t tab = 0; int8_t row = 0; };

// Advance the cursor one row, wrapping through the other tab at each end.
inline Cursor nextRow(Cursor c) {
    c.row++;
    if (c.row >= (int8_t)rowsOnTab(c.tab)) {
        c.tab = (uint8_t)(c.tab == 0 ? 1 : 0);
        c.row = 0;
    }
    return c;
}

} // namespace flu
