#pragma once
// =============================================================================
// EyeGeometry.h — StackChan-Companion (engine)
// =============================================================================
// Geometric normalization of an EyeConfig BEFORE drawing — the fix for the
// "shape falls apart, corner rounds spill out" bug (ROADMAP §2.3).
//
// CAUSE: the original drawer only clamped the radii against HEIGHT. But Width
// is shrunk independently of the radii by: the inter-eye spacing constraint,
// the blink (Width→120 while the radii shrink along a different curve), and
// the transitions between presets.
// As soon as Radius > Width/2, the corner points cross over and the Bresenham
// quarter-ellipses (drawn with the FULL radius from those points) spill out of
// the eye body.
//
// normalize() CONTRACT — invariants guaranteed on output, for ANY input:
//   I1. Width ≥ 0, Height ≥ 0                        (never negative)
//   I2. Radius_Top + Radius_Bottom ≤ totalHeight - 1
//   I3. 2 × max(Radius_Top, Radius_Bottom) ≤ Width   (new — §2.3)
//   I4. Inverse_Radius_* ≤ Width / 2                 (same spill-out possible)
//   I5. every radius ≥ 0
// Radius reductions are PROPORTIONAL (Top/Bottom keep their ratio) so the
// character of the shape survives the transitions.
//
// Call it from the drawer on the FINAL config — after transition, gaze,
// variation, blink AND the inter-eye constraint. Any Width reduction made
// downstream must come back through here.
//
// PURITY: no Arduino/M5GFX dependency → tested natively (test/test_geometry).
// The drawing itself (EyeDrawer.h) stays device-only.
// =============================================================================

#include <cstdint>
#include "EyeConfig.h"

namespace sce {
namespace eyegeom {

// -----------------------------------------------------------------------
// Effective total height: the edge slopes add/remove height at the ends
// (delta = H × slope / 2).
// -----------------------------------------------------------------------
inline int32_t totalHeight(const EyeConfig& c) {
    int32_t dTop    = (int32_t)(c.Height * c.Slope_Top    / 2.0f);
    int32_t dBottom = (int32_t)(c.Height * c.Slope_Bottom / 2.0f);
    return (int32_t)c.Height + dTop - dBottom;
}

// -----------------------------------------------------------------------
// normalize — enforces invariants I1..I5 (see header) in place.
// -----------------------------------------------------------------------
inline void normalize(EyeConfig& c) {
    // I1: dimensions are never negative (aggressive transitions/scales)
    if (c.Width  < 0) c.Width  = 0;
    if (c.Height < 0) c.Height = 0;

    // I5: radii are never negative (interpolation can drive them below 0)
    if (c.Radius_Top            < 0) c.Radius_Top            = 0;
    if (c.Radius_Bottom         < 0) c.Radius_Bottom         = 0;
    if (c.Radius_Top_Outer      < 0) c.Radius_Top_Outer      = 0;
    if (c.Radius_Bottom_Outer   < 0) c.Radius_Bottom_Outer   = 0;
    if (c.Inverse_Radius_Top    < 0) c.Inverse_Radius_Top    = 0;
    if (c.Inverse_Radius_Bottom < 0) c.Inverse_Radius_Bottom = 0;

    // I2: sum of radii ≤ total height - 1 (proportional Top/Bottom reduction
    // to keep the character of the shape). The outer variants (2026-07-16)
    // enter through the worst case (max of the side) and take the same
    // reduction — every corner pair stays within budget.
    int32_t th   = totalHeight(c);
    int32_t rT   = c.Radius_Top_Outer    > c.Radius_Top
                 ? c.Radius_Top_Outer    : c.Radius_Top;
    int32_t rB   = c.Radius_Bottom_Outer > c.Radius_Bottom
                 ? c.Radius_Bottom_Outer : c.Radius_Bottom;
    int32_t sumR = rT + rB;
    if (sumR > 0 && th - 1 < sumR) {
        float ratio = th > 1 ? (float)(th - 1) / (float)sumR : 0.0f;
        c.Radius_Top          = (int16_t)(c.Radius_Top          * ratio);
        c.Radius_Bottom       = (int16_t)(c.Radius_Bottom       * ratio);
        c.Radius_Top_Outer    = (int16_t)(c.Radius_Top_Outer    * ratio);
        c.Radius_Bottom_Outer = (int16_t)(c.Radius_Bottom_Outer * ratio);
    }

    // I3: no radius > Width/2 (THE §2.3 fix — never checked originally).
    // Proportional reduction across all radii.
    int32_t halfW = c.Width / 2;
    int32_t maxR  = c.Radius_Top > c.Radius_Bottom ? c.Radius_Top : c.Radius_Bottom;
    if (c.Radius_Top_Outer    > maxR) maxR = c.Radius_Top_Outer;
    if (c.Radius_Bottom_Outer > maxR) maxR = c.Radius_Bottom_Outer;
    if (maxR > halfW) {
        float ratio = maxR > 0 ? (float)halfW / (float)maxR : 0.0f;
        c.Radius_Top          = (int16_t)(c.Radius_Top          * ratio);
        c.Radius_Bottom       = (int16_t)(c.Radius_Bottom       * ratio);
        c.Radius_Top_Outer    = (int16_t)(c.Radius_Top_Outer    * ratio);
        c.Radius_Bottom_Outer = (int16_t)(c.Radius_Bottom_Outer * ratio);
    }

    // I4: concave corners clamped independently (no shape ratio to preserve
    // between them — each one carves its own arc)
    if (c.Inverse_Radius_Top    > halfW) c.Inverse_Radius_Top    = (int16_t)halfW;
    if (c.Inverse_Radius_Bottom > halfW) c.Inverse_Radius_Bottom = (int16_t)halfW;
}

// -----------------------------------------------------------------------
// isDrawable — an eye of (near) zero size must draw nothing.
// The drawer short-circuits on false (safe state, cf. EyeConfig defaults).
// -----------------------------------------------------------------------
inline bool isDrawable(const EyeConfig& c) {
    return c.Width > 0 && c.Height > 0;
}

} // namespace eyegeom
} // namespace sce
