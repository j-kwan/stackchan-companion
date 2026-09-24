#pragma once
// =============================================================================
// EyeEffects.h — StackChan-Companion (engine, device)
// =============================================================================
// Per-emotion decorative overlays (T7) — blush/sparkles/sweat effects
// reimplemented as pure functions for this architecture:
//
//   ARCHITECTURE: the effects are a PURE function (emotion, time) → drawing.
//   No state in the Brain, no extra FaceState channel: the Renderer calls
//   draw() after the eyes, into the same canvas — so the CRT post-process
//   applies to the effects too (visual consistency).
//
//   STYLE (Wall-E/Cozmo, ROADMAP §3.0): simple, blunt geometry, regular
//   procedural cycles (the deliberately mechanical side), appearance damped
//   through the transition progress (no popping in).
//
//   Effects:
//     BLUSH    (Blush, Glee, Smug)            : cluster of 4 thin, near-vertical
//                                               strokes under each eye, follows
//                                               the gaze
//     SPARKLES (Excited, Awe)                 : 3 stars twinkling on offset
//                                               cadences
//     SWEAT    (Scared, Worried, Frustrated)  : a drop that beads up and slides
//                                               down along the right eye, looping
//
// Colors: RGB888 (M5GFX converts them, lesson COLOR-2). The global dimming
// (colorDim) is applied by the caller to the colors passed in.
// =============================================================================

#include <M5GFX.h>
#include <cmath>
#include "Emotions.h"
#include "Units.h"

