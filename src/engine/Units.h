#pragma once
// =============================================================================
// Units.h — StackChan-Companion (engine)
// =============================================================================
// NORMATIVE SOURCE for frames of reference, units and conversions — encodes
// docs/architecture/CONVENTIONS.md. Every conversion between layers goes
// through here: NO magic factor (/45, /60, ×2.5...) anywhere else in the code.
//
// Frame of reference: viewer-centric (docs/architecture/CONVENTIONS.md §1)
//   +X = the VIEWER's right   +Y = UP
//   gaze.x > 0 → eyes towards the viewer's right
//   (⚠ inverted vs the historical convention where gazeH>0 = viewer's left —
//   converted on import via gazeFromLegacyConvention)
//
// Layers and units (§2):
//   behavior/ : gaze units [-1..1], servo degrees, ms          (never px)
//   engine/   : the only place where pixels exist (pxFromGaze)
//
// PURITY: this header does NOT depend on Arduino → compiles natively
// (env `native`, unit tests). Include only <cstdint>/<cmath>.
// =============================================================================

#include <cstdint>
#include <cmath>

namespace sce {

// -----------------------------------------------------------------------
// Vec2f — minimal 2D vector (gaze, targets, velocities)
// Deliberately trivial: POD aggregate, basic operations only.
// -----------------------------------------------------------------------
struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2f operator+(const Vec2f& o) const { return { x + o.x, y + o.y }; }
    constexpr Vec2f operator-(const Vec2f& o) const { return { x - o.x, y - o.y }; }
    constexpr Vec2f operator*(float k)        const { return { x * k,   y * k   }; }
    Vec2f& operator+=(const Vec2f& o) { x += o.x; y += o.y; return *this; }
    float  length() const { return sqrtf(x * x + y * y); }
};

// Generic clamp (avoids Arduino constrain(), which is absent in native builds).
// `v != v` is the NaN self-inequality — both comparisons below are false for
// NaN, so an unguarded clamp lets it through untouched. Every hardware bound
// in this file (servo yaw, gaze, pitch) is built on this one function, so a
// NaN reaching any of them — a corrupt SD value, a bad atof() — would have
// skipped its clamp silently instead of being confined to it (caught by
// test_personality_feel sweeping degenerate inputs through the new
// personality-scale dials, 2026-09-19).
template <typename T>
constexpr T clampVal(T v, T lo, T hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }

