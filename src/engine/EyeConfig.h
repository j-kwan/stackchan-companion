#pragma once
// =============================================================================
// EyeConfig.h — StackChan-Companion (engine)
// =============================================================================
// Geometry of one rounded-rectangle eye.
// Ported from ESP32_Faces/EyeConfig.h (Luis Llamas, AGPL-3.0) — this file
// stays under AGPL-3.0 (see ROADMAP §3.1 "Licences").
// https://github.com/luisllamasbinaburo/ESP32_Faces
//
// INVARIANT (the "bands at boot" bug): EVERY member has an initialiser.
// An indeterminate struct outside of global objects, plus a transition
// started toward an EyeConfig that was never filled in, drew random geometry
// during the first few seconds. A default EyeConfig = zero-sized eye =
// NOTHING is drawn (safe state).
//
// Anatomy:
//
//   ╔══════════════════╗  ← Slope_Top: tilt of the upper edge
//   ║  Radius_Top      ║     (lid-eyebrow: the only tilt available, the K151
//   ║   (top corners)  ║      has no roll axis — ROADMAP §3.0-6)
//   ║  Radius_Bottom   ║
//   ╚══════════════════╝  ← Slope_Bottom: tilt of the lower edge
//
//   Inverse_Radius_* : re-entrant corners (carve out a concave arc)
//   OffsetX/Y        : centre offset vs the default position (px)
// =============================================================================

#include <cstdint>

namespace sce {

struct EyeConfig {
    int16_t OffsetX               = 0;  // horizontal offset of the centre (px)
    int16_t OffsetY               = 0;  // vertical offset of the centre (px)
    int16_t Height                = 0;  // total height (px) — 0 = nothing drawn
    int16_t Width                 = 0;  // total width (px) — 0 = nothing drawn

    float   Slope_Top             = 0.0f;  // upper edge tilt (0 = horizontal)
    float   Slope_Bottom          = 0.0f;  // lower edge tilt

    int16_t Radius_Top            = 0;  // radius of the top corners (px)
    int16_t Radius_Bottom         = 0;  // radius of the bottom corners (px)

    int16_t Inverse_Radius_Top    = 0;  // re-entrant top corner radius (0 = off)
    int16_t Inverse_Radius_Bottom = 0;  // re-entrant bottom corner radius

    int16_t Inverse_Offset_Top    = 0;  // horizontal offset, top re-entrant corner
    int16_t Inverse_Offset_Bottom = 0;  // horizontal offset, bottom re-entrant corner

    // ---- PER-SIDE radii (2026-07-16): OUTER corner ≠ inner corner ----
    // 0 = inherit the base radius (resolved by EyeRig::mirrored BEFORE the
    // transition, so that interpolation starts from the real value).
    // "Outer" = the screen-edge side of the eye (anatomical, it does NOT
    // follow the random mirroring of the presets) — mapped via OuterIsLeft.
    int16_t Radius_Top_Outer      = 0;  // outer top corner
    int16_t Radius_Bottom_Outer   = 0;  // outer bottom corner
    uint8_t OuterIsLeft           = 0;  // 1 = left eye (set by EyeRig)
};

} // namespace sce
