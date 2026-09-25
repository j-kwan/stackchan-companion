#pragma once
// =============================================================================
// EyeDrawer.h — StackChan-Companion (engine)
// =============================================================================
// Draws one rounded-rectangle eye on an M5Canvas.
// Port of esp32-eyes/EyeDrawer.h (u8g2 → M5Canvas) — this file stays under
// AGPL-3.0:
//   Copyright (c) 2023 Alastair Aitchison, Playful Technology
//   Copyright (c) 2020 Luis Llamas (www.luisllamas.es)
//
// Notable points of the port:
//   - normalize() (EyeGeometry.h) called on the final config BEFORE drawing:
//     kills the whole "corner rounds spill out" bug class (the original only
//     clamped the radii against the height).
//   - isDrawable(): a null/degenerate geometry draws NOTHING (safe state, the
//     "bands at boot" fix).
//   - Draw() takes the config BY VALUE: normalization works on a copy, the
//     caller keeps its geometry intact.
//   - Pupil/highlight dropped (esp32-eyes style = solid shapes, no pupil).
//   - Clipping to the eye area is structural: the Renderer draws into a
//     dedicated 320×160 canvas — M5GFX clips automatically to the sprite
//     bounds, no setClipRect needed.
//
// Colors: uint32_t RGB888 — M5GFX converts according to the canvas depth
// (verified lesson: NEVER pre-convert).
//
// Geometric algorithm identical to the original:
//   1. 4 inner corners (TL/TR/BL/BR) with slope + offset
//   2. Central rectangle + bands out to the edges
//   3. Slope triangles (Slope_Top / Slope_Bottom)
//   4. Bresenham quarter-ellipses (rounded corners)
// =============================================================================

#include <M5GFX.h>
#include "EyeConfig.h"
#include "EyeGeometry.h"

namespace sce {

enum CornerType { T_R, T_L, B_L, B_R };

class EyeDrawer {
public:
    // Shared colors — written by EyeRig before each frame, read here.
    // Thread safety: Draw() is called exclusively by the renderer task.
    inline static uint32_t colorFg = 0x0096C8;  // eye color (cyan by default)
    inline static uint32_t colorBg = 0x000000;  // background

