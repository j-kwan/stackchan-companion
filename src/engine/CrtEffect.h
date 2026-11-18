#pragma once
// =============================================================================
// CrtEffect.h — StackChan-Companion (engine)
// =============================================================================
// "CRT screen" post-processing (ROADMAP §3.8), applied to the 8-bit RGB332
// buffer of the eye-zone canvas, between drawing and pushing it.
//
// Components (measured during the P-1 spike, per-pixel costs on the 320×160
// zone):
//   scanlines : phosphor DOT MASK (user request 2026-07-12, reference photo
//               docs/crt.webp — retired from the public repo at publication)
//               — 3×3 cells: 2×2 bright dots, 1 px gutters
//               strongly darkened both H and V. The CrtOptions flag keeps
//               the name `scanlines` (stable API).               (~4 ms)
//   phosphor  : persistence — buffer decay + max(new, faded)   (~4.3 ms)
//               → trails only visible on fast movements: direct synergy
//               with the crisp saccades (§3.0)
//   flicker   : "drops" — whole frames randomly darkened (LUT, ~1 frame in
//               90 ≈ every 3 s at 30 Hz). The initial implementation
//               (setBrightness ±3 % per frame) is FORBIDDEN: on the CoreS3
//               setBrightness is an I2C transaction to the AXP2101 PMIC;
//               30 calls/s from the renderer collided with the internal I2C
//               bus (touch/power) → the screen cut out (bug observed on
//               hardware during P1b, 2026-07-06)
//   glow      : NOT here — the halo is a second dilated draw in a dimmed
//               colour, done by EyeRig::draw (glowPx parameter) BEFORE the
//               post-process; it is not a per-pixel filter.
//
// Budget: at 40 MHz the bare P1a frame is ~21.4 ms (33 ms period); scanlines
// + phosphor add ~7 ms → ~28.5 ms, which fits. Continuously checked by the
// frame telemetry (anti-starvation rule §3.5).
//
// Thread: everything here is called by the renderer task only.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <Arduino.h>       // esp_random for the flicker micro-drop
#include <esp_heap_caps.h>
#include "FaceState.h"     // CrtOptions

namespace sce {

class CrtEffect {
public:
    // ------------------------------------------------------------------
    // Allocates the persistence buffer (w×h bytes, PSRAM preferred — read
    // and written by the CPU only, never by DMA). Builds the LUTs.
    // ------------------------------------------------------------------
    bool begin(int w, int h) {
        _w = w; _h = h;
        buildLuts();
        _phosphor = (uint8_t*)heap_caps_calloc((size_t)w * h, 1,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_phosphor) _phosphor = (uint8_t*)calloc((size_t)w * h, 1);
        return _phosphor != nullptr;
    }

    // ------------------------------------------------------------------
    // Applies the active components to the RGB332 canvas buffer.
    // NO access to M5.Display here ("single owner" rule, and the PMIC must
    // never be poked once per frame) — everything is per-buffer.
    // ------------------------------------------------------------------
    void apply(uint8_t* buf, const CrtOptions& opt, uint32_t /*nowMs*/) {
        if (!opt.enabled) {
            _phosphorDirty = true;     // to be purged on the next activation
            return;
        }

        if (opt.phosphor && _phosphor) {
            if (_phosphorDirty) {      // purge after an off period
                memset(_phosphor, 0, (size_t)_w * _h);
                _phosphorDirty = false;
            }
            applyPhosphor(buf);
        }
        if (opt.scanlines) applyDotMask(buf);
        // Flicker "drop": ~1 frame in 90 darkened entirely (LUT).
        // Costs ~2.5 ms on THAT frame only — the 33 ms budget still holds.
        if (opt.flicker && (esp_random() % 90) == 0) {
            const int n = _w * _h;
            for (int i = 0; i < n; i++) buf[i] = _lutDim50[buf[i]];
        }
    }

    // Halo (glow): dimmed colour derived from the eye colour — used by the
    // Renderer for EyeRig's dilated drawing pass.
    uint8_t dim332(uint8_t c) const { return _lutDim50[c]; }

    // Halo: its parameters (dilation, intensity) now live in the Tuning
    // registry (crt_glow_px/crt_glow_dim, defaults = P1b verdicts
    // 3 px / 28 %) and travel through CrtOptions — no more constants here.

private:
    int      _w = 0, _h = 0;
    uint8_t* _phosphor = nullptr;
    bool     _phosphorDirty = false;
    uint8_t  _lutDim50[256];   // drops/halo: ~50 % per RGB332 component
    uint8_t  _lutDim25[256];   // dot-mask gutters: ~25 %
    uint8_t  _lutDecay[256];   // phosphor: -1 per component (~-12 %/frame)

    void buildLuts() {
        // Phosphor decay: -1 per component per frame → a ~7 frame trail
        // (~230 ms at 30 Hz). User verdict P1b (2026-07-07): this duration
        // is the right one — it was the HALO (glow) that was too strong,
        // not the trail. Will become tuning.crt_phosphor_decay.
        for (int c = 0; c < 256; c++) {
            uint8_t r = (c >> 5) & 0x07, g = (c >> 2) & 0x07, b = c & 0x03;
            _lutDim50[c] = (uint8_t)(((r >> 1) << 5) | ((g >> 1) << 2) | (b >> 1));
            // Gutters: ~25 % (blue has only 2 bits → >>1 so it is not killed)
            _lutDim25[c] = (uint8_t)(((r >> 2) << 5) | ((g >> 2) << 2) | (b >> 1));
            uint8_t rd = r ? r - 1 : 0, gd = g ? g - 1 : 0, bd = b ? b - 1 : 0;
            _lutDecay[c] = (uint8_t)((rd << 5) | (gd << 2) | bd);
        }
    }

    // Phosphor dot mask (ref photo — see header note): the image becomes a grid of
    // discrete dots — 3×3 cells, 2×2 bright dots, 1 px dark gutters in both
    // directions. Per-pixel LUT (preserves the underlying image, including
    // the phosphor trails applied before it).
    void applyDotMask(uint8_t* buf) {
        for (int y = 0; y < _h; y++) {
            uint8_t* row = buf + (size_t)y * _w;
            if (y % 3 == 2) {                          // horizontal gutter
                for (int x = 0; x < _w; x++) row[x] = _lutDim25[row[x]];
            } else {                                   // vertical gutters
                for (int x = 2; x < _w; x += 3) row[x] = _lutDim25[row[x]];
            }
        }
    }

    // Persistence: phosphor = max(decay(phosphor), scene); copied back to buf
    void applyPhosphor(uint8_t* buf) {
        const int n = _w * _h;
        for (int i = 0; i < n; i++) {
            uint8_t faded = _lutDecay[_phosphor[i]];
            uint8_t cur   = buf[i];
            uint8_t out   = (cur > faded) ? cur : faded;
            _phosphor[i] = out;
            buf[i]       = out;
        }
    }

};

} // namespace sce
