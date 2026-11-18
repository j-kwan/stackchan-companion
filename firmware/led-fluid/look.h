#pragma once
// =============================================================================
// look.h — what a dot looks like, and WHEN it is worth repainting
// =============================================================================
// Two jobs, and the second is the load-bearing one. The frame budget is only
// met because a cell whose appearance has not changed is not drawn again, and
// "has not changed" has to be decided on something coarser than a byte or
// noise alone repaints the whole grid. That is the key: density and speed
// quantised to 5 bits each, packed into one 16-bit value the renderer stores
// per cell and compares.
//
// THE MAPPING, AND WHY IT RUNS THIS WAY. The picker sets the hue and a
// saturation CEILING; the fluid moves under it. Density RAISES saturation
// towards that ceiling and speed raises value, so a deep still pool reads as
// strong dark colour and fast thin spray reads as pale and bright — the
// white-hot highlight the eye already expects from a splash. The other
// direction was tried on paper and washes out the body of the fluid, which is
// the one part the eye actually follows.
//
// PURE: no Arduino. Tested natively (test/test_fluidlook).
// =============================================================================

#include <stdint.h>

namespace sce {
namespace look {

// 5 bits each: 32 density steps, 32 speed steps. Below that the eye sees
// banding on a slow drift; above it, noise starts costing repaints.
inline uint16_t dotKey(uint8_t dens, uint8_t spd) {
    const uint8_t d = (uint8_t)(dens >> 3);
    if (d == 0) return 0;              // empty is ONE key, whatever the speed
    return (uint16_t)((d << 5) | (spd >> 3));
}

// key -> RGB565. hue 0-359, satMax 0-100 (the ceiling from the picker).
inline uint16_t keyToRgb565(uint16_t key, uint16_t hue, uint8_t satMax) {
    const uint8_t d = (uint8_t)(key >> 5);        // 0-31 density
    if (d == 0) return 0x0000;                    // black, what the wipe leaves
    const uint8_t s = (uint8_t)(key & 0x1F);      // 0-31 speed

    // Saturation: the ceiling, approached as the fluid piles up. Value: a
    // floor plus what the speed adds, so a still pool is still visible.
    const int ceil255 = (int)satMax * 255 / 100;
    const int sat = ceil255 * (60 + (int)d * 195 / 31) / 255;
    const int val = 110 + (int)s * 145 / 31;

    // HSV -> RGB, integer, sextant by sextant. ONE conversion in the bin.
    const int h6 = ((int)hue % 360) * 6;
    const int sect = h6 / 360;
    const int f = h6 % 360;
    const int p = val * (255 - sat) / 255;
    const int q = val * (255 - sat * f / 360) / 255;
    const int t = val * (255 - sat * (360 - f) / 360) / 255;
    int r, g, b;
    switch (sect) {
        case 0:  r = val; g = t;   b = p;   break;
        case 1:  r = q;   g = val; b = p;   break;
        case 2:  r = p;   g = val; b = t;   break;
        case 3:  r = p;   g = q;   b = val; break;
        case 4:  r = t;   g = p;   b = val; break;
        default: r = val; g = p;   b = q;   break;
    }
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// The presets set the PHYSICS only — never the hue. A preset that also wrote
// the colour would undo the panel next door, which is a control fighting
// another control rather than a shortcut.
struct Preset {
    const char* en;
    const char* fr;
    float   viscosity;
    float   gravity;
    float   bounce;
    uint8_t trail;
};

inline const Preset* presets() {
    static const Preset P[] = {
        { "Water",      "Eau",        18,  100, 35, 55 },
        { "Honey",      "Miel",       88,  100,  5, 75 },
        { "Mercury",    "Mercure",    35,  160, 70, 30 },
        { "Lava",       "Lave",       70,   70, 10, 90 },
        { "Weightless", "Apesanteur", 12,    0, 60, 70 },
    };
    return P;
}
inline int presetCount() { return 5; }

} // namespace look
} // namespace sce