namespace units {

// =========================================================================
// 1. Screen geometry (values validated on hardware)
// =========================================================================
inline constexpr int SCREEN_W   = 320;
inline constexpr int SCREEN_H   = 240;
inline constexpr int EYEZONE_H  = 160;  // eye zone = top 2/3
// DEFAULT eye centres (historical position, preserved by the default
// eye_spacing=14). Since 2026-07-13 the EFFECTIVE centres are computed by
// the Renderer: MID ∓ (EYE_HALF_W_MAX + eye_spacing) — eye_spacing =
// edge-to-edge gap in px, 0 = the eyes TOUCH.
inline constexpr int EYE_L_CX   = 90;   // left eye centre (screen px)
inline constexpr int EYE_R_CX   = 230;  // right eye centre
inline constexpr int EYE_CY     = 80;   // vertical centre of the eye zone
inline constexpr int EYE_HALF_W_MAX = 56; // half-width of the widest preset
                                          // (112) — the "contact" position
inline constexpr int EYE_GAP    = 0;    // anti-CROSSING guard of the
                                        // inter-eye clamp (0 since
                                        // 2026-07-13: edges touching is
                                        // ALLOWED — eye_spacing=0 —,
                                        // overlapping never is)

// =========================================================================
// 2. Gaze — [-1..1] units, maximum amplitudes and projection to pixels
// =========================================================================
inline constexpr float GAZE_MAX_X = 0.40f;  // max horizontal amplitude
inline constexpr float GAZE_MAX_Y = 0.20f;  // max vertical amplitude
inline constexpr float GAZE_PX_X  = 62.0f;  // px of travel at gaze.x = 1.0
inline constexpr float GAZE_PX_Y  = 50.0f;  // px at gaze.y = 1.0

// Gaze → screen pixels projection. The ONLY conversion point (used by
// EyeRig). The -Y turns "screen Y axis points down" into "+Y viewer = up".
inline constexpr Vec2f pxFromGaze(Vec2f g) {
    return { g.x * GAZE_PX_X, -g.y * GAZE_PX_Y };
}

// Clamps a gaze to the maximum amplitudes (defensive, used by the arbiter)
inline constexpr Vec2f clampGaze(Vec2f g) {
    return { clampVal(g.x, -GAZE_MAX_X, GAZE_MAX_X),
             clampVal(g.y, -GAZE_MAX_Y, GAZE_MAX_Y) };
}

// =========================================================================
// 3. SCS0009 servo, K151 — calibration measured on hardware
// =========================================================================
inline constexpr int YAW_CENTER    = 166;  // head facing forward (raw 463)

// Yaw amplitude — DERIVED, and it used to be a number with no source (08-01).
// It read `±40° around the centre` under a heading claiming a hardware
// measurement that nobody had written down, while the pitch limits beside it
// cite chapter and verse. Three facts settle it:
//   1. M5Stack's own StackChan page: "No angle restriction is required for the
//      X-axis" — the restriction it DOES state, and states loudly, is the
//      Y-axis 5~85° with its stall-and-permanent-damage warning. The SCS0009
//      is specified as 360-degree continuous on the horizontal axis.
//   2. Measured on the robot with the torque released (user 08-01): the head
//      turns several FULL revolutions, no resistance, no stop. The cable does
//      not cross the yaw axis — an earlier guess here said it did, and it was
//      wrong.
//   3. What actually binds is OUR OWN command path: ServoMotion::writeDeg maps
//      0-300 deg onto the SCS0009 position register and clamps there. With the
//      centre at 166, the reachable envelope is +134 / -166, so the widest
//      SYMMETRIC range is +/-134.
// 130 and not 134: four degrees of margin against the conversion clamp, so a
// commanded extreme never lands exactly on it. There is no stall risk here —
// nothing mechanical is being pressed, unlike the pitch end stops — the margin
// only keeps the arithmetic honest.
// TO GO FURTHER than this you must leave the 0-300 mapping, i.e. the servo's
// multi-turn mode: a different protocol path, not a bigger constant.
inline constexpr int YAW_SPAN_DEG  = 300;  // writeDeg's addressable span
inline constexpr int YAW_RANGE     = 130;  // was 40 — see above

// REFLEX envelope, and it is NOT the one above (review 08-02). Widening
// YAW_RANGE was argued for CHOREOGRAPHIES, but it is the clamp inside
// `ServoMotion::moveTo`, and two reflex paths walk the yaw RELATIVELY with
// no bound of their own — sound tracking and the post-startle turn both do
// `moveTo(yawDeg() + step, ...)`. At +/-40 such a walk stopped after a step
// or two; at +/-130 a sustained off-axis noise carries the head 130 deg off
// centre, screen turned away from the person it is meant to face.
// A dance ASKS for a big amplitude; a reflex should merely LOOK TOWARDS
// something. 40 is the value those paths were written and validated against.
inline constexpr int YAW_REFLEX_RANGE = 40;

// Clamp for the relative, accumulating movers. Absolute commands (dances,
// POST /api/servo) keep the full YAW_RANGE.
inline constexpr float clampReflexYaw(float yaw) {
    return clampVal(yaw, (float)(YAW_CENTER - YAW_REFLEX_RANGE),
                         (float)(YAW_CENTER + YAW_REFLEX_RANGE));
}
inline constexpr int PITCH_NEUTRAL = 93;   // HOME pose (user 2026-07-16:
                                           // 103→93, head slightly raised
                                           // → 10° of headroom to LOWER the
                                           // head: Sad/Blush/Sleepy...)
// Pitch limits — OFFICIAL M5Stack SPEC (docs.m5stack.com/en/StackChan,
// 2026-07-18): "Y-axis recommended within 5 ~ 85°. Operating at extreme
// angles may cause servo stall and permanent damage." The official frame
// (0-90°, 90 = max backward tilt) is the INVERSE of our measured raw frame
// (physical end stops: 14 = max raised, ~104 = horizon): raw ≈ 104 - official.
// → safe range 5..85 official = raw 19..99. The old bounds 15/103 HELD the
// mechanical end stop (NOD/SHY/cry dances, Sad) = servo stall.
inline constexpr int PITCH_MIN     = 19;   // = 85° official (max safe raise)
inline constexpr int PITCH_MAX     = 99;   // = 5° official (max safe lower —
                                           // NEVER the 103/104 end stop again)

// MAX "head down" posture bias actually reachable from the home pose.
// Beyond it the target exceeds PITCH_MAX, ServoMotion clamps it, and the
// head-at-home rest condition NEVER converges (servo re-command loop —
// Sleepy/Blush bug 2026-07-18). Derived: NEVER hardcode this 6 elsewhere.
inline constexpr int PITCH_DOWN_MAX = PITCH_MAX - PITCH_NEUTRAL;  // = 6

// Threshold of the opening channel (openL/openR) below which the eye counts as
// CLOSED: blink line on screen AND LED bar off. Shared Renderer↔EmotionLeds so
// the LED goes out at exactly the eye height where the blink line settles.
inline constexpr float EYE_CLOSED = 0.06f;

// Reference eye height (SIZE, not position) = maximum LED brightness:
// Preset_Surprised (112 px, the largest). Each eye's LED bar is lit
// ∝ its drawn height / this reference — a bigger eye is brighter
// (asymmetric Curious/Questioning), Surprised = full.
inline constexpr float EYE_HEIGHT_MAX = 112.0f;

// =========================================================================
// 4. Head ↔ gaze — replaces every /45 or /25 constant scattered elsewhere
// =========================================================================
inline constexpr float YAW_FULL_GAZE   = 45.0f; // ±45° yaw ↔ |gaze.x| = 1
inline constexpr float PITCH_FULL_GAZE = 25.0f; // 25° tilt ↔ gaze.y = 1

// FOLLOW-ALONG gaze: the eyes look in the direction the head points
// (aesthetic sync for the dances, behaviour validated on hardware).
// K151 servo: yaw < YAW_CENTER = head towards the viewer's left
//   → eyes towards the viewer's LEFT as well (gaze.x < 0).
// pitch < PITCH_NEUTRAL = head raised → eyes upwards (gaze.y > 0).
// NB: COMPENSATION (world-stable gaze while the head moves) is the VOR's
// business (VestibularSystem, gyro + efference copy) — not this file's.
inline constexpr Vec2f gazeFromHead(float yawDeg, float pitchDeg) {
    return { clampVal(-(YAW_CENTER    - yawDeg)   / YAW_FULL_GAZE,   -1.0f, 1.0f),
             clampVal( (PITCH_NEUTRAL - pitchDeg) / PITCH_FULL_GAZE, -1.0f, 1.0f) };
}

// Exact inverse (head-follow: where to turn the head to look at the target)
inline constexpr float headYawFromGaze(float gazeX) {
    return YAW_CENTER + gazeX * YAW_FULL_GAZE;    // +X viewer = yaw BEYOND the centre
}
inline constexpr float headPitchFromGaze(float gazeY) {
    return PITCH_NEUTRAL - gazeY * PITCH_FULL_GAZE;
}

// =========================================================================
// 5. IMU / VOR — gyroscope integration in gaze units
// =========================================================================
inline constexpr float DEG2GAZE_X = 1.0f / YAW_FULL_GAZE;
inline constexpr float DEG2GAZE_Y = 1.0f / PITCH_FULL_GAZE;

// =========================================================================
// 6. Importing historical data (keyframes, presets) — documents the
// conversion direction from the old convention (gazeH > 0 = viewer's left)
// to this project's viewer-centric convention (gaze.x = -gazeH; gazeV
// unchanged). To be used ONLY to port external data tables, never at
// runtime.
// =========================================================================
// IMPORT UTILITY (no firmware caller — used by test_units and to port
// inverted historical data, rule 6 / CLAUDE.md):
// DO NOT DELETE during a dead-code cleanup.
inline constexpr Vec2f gazeFromLegacyConvention(float gazeH_legacy, float gazeV_legacy) {
    return { -gazeH_legacy, gazeV_legacy };
}

// =========================================================================
// 7. Status-band clock layout — SHARED between the painter and the finger
// =========================================================================
// The band clock draws full-size digits CENTRED, with the phase icon set to
// their left. Both numbers below therefore MOVE with the text: "1/4 25:00"
// and "1/4 120:00" do not put the icon in the same place.
//
// That is exactly why this lives here and not as a literal on either side.
// The renderer positions the stamp with it (Renderer::drawBandClock) and the
// touch routing decides what a tap MEANT with it (main.cpp, mode 5: left of
// the digits = the icon = cycle the session's shape; on the digits =
// start/pause). A hit box written as a constant would be right for one text
// and quietly wrong for the others — the "diverging literal" this project
// keeps paying for elsewhere.
//
// Geometry of the digits: the built-in font is a 5x7 glyph in a 6x8 cell, so
// at size 4 one character occupies 24 px.
// --- the two fixed rails, and the row derived between them ---------------
// The progress rule: the band's FIRST rows, thin, growing from the CENTRE
// outward (user 08-25). It began under the digits and read as a second widget
// competing with them; at the very top it doubles as the separator between the
// face and the band, and the strip it used to occupy goes back to being air --
// which is what the band was short of. Shown ONLY by the modes that have
// progress to report (Renderer::drawBandRule): an always-lit divider was one
// line too many, and the unspent part is never drawn at all.
// Two rows. THREE is the hard ceiling rather than a taste -- the sound
// visualiser's zone starts at BAND_TOP + 3, so a 4 px rule would be the one
// piece of band furniture able to collide with another mode's content, and
// raising it means moving SND_TOP in the same breath.
inline constexpr int BAND_BAR_Y = EYEZONE_H;   // 160: the first band row
inline constexpr int BAND_BAR_H = 2;
// The status pill row, pinned near the bottom edge (battery, wifi, cam, mic,
// night). Lives here rather than in the Renderer because the text row's
// position is now DERIVED from it, and a second spelling of 225 would let the
// two drift apart.
inline constexpr int BAND_STATUS_Y = SCREEN_H - 15;   // 225

inline constexpr int BAND_CHAR_W = 6 * 4;          // 24 px per character
// THE TEXT ROW IS CENTRED BETWEEN THE RULE AND THE PILLS (user 08-25), not
// pinned 8 px under the top of the band as it was. With the rule moved to the
// top edge the old fixed offset left 12 px of air above the digits and 23 px
// below them: the band read as top-heavy, and the digits looked stuck to the
// rule. Deriving the row from its two neighbours makes the two gaps 17 and 18
// px — one apart, because 35 free rows do not halve evenly — and keeps them
// that way if the rule or the pill row ever moves again.
//
// The digits are size 4 in a 5x7-glyph/6x8-cell font: 28 rows of INK starting
// 6 px below the cursor, the rest of the 32 px cell being spacing. Centring
// the CELL would sink the ink, which is the same mistake the phase stamp's
// +8 offset already documents.
inline constexpr int BAND_TEXT_INK_H  = 28;   // rows of ink at size 4
inline constexpr int BAND_TEXT_INK_DY = 6;    // cursor -> first ink row
inline constexpr int BAND_DYN_Y =
    (BAND_BAR_Y + BAND_BAR_H)
    + (((BAND_STATUS_Y - (BAND_BAR_Y + BAND_BAR_H)) - BAND_TEXT_INK_H) / 2)
    - BAND_TEXT_INK_DY;                        // 173
// The 18x24 phase stamp, set 12 px clear of the first digit so it sits at the
// same visual distance as the colon's gap (user 08-04). Vertically centred on
// the INK of the digits, which is why it is +8 and not the +10 the 32 px cell
// would suggest.
inline constexpr int BAND_ICON_W   = 18;
inline constexpr int BAND_ICON_GAP = 12;
inline constexpr int BAND_ICON_H   = 24;
inline constexpr int BAND_ICON_Y   = BAND_DYN_Y + 8;

// THE ASSEMBLY IS CENTRED, NOT THE DIGITS (user 08-25). The digits used to be
// centred on their own with the stamp hung off their left, so the thing you
// actually see -- icon, air, clock -- sat 15 px left of centre and the band
// looked lopsided. Centring the WHOLE object is one subtraction, and it is the
// object the eye reads.
inline constexpr int bandAssemblyW(int nChars) {
    return BAND_ICON_W + BAND_ICON_GAP + nChars * BAND_CHAR_W;
}
inline constexpr int bandIconX0(int nChars) {
    return (SCREEN_W - bandAssemblyW(nChars)) / 2;
}
inline constexpr int bandTextX0(int nChars) {
    return bandIconX0(nChars) + BAND_ICON_W + BAND_ICON_GAP;
}
// How far right the digits sit compared with being centred ALONE -- exactly
// half the room the icon and its gap take up, and independent of the text
// length. `drawDynText` centres what it is given, so the band clock hands it
// this offset rather than a second centring rule of its own.
inline constexpr int BAND_TEXT_DX = (BAND_ICON_W + BAND_ICON_GAP) / 2;   // 15


} // namespace units
} // namespace sce