    // ------------------------------------------------------------------
    // Entry point: draws one complete eye centered on (centerX, centerY).
    // `config` by value: normalized locally (I1..I5, see EyeGeometry).
    // ------------------------------------------------------------------
    static void Draw(M5Canvas* canvas, int16_t centerX, int16_t centerY,
                     EyeConfig config) {
        eyegeom::normalize(config);                 // invariants §2.3
        if (!eyegeom::isDrawable(config)) return;   // null geometry → nothing

        // --- Slope deltas (height added/removed at the ends) ---
        int32_t delta_y_top    = (int32_t)(config.Height * config.Slope_Top    / 2.0f);
        int32_t delta_y_bottom = (int32_t)(config.Height * config.Slope_Bottom / 2.0f);

        // --- PER-CORNER radii (2026-07-16): the OUTER corner (screen-edge
        //     side, OuterIsLeft) can carry its own radius —
        //     Surprised (widened top outers), Awe (widened bottom outers).
        //     0 = inherit the base radius (raw presets used outside EyeRig).
        int32_t rT_out = config.Radius_Top_Outer    > 0 ? config.Radius_Top_Outer    : config.Radius_Top;
        int32_t rB_out = config.Radius_Bottom_Outer > 0 ? config.Radius_Bottom_Outer : config.Radius_Bottom;
        int32_t rTL = config.OuterIsLeft ? rT_out : config.Radius_Top;
        int32_t rTR = config.OuterIsLeft ? config.Radius_Top    : rT_out;
        int32_t rBL = config.OuterIsLeft ? rB_out : config.Radius_Bottom;
        int32_t rBR = config.OuterIsLeft ? config.Radius_Bottom : rB_out;

        // --- Coordinates of the 4 inner corners ---
        int32_t TLc_y = centerY + config.OffsetY - config.Height/2 + rTL - delta_y_top;
        int32_t TLc_x = centerX + config.OffsetX - config.Width/2  + rTL;
        int32_t TRc_y = centerY + config.OffsetY - config.Height/2 + rTR + delta_y_top;
        int32_t TRc_x = centerX + config.OffsetX + config.Width/2  - rTR;
        int32_t BLc_y = centerY + config.OffsetY + config.Height/2 - rBL - delta_y_bottom;
        int32_t BLc_x = centerX + config.OffsetX - config.Width/2  + rBL;
        int32_t BRc_y = centerY + config.OffsetY + config.Height/2 - rBR + delta_y_bottom;
        int32_t BRc_x = centerX + config.OffsetX + config.Width/2  - rBR;

        int32_t min_c_x = min(TLc_x, BLc_x);
        int32_t max_c_x = max(TRc_x, BRc_x);
        int32_t min_c_y = min(TLc_y, TRc_y);
        int32_t max_c_y = max(BLc_y, BRc_y);

        // --- Central body ---
        FillRect(canvas, min_c_x, min_c_y, max_c_x, max_c_y, colorFg);

        // --- Bands out to the rounded edges ---
        FillRect(canvas, TRc_x,       TRc_y, BRc_x + rBR, BRc_y, colorFg); // right
        FillRect(canvas, TLc_x - rTL, TLc_y, BLc_x,       BLc_y, colorFg); // left
        FillRect(canvas, TLc_x, TLc_y - rTL, TRc_x,       TRc_y, colorFg); // top
        FillRect(canvas, BLc_x, BLc_y,       BRc_x, BRc_y + rBR, colorFg); // bottom

        // --- Slope triangles (Slope_Top) ---
        if (config.Slope_Top > 0.0f) {
            FillRectTriangle(canvas, TLc_x, TLc_y - rTL,
                                     TRc_x, TRc_y - rTR, colorBg);
            FillRectTriangle(canvas, TRc_x, TRc_y - rTR,
                                     TLc_x, TLc_y - rTL, colorFg);
        } else if (config.Slope_Top < 0.0f) {
            FillRectTriangle(canvas, TRc_x, TRc_y - rTR,
                                     TLc_x, TLc_y - rTL, colorBg);
            FillRectTriangle(canvas, TLc_x, TLc_y - rTL,
                                     TRc_x, TRc_y - rTR, colorFg);
        }

        // --- Slope triangles (Slope_Bottom) ---
        if (config.Slope_Bottom > 0.0f) {
            FillRectTriangle(canvas, BRc_x + rBR, BRc_y + rBR,
                                     BLc_x - rBL, BLc_y + rBL, colorBg);
            FillRectTriangle(canvas, BLc_x - rBL, BLc_y + rBL,
                                     BRc_x + rBR, BRc_y + rBR, colorFg);
        } else if (config.Slope_Bottom < 0.0f) {
            FillRectTriangle(canvas, BLc_x - rBL, BLc_y + rBL,
                                     BRc_x + rBR, BRc_y + rBR, colorBg);
            FillRectTriangle(canvas, BRc_x + rBR, BRc_y + rBR,
                                     BLc_x - rBL, BLc_y + rBL, colorFg);
        }

        // --- Rounded corners (Bresenham quarter-ellipses, per-corner radius) ---
        if (rTL > 0) FillEllipseCorner(canvas, T_L, TLc_x, TLc_y, rTL, rTL, colorFg);
        if (rTR > 0) FillEllipseCorner(canvas, T_R, TRc_x, TRc_y, rTR, rTR, colorFg);
        if (rBL > 0) FillEllipseCorner(canvas, B_L, BLc_x, BLc_y, rBL, rBL, colorFg);
        if (rBR > 0) FillEllipseCorner(canvas, B_R, BRc_x, BRc_y, rBR, rBR, colorFg);
    }

