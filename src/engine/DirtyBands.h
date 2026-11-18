#pragma once
// =============================================================================
// DirtyBands.h — StackChan-Companion (engine)
// =============================================================================
// WHAT THIS IS FOR (frame budget, diagnosed 08-03). Pushing the eye zone costs
// 320×160 px × 2 B on a 40 MHz SPI2 bus = 102 400 bytes = **20,5 ms of pure
// wire time**. That is arithmetic, not a measurement, and it dwarfs the ~3,5 ms
// that ALL the drawing code together costs. The only lever of the right order
// is therefore the NUMBER OF PIXELS PUSHED — so the renderer keeps a ghost copy
// of what the panel is showing, compares it with the freshly drawn canvas, and
// pushes only the rows that actually differ.
//
// This header is the comparison, and NOTHING else: two byte buffers in, a list
// of row bands out. It is pure (no Arduino, no LovyanGFX, no clock, no state)
// so it is tested natively — `test_dirtybands` — which is the only proof
// available without hardware for a change on the path every one of the 30
// expressions goes through.
//
// WHY ROWS AND NOT RECTANGLES. The push goes through
// `pushImage(x, y, w, h, const rgb332_t*)`, and LovyanGFX derives the source
// stride from `w` alone (`param->src_bitwidth = w` for 8 bpp): the data it
// reads MUST be `w * h` CONTIGUOUS bytes. With `w` = the full canvas width, a
// band of rows is exactly that — the sub-image is a plain slice of the canvas
// buffer, no copy, no stride juggling. A narrower rectangle would need a
// per-row call, i.e. one window setup per row, which is how you turn an
// optimisation into a regression.
//
// WHY THIS IS NOT A FILTER (A2.15). The Brain is the ONLY source of smoothing
// for the continuous channels, and "skip the frame if the movement is small"
// would be a filter living outside it. What is compared here is PIXELS ALREADY
// RENDERED, never a channel value: the drawing runs in full, every frame, and
// the result is bit-identical to a full push. This is a transport optimisation,
// and it stays legitimate exactly as long as it never looks at a channel.
//
// MERGING, AND WHY THE THRESHOLD THE RENDERER PASSES IS ZERO (recalibrated
// 08-03 — it was 2, and the accounting was upside down). Two dirty bands
// separated by a few CLEAN rows can be pushed as one, which buys back a band
// at the price of pushing rows nobody asked for. Both sides of that trade are
// computable from the numbers at the top of this file, and they are not close:
//   - ONE clean row pushed for nothing = 320 px × 2 B = 640 B = 5 120 bits on
//     a 40 MHz bus = **128 µs** of wire, the same arithmetic as above;
//   - ONE extra band = a window setup (CASET 1+4 B, RASET 1+4 B, RAMWR 1 B ≈
//     11 B ≈ 2,2 µs of wire) plus the LovyanGFX call overhead around it
//     (pixelcopy setup, bounds, a loop entry). The SPI transaction itself is
//     NOT part of it: `Renderer::pushEyeZone` brackets the whole band loop in
//     one `startWrite`/`endWrite`, so the bus is acquired once per frame, not
//     once per band. Generously **≤ 20 µs**.
// So merging a SINGLE clean row spends ~128 µs to save ≤ 20 µs — a 6× loss —
// and every further row is another 128 µs. The break-even gap is 20/128 < 1
// row; the gap is an integer, so the only defensible value is **0**. With
// `mergeGap = 2` the shipped renderer was spending up to 256 µs per merge to
// save 20, on the one path all 30 expressions go through.
// ZERO IS NOT "NO COALESCING": adjacent dirty rows have a gap of 0, so a block
// of rows — an eye, a whole blink line — still comes out as ONE band. It means
// "never push a clean row on purpose". `mergeGap` remains a PARAMETER (the
// tests exercise 0..5, and the fusion below rests on the same arithmetic);
// what the recalibration changed is the value `Renderer::MERGE_GAP` passes.
// `maxBands` bounds the per-band overhead when the frame is scattered
// (sparkles, sweat): once the array is full, the two bands separated by the
// SMALLEST gap are fused, which degrades gracefully towards one coarse band
// instead of collapsing to "push everything". That fusion is now the ONLY
// place a clean row is ever pushed — and it fuses the cheapest gap first,
// which is exactly the choice the arithmetic above would make.
// =============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace sce {
namespace dirty {

// A horizontal band of the canvas: rows [y, y+h) — full width, always.
struct Band {
    int16_t y = 0;
    int16_t h = 0;
};

// ---------------------------------------------------------------------------
// Compares `cur` and `ghost` (both `w * h` bytes, row-major, stride == w) and
// fills `out` with the bands of rows that differ.
//
// GUARANTEE (the one the tests pin down): every row where the two buffers
// differ is covered by exactly one band. Bands are ordered, disjoint and
// contained in [0, h). A band may contain IDENTICAL rows (merging) — pushing
// those is wasted wire time, never a wrong pixel.
//
// Returns the number of bands (0 = the two buffers are identical → nothing to
// push at all, which is the whole point).
// ---------------------------------------------------------------------------
inline int diffBands(const uint8_t* cur, const uint8_t* ghost,
                     int w, int h, Band* out, int maxBands, int mergeGap) {
    if (!cur || !ghost || !out || w <= 0 || h <= 0 || maxBands <= 0) return 0;
    if (mergeGap < 0) mergeGap = 0;

    int n = 0;
    for (int y = 0; y < h; y++) {
        const size_t off = (size_t)y * (size_t)w;
        if (memcmp(cur + off, ghost + off, (size_t)w) == 0) continue;

        if (n > 0) {
            // Distance in CLEAN rows between the end of the last band and
            // this one (0 = the band simply continues).
            const int gap = y - (out[n - 1].y + out[n - 1].h);
            if (gap <= mergeGap) {
                out[n - 1].h = (int16_t)(y + 1 - out[n - 1].y);
                continue;
            }
            if (n == maxBands) {
                // Array full. Fuse the CHEAPEST pair — the two bands with the
                // smallest gap between them, the new row included as a
                // candidate (ties go to the new row: extending the last band
                // keeps the array's shape and costs the same).
                int bi = -1, best = gap;
                for (int i = 0; i + 1 < n; i++) {
                    const int g = out[i + 1].y - (out[i].y + out[i].h);
                    if (g < best) { best = g; bi = i; }
                }
                if (bi < 0) {                       // extend the last band
                    out[n - 1].h = (int16_t)(y + 1 - out[n - 1].y);
                    continue;
                }
                out[bi].h = (int16_t)(out[bi + 1].y + out[bi + 1].h - out[bi].y);
                for (int i = bi + 1; i + 1 < n; i++) out[i] = out[i + 1];
                n--;
            }
        }
        out[n].y = (int16_t)y;
        out[n].h = 1;
        n++;
    }
    return n;
}

// Total number of rows the bands cover — × the canvas width, that is the
// pixel count actually put on the wire (telemetry: `Renderer::pushAvgPx`).
inline int32_t bandRows(const Band* b, int n) {
    int32_t rows = 0;
    for (int i = 0; i < n; i++) rows += b[i].h;
    return rows;
}

} // namespace dirty
} // namespace sce
