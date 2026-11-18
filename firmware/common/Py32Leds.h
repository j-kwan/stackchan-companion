#pragma once
// =============================================================================
// Py32Leds.h — the PY32 WS2812 payload, and NOT the transaction that sends it
// =============================================================================
// WHY THE SPLIT. The companion drives the K151's twelve LEDs through
// hal/Py32Expander.h, under the 11/12 bus lock, because its Brain reads the IMU
// from another task. A guest bin whose every I2C transaction already lives in
// loop() needs no lock at all. What both need IDENTICALLY is the arithmetic:
// the RGB888 to RGB565 rounding, and the little-endian order of the twelve
// entries of the colour RAM.
//
// So the computation is shared and the Wire transaction is not — the same split
// rule A2.23 draws between a YAML decoder (shared, one implementation) and the
// bounded read around it (local, and never divergent). Duplicating the rounding
// instead would repeat a bug that has already been paid for once: truncating
// drops blue before green, and the cyan eyes turned GREEN as they dimmed.
//
// PURE: no Arduino, no Wire, no M5 — only <stdint.h>.
// =============================================================================

#include <stdint.h>

namespace sce {
namespace py32 {

// The K151 carries twelve WS2812C on PY32 GPIO 13. The count is part of the
// payload's shape, not a preference: the colour RAM is written as one burst of
// LED_COUNT entries and the chip refreshes exactly that many.
inline constexpr int LED_COUNT   = 12;
inline constexpr int PAYLOAD_LEN = LED_COUNT * 2;

// RGB888 × brightness → RGB565. ROUNDS instead of truncating: blue and red have
// five bits, green six, so truncation kills blue FIRST and tints everything as
// it dims. Rounding — half an LSB added before the shift — makes the three
// channels fade together.
inline uint16_t scale565(uint8_t r, uint8_t g, uint8_t b, uint8_t bri) {
    const uint16_t rs = (uint16_t)r * bri / 255;
    const uint16_t gs = (uint16_t)g * bri / 255;
    const uint16_t bs = (uint16_t)b * bri / 255;
    const uint8_t r5 = (uint8_t)((rs * 31 + 127) / 255);
    const uint8_t g6 = (uint8_t)((gs * 63 + 127) / 255);
    const uint8_t b5 = (uint8_t)((bs * 31 + 127) / 255);
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

// The colour RAM as the vendor firmware wants it: twelve RGB565, LOW BYTE
// FIRST. The caller writes `out` to REG_LED_RAM (0x30) and then sets the
// refresh bit — this function knows the bytes, not the bus.
inline void buildPayload(const uint16_t colors[LED_COUNT], uint8_t out[PAYLOAD_LEN]) {
    for (int i = 0; i < LED_COUNT; i++) {
        out[i * 2]     = (uint8_t)(colors[i] & 0xFF);
        out[i * 2 + 1] = (uint8_t)((colors[i] >> 8) & 0xFF);
    }
}

// The two-bar case the companion's eyes use: LEDs 0-5 left, 6-11 right
// (viewer-centric — a caller whose wiring differs swaps the arguments).
inline void buildBars(uint16_t left, uint16_t right, uint16_t out[LED_COUNT]) {
    for (int i = 0; i < LED_COUNT; i++)
        out[i] = (i < LED_COUNT / 2) ? left : right;
}

} // namespace py32
} // namespace sce