namespace sce {
namespace effects {

// Colors specific to the effects (dimmed by the Renderer through dimRgb888)
inline constexpr uint32_t BLUSH_RGB = 0xF07898;  // cheek pink
inline constexpr uint32_t SWEAT_RGB = 0x58B8F0;  // drop blue

inline bool hasBlush(eEmotions e) {
    return e == Blush || e == Glee || e == Smug;
}
inline bool hasSparkles(eEmotions e) {
    return e == Excited || e == Awe;
}
inline bool hasSweat(eEmotions e) {
    return e == Scared || e == Worried || e == Frustrated;
}

// ------------------------------------------------------------------
// Blushing cheeks (anime style): a cluster of 4 THIN, near-vertical strokes
// on each cheek, DIRECTLY under the eyes (not at the screen edges), MIRRORED
// left/right (kawaii look, symmetry discipline A2.17).
// fade in [0..1] scales the length.
// ------------------------------------------------------------------
// dx = horizontal eye offset (gaze); anchorY = vertical center of the cluster,
// placed A FEW PX BELOW the REAL bottom edge of the eyes → the cheeks follow
// the motion (gaze + squash) and stay attached to the face.
inline void drawBlush(M5Canvas* c, float fade, uint32_t rgb, float dx, float anchorY) {
    // 4 strokes of DIFFERENT LENGTHS (domed cluster, tallest near the center),
    // near-vertical, MIRRORED left/right (anime style).
    static constexpr float LENF[4] = { 0.60f, 1.00f, 0.85f, 0.65f };
    const float baseLen = 18.0f * fade;                   // height of the longest
    if (baseLen < 4.0f) return;
    // Horizontal lean PROPORTIONAL to the half-height → CONSTANT slope, so all
    // strokes stay PARALLEL (a small ratio means near-vertical).
    const float LEAN_RATIO = 0.32f;
    const float cx[2] = { units::EYE_L_CX - 18.0f + dx, units::EYE_R_CX + 18.0f + dx };
    const float gap = 9.0f;                               // spacing between strokes
    // A2.22 — NO ANTI-ALIASED PRIMITIVE ON THE PER-FRAME PATH, and this was the
    // last one left (found 08-03 by the frame-budget audit; the CHANGELOG
    // claimed the special renders had already stopped using `drawWideLine`,
    // which was true of the Dead "X" and false here). LovyanGFX rasterises a
    // wide line as a wedge: a `sqrtf` per pixel of the bounding box, then every
    // covered pixel blended through a 1×1 `fillRectAlpha` — a read-modify-write
    // with two `pixelcopy_t` PER PIXEL. That is the primitive that took a frame
    // to ~57 ms in July and starved loop()'s touch polling on the same core.
    // Blush, Glee and Smug all draw these eight strokes, every frame.
    //
    // Stamped circles instead, exactly like `drawDeadXEye`. The canvas is
    // RGB332, so the anti-aliasing being given up was quantised to 8 levels of
    // red to begin with — we were paying full price for a gradient the panel
    // cannot show.
    //
    // ONE fillCircle call site, in a loop, for the OTHER half of A2.22: GCC 8.4
    // Xtensa drops the second of two similar drawing calls in the same body,
    // and eight strokes written as eight calls is precisely that trap.
    for (int side = 0; side < 2; side++) {
        const float dir = side == 0 ? +1.0f : -1.0f;      // left/right mirror
        for (int i = 0; i < 4; i++) {                     // 4 centered strokes
            float half = 0.5f * baseLen * LENF[i];
            float lean = half * LEAN_RATIO;
            float x = cx[side] + (i - 1.5f) * gap;
            const float x0 = x - dir * lean, y0 = anchorY + half;
            const float x1 = x + dir * lean, y1 = anchorY - half;
            // TWO stamps per pixel of height, not one (user 08-03: the strokes
            // looked cut horizontally, as if drawn in two pieces).
            //
            // They are drawn in ONE piece — the breaks were BETWEEN STAMPS. At
            // one stamp per pixel the spacing is `2*half / floor(2*half)`,
            // which is >= 1 and lands on 2 whenever the fractional part
            // accumulates past a pixel. A `fillCircle` of radius 1 is a plus
            // shape, so two centres 2 px apart meet on their single centre
            // column — and since the lean shifts x by ~0.3 px per step, that
            // column often moves at the same time and they then touch only
            // diagonally. That gap, repeated at the same height across the
            // eight parallel strokes, reads as one horizontal cut.
            //
            // Half-pixel steps make consecutive stamps overlap whatever the
            // rounding does. ~37 stamps per stroke, ~300 per frame of a 1x1
            // fill: still an order of magnitude under the anti-aliased wedge
            // this replaced.
            int n = (int)(4.0f * half);
            if (n < 1) n = 1;
            for (int s = 0; s <= n; s++) {
                const float t = (float)s / (float)n;
                c->fillCircle((int)lroundf(x0 + (x1 - x0) * t),
                              (int)lroundf(y0 + (y1 - y0) * t), 1, rgb);
            }
        }
    }
}

// ------------------------------------------------------------------
// Twinkling ✦ stars: 3 fixed positions (free corners of the eye area), each
// pulsing on an 1100 ms cycle offset by 1/3 — a regular, deliberately
// mechanical cadence. Size ease-in-out 0→7→0 px.
// ------------------------------------------------------------------
inline void drawSparkles(M5Canvas* c, uint32_t nowMs, float fade, uint32_t rgb) {
    // Positions near the top corners of the eyes (2026-07-11: placed too high,
    // hugging the edge, they read as "yellow spilling over" — TESTS §4e.2)
    static constexpr int16_t POS[3][2] = { {46, 34}, {160, 24}, {272, 38} };
    static constexpr uint32_t PERIOD = 1100;
    for (int i = 0; i < 3; i++) {
        float t = (float)((nowMs + i * (PERIOD / 3)) % PERIOD) / (float)PERIOD;
        float pulse = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;   // triangle
        pulse = pulse * pulse * (3.0f - 2.0f * pulse);           // smooth
        int s = (int)(7.0f * pulse * fade);
        if (s < 2) continue;
        int x = POS[i][0], y = POS[i][1];
        c->drawFastVLine(x, y - s, 2 * s + 1, rgb);              // vertical arm
        c->drawFastHLine(x - s, y, 2 * s + 1, rgb);              // horizontal arm
        int d = s / 2;                                           // diagonals
        if (d > 0) {
            c->drawLine(x - d, y - d, x + d, y + d, rgb);
            c->drawLine(x - d, y + d, x + d, y - d, rgb);
        }
    }
}

// ------------------------------------------------------------------
// Sweat drop: beads up at the top right of the right eye, growing in place
// (0-40 % of the cycle), then slides down while stretching (40-85 %),
// vanishes, and starts over — 2400 ms cycle.
// ------------------------------------------------------------------
inline void drawSweat(M5Canvas* c, uint32_t nowMs, float fade, uint32_t rgb) {
    static constexpr uint32_t PERIOD = 2400;
    float t = (float)(nowMs % PERIOD) / (float)PERIOD;
    if (t > 0.85f) return;                       // pause between two drops
    const int x = units::EYE_R_CX + 62;
    int   y;
    float r;
    if (t < 0.40f) {                             // bead growing in place
        float g = t / 0.40f;
        y = 34;
        r = 2.0f + 3.0f * g;
    } else {                                     // sliding (accelerates: t²)
        float g = (t - 0.40f) / 0.45f;
        y = 34 + (int)(58.0f * g * g);
        r = 5.0f - 1.5f * g;
    }
    r *= fade;
    if (r < 1.5f) return;
    c->fillCircle(x, y, (int)r, rgb);
    // tip on top (teardrop shape)
    c->fillTriangle(x - (int)r + 1, y, x + (int)r - 1, y,
                    x, y - (int)(r * 2.2f), rgb);
}

// ------------------------------------------------------------------
// Renderer entry point: draws the overlay(s) of the current emotion.
// `fade` = transition progress (0→1, avoids popping in).
// `dim`  = tuning.eye_color_dim (kept consistent with the eye palette).
// `dx` = horizontal eye offset (gaze); `anchorY` = vertical center of the
// blush (below the real bottom edge of the eyes) → the blush follows motion.
// ------------------------------------------------------------------
inline void drawForEmotion(M5Canvas* c, uint32_t nowMs, eEmotions e,
                           float fade, float dim, float dx = 0.0f,
                           float anchorY = units::EYE_CY + 48) {
    if (fade <= 0.05f) return;
    if (hasBlush(e))    drawBlush(c, fade, dimRgb888(BLUSH_RGB, dim), dx, anchorY);
    if (hasSparkles(e)) drawSparkles(c, nowMs, fade, dimRgb888(0xFFF0A0, dim));
    if (hasSweat(e))    drawSweat(c, nowMs, fade, dimRgb888(SWEAT_RGB, dim));
}

} // namespace effects
} // namespace sce