    // ------------------------------------------------------------------
    // Rounded corner: Bresenham identical to the original
    // ------------------------------------------------------------------
    static void FillEllipseCorner(M5Canvas* canvas, CornerType corner,
                                  int16_t x0, int16_t y0,
                                  int32_t rx, int32_t ry, uint32_t color) {
        if (rx < 2 || ry < 2) return;
        canvas->setColor(color);

        int32_t x, y;
        int32_t rx2 = rx * rx, ry2 = ry * ry;
        int32_t fx2 = 4 * rx2,  fy2 = 4 * ry2;
        int32_t s;

        switch (corner) {
        case T_R:
            for (x = 0, y = ry, s = 2*ry2 + rx2*(1-2*ry); ry2*x <= rx2*y; x++) {
                canvas->drawFastHLine(x0, y0 - y, x);
                if (s >= 0) { s += fx2*(1-y); y--; }
                s += ry2*((4*x)+6);
            }
            for (x = rx, y = 0, s = 2*rx2 + ry2*(1-2*rx); rx2*y <= ry2*x; y++) {
                canvas->drawFastHLine(x0, y0 - y, x);
                if (s >= 0) { s += fy2*(1-x); x--; }
                s += rx2*((4*y)+6);
            }
            break;

        case B_R:
            for (x = 0, y = ry, s = 2*ry2 + rx2*(1-2*ry); ry2*x <= rx2*y; x++) {
                canvas->drawFastHLine(x0, y0 + y - 1, x);
                if (s >= 0) { s += fx2*(1-y); y--; }
                s += ry2*((4*x)+6);
            }
            for (x = rx, y = 0, s = 2*rx2 + ry2*(1-2*rx); rx2*y <= ry2*x; y++) {
                canvas->drawFastHLine(x0, y0 + y - 1, x);
                if (s >= 0) { s += fy2*(1-x); x--; }
                s += rx2*((4*y)+6);
            }
            break;

        case T_L:
            for (x = 0, y = ry, s = 2*ry2 + rx2*(1-2*ry); ry2*x <= rx2*y; x++) {
                canvas->drawFastHLine(x0 - x, y0 - y, x);
                if (s >= 0) { s += fx2*(1-y); y--; }
                s += ry2*((4*x)+6);
            }
            for (x = rx, y = 0, s = 2*rx2 + ry2*(1-2*rx); rx2*y <= ry2*x; y++) {
                canvas->drawFastHLine(x0 - x, y0 - y, x);
                if (s >= 0) { s += fy2*(1-x); x--; }
                s += rx2*((4*y)+6);
            }
            break;

        case B_L:
            for (x = 0, y = ry, s = 2*ry2 + rx2*(1-2*ry); ry2*x <= rx2*y; x++) {
                canvas->drawFastHLine(x0 - x, y0 + y - 1, x);
                if (s >= 0) { s += fx2*(1-y); y--; }
                s += ry2*((4*x)+6);
            }
            for (x = rx, y = 0, s = 2*rx2 + ry2*(1-2*rx); rx2*y <= ry2*x; y++) {
                canvas->drawFastHLine(x0 - x, y0 + y, x);
                if (s >= 0) { s += fy2*(1-x); x--; }
                s += rx2*((4*y)+6);
            }
            break;
        }
    }

    // ------------------------------------------------------------------
    // Solid rectangle between any two points
    // ------------------------------------------------------------------
    static void FillRect(M5Canvas* canvas,
                         int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                         uint32_t color) {
        int32_t l = min(x0, x1), r = max(x0, x1);
        int32_t t = min(y0, y1), b = max(y0, y1);
        int32_t w = r - l, h = b - t;
        if (w <= 0 || h <= 0) return;
        canvas->fillRect(l, t, w, h, color);
    }

    // ------------------------------------------------------------------
    // Right triangle (hypotenuse (x0,y0)-(x1,y1), right angle at (x1,y0))
    // ------------------------------------------------------------------
    static void FillRectTriangle(M5Canvas* canvas,
                                 int32_t x0, int32_t y0,
                                 int32_t x1, int32_t y1,
                                 uint32_t color) {
        canvas->fillTriangle(x0, y0, x1, y1, x1, y0, color);
    }
};

} // namespace sce
