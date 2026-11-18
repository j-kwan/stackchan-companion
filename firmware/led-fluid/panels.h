#pragma once
// =============================================================================
// panels.h — ONE table, four consumers
// =============================================================================
// A guest setting crosses four hand-written lists: the panel that draws it,
// the hit-test that finds it, the yaml that persists it and the addSetting()
// that publishes it on /config. The radar earned the rule for its rows after a
// row drawn at one height was hit-tested at another; check-guest-config.py was
// written after a key that loadConfig read and saveConfig did not write was
// DESTROYED on the first save from the web page. Both failures are the same
// failure: a list that drifted from its twin.
//
// So there is one table. The panel reads it, the finger reads it, the yaml
// reads it, /config reads it. Adding a setting is adding a row.
//
// PURE: no Arduino. Tested natively (test/test_fluidui).
// =============================================================================

#include <stdint.h>
#include <string.h>

namespace sce {
namespace ui {

enum class Kind : uint8_t { Num, Bool };

// Tab 2 is DRAWN BY NOBODY, and the reason is narrow: hue, saturation and
// brightness already have a widget, the colour rectangle. A second slider for
// them on the settings panel would be two controls writing one value,
// disagreeing on screen the moment somebody moved only one. A row here still
// persists and publishes them — which is the whole point of the tab.
//
// It used to hold the three IMU switches as well, on the argument that they are
// "set once". That argument was doing no work: they have no other widget, so
// putting them on a tab duplicates nothing, and "set once" is exactly the case
// that needs to be reachable from the robot — the person who can see which way
// the fluid runs is holding it, not reading /config on a laptop. They are on
// the RENDER tab now.
inline constexpr uint8_t TAB_HIDDEN = 2;

struct Param {
    const char* key;      // yaml key AND /config key — one spelling, everywhere
    const char* en;
    const char* fr;
    Kind        kind;
    int16_t     lo, hi, def;
    uint8_t     tab;      // 0 = PHYSICS, 1 = RENDER, 2 = not drawn
};

inline const Param* params() {
    static const Param P[] = {
        // --- tab 0: PHYSICS (seven sliders, and ROWS_PER_TAB is seven) ---
        { "particles", "Particles",    "Particules",      Kind::Num,  60, 400, 240, 0 },
        { "dot_pitch", "Dot size",     "Taille pastille", Kind::Num,   8,  20,  12, 0 },
        { "viscosity", "Viscosity",    "Viscosite",       Kind::Num,   0, 100,  30, 0 },
        { "gravity",   "Gravity",      "Gravite",         Kind::Num,   0, 200, 100, 0 },
        { "bounce",    "Bounce",       "Rebond",          Kind::Num,   0,  90,  25, 0 },
        { "trail",     "Trail",        "Trainee",         Kind::Num,   0,  95,  60, 0 },
        { "gyro_gain", "Gyro gain",    "Gain gyro",       Kind::Num,   0, 200, 100, 0 },
        // --- tab 1: RENDER (six switches, every one off by default) ---
        { "leds",      "WS2812 echo",  "Echo WS2812",     Kind::Bool,  0,   1,   0, 1 },
        { "sound",     "Splash sound", "Son eclaboussure",Kind::Bool,  0,   1,   0, 1 },
        { "auto_bright","Auto brightness","Luminosite auto",Kind::Bool,0,   1,   0, 1 },
        // RENAMED from imu_* (08-23), and the rename IS the migration. The
        // measured mapping moved into the baseline, so the correct value of
        // every one of these went from "inv_x on" to "all off" — and a card
        // already on a robot carried the old answer, which saveConfig rewrites
        // in full and loadConfig applies without question. Under the old names
        // the upgrade would have silently re-inverted X on exactly the robots
        // that had been set up correctly. loadConfig ignores keys it does not
        // know and saveConfig truncates, so a new name retires the old value on
        // its own. `tilt_` is also the honest word: they invert the TILT the
        // fluid is given, not the sensor.
        { "tilt_swap",  "Swap tilt axes","Axes inclinaison permutes",Kind::Bool, 0, 1, 0, 1 },
        { "tilt_inv_x", "Invert X",      "Inverser X",       Kind::Bool,  0,   1,   0, 1 },
        { "tilt_inv_y", "Invert Y",      "Inverser Y",       Kind::Bool,  0,   1,   0, 1 },
        // --- not drawn: owned by the colour panel ---
        { "hue",       "Hue",          "Teinte",          Kind::Num,   0, 359, 200, TAB_HIDDEN },
        { "sat",       "Saturation",   "Saturation",      Kind::Num,   0, 100,  85, TAB_HIDDEN },
        { "bright",    "Brightness",   "Luminosite",      Kind::Num,  10, 255, 120, TAB_HIDDEN },
    };
    return P;
}
inline int paramCount() { return 16; }

inline int paramIndex(const char* key) {
    for (int i = 0; i < paramCount(); i++)
        if (strcmp(params()[i].key, key) == 0) return i;
    return -1;
}

// ---- geometry: the ONE description of a row --------------------------------
struct Rect { int16_t x, y, w, h; };

inline constexpr int SCR_W        = 320;
inline constexpr int SCR_H        = 240;
inline constexpr int TAB_H        = 30;    // the tab strip at the top
inline constexpr int ROW_H        = 24;
inline constexpr int ROW_GAP      = 2;
inline constexpr int ROWS_PER_TAB = 7;
inline constexpr int ROWS_TOP     = TAB_H + 4;
inline constexpr int SLIDER_X0    = 150;   // labels left, slider right
inline constexpr int SLIDER_X1    = 305;
// The preset row, PHYSICS tab only. It must start BELOW the last slider row,
// which ends at 213 (ROWS_TOP + 6 x 26 + 24): at 212 it overlapped the last two
// pixels of `gyro_gain`, and since a tap tests the chips FIRST while a drag
// still moved the slider, those two pixels did one thing to a tap and another
// to a drag. `test_fluidui` asserts the gap now.
inline constexpr int PRESET_Y     = 216;
inline constexpr int PRESET_H     = 22;
inline constexpr int PRESET_W     = 58;
inline constexpr int PRESET_X0    = 6;
inline constexpr int PRESET_STEP  = 62;

inline Rect rowRect(int slot) {
    return Rect{ 8, (int16_t)(ROWS_TOP + slot * (ROW_H + ROW_GAP)),
                 (int16_t)(SCR_W - 16), (int16_t)ROW_H };
}

inline int rowAt(int y) {
    if (y < ROWS_TOP) return -1;
    const int slot = (y - ROWS_TOP) / (ROW_H + ROW_GAP);
    if (slot < 0 || slot >= ROWS_PER_TAB) return -1;
    const Rect r = rowRect(slot);
    if (y >= r.y + r.h) return -1;          // the gap belongs to no row
    return slot;
}

// Which preset a finger fell on, -1 for none. Same shape as rowAt: the drawing
// and the hit-test share the constants above rather than each holding a copy.
inline int presetAt(int x, int y) {
    if (y < PRESET_Y || y >= PRESET_Y + PRESET_H) return -1;
    if (x < PRESET_X0) return -1;
    const int i = (x - PRESET_X0) / PRESET_STEP;
    if ((x - PRESET_X0) % PRESET_STEP >= PRESET_W) return -1;   // between two chips
    return i;
}

inline int valueAt(const Param& p, int x) {
    if (x <= SLIDER_X0) return p.lo;
    if (x >= SLIDER_X1) return p.hi;
    const int span = SLIDER_X1 - SLIDER_X0;
    return p.lo + (int)(((long)(x - SLIDER_X0) * (p.hi - p.lo) + span / 2) / span);
}

// ROUNDS, like valueAt. Truncating here while rounding there stacked the two
// errors in the same direction: a hue of 90 came back as 88, and the knob
// visibly sat left of the value printed beside it.
inline int sliderX(const Param& p, int value) {
    if (value <= p.lo) return SLIDER_X0;
    if (value >= p.hi) return SLIDER_X1;
    const int span = SLIDER_X1 - SLIDER_X0;
    const int range = p.hi - p.lo;
    return SLIDER_X0 + (int)(((long)(value - p.lo) * span + range / 2) / range);
}

// What one pixel of slider is worth, in units of this parameter — rounded up.
// It is the resolution the widget actually has: hue spans 360 values across
// 155 px, so no amount of care makes that round trip exact, and a test that
// demanded it would be demanding a bigger screen.
inline int unitsPerPixel(const Param& p) {
    const int span = SLIDER_X1 - SLIDER_X0;
    return ((p.hi - p.lo) + span - 1) / span;
}

// ---- the colour rectangle: X = hue, Y = saturation --------------------------
// Y is INVERTED — saturated at the top — because that is what the gradient
// shows, and a picker whose finger disagrees with its own picture is worse
// than no picker.
inline Rect hueRect() { return Rect{ 16, 44, 288, 120 }; }

inline int hueAt(int x) {
    const Rect r = hueRect();
    if (x <= r.x) return 0;
    if (x >= r.x + r.w - 1) return 359;
    return (int)((long)(x - r.x) * 359 / (r.w - 1));
}

inline int satAt(int y) {
    const Rect r = hueRect();
    if (y <= r.y) return 100;
    if (y >= r.y + r.h - 1) return 0;
    return 100 - (int)((long)(y - r.y) * 100 / (r.h - 1));
}

// The brightness slider of the colour panel, which is NOT a settings row: it
// sits under the rectangle so a hue can be judged at the brightness it will be
// seen at. Its geometry lives here for the same reason the rows' does.
inline constexpr int BRI_X0 = 20;
inline constexpr int BRI_X1 = 300;
inline constexpr int BRI_Y  = 195;
inline constexpr int BRI_H  = 30;          // the band a finger may land in

inline bool inBrightness(int y) { return y >= BRI_Y - BRI_H / 2 && y <= BRI_Y + BRI_H / 2; }

inline int brightnessAt(const Param& p, int x) {
    if (x <= BRI_X0) return p.lo;
    if (x >= BRI_X1) return p.hi;
    const int span = BRI_X1 - BRI_X0;
    return p.lo + (int)(((long)(x - BRI_X0) * (p.hi - p.lo) + span / 2) / span);
}

inline int brightnessX(const Param& p, int value) {
    if (value <= p.lo) return BRI_X0;
    if (value >= p.hi) return BRI_X1;
    return BRI_X0 + (int)((long)(value - p.lo) * (BRI_X1 - BRI_X0) / (p.hi - p.lo));
}

} // namespace ui
} // namespace sce
