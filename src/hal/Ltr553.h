#pragma once
// =============================================================================
// Ltr553.h — StackChan-Companion (hal)
// =============================================================================
// CoreS3 ambient light + proximity sensor LTR-553ALS-WA (I2C 0x23, INTERNAL
// bus M5.In_I2C — pins 11/12, see I2cBus.h). Reference: CoreS3 K128 datasheet
// (docs.m5stack.com/en/StackChan). We only use the ALS (ambient light) part,
// to drive the automatic screen brightness.
//
// ALS: two 16-bit channels — CH0 (visible+IR), CH1 (IR). For a RELATIVE light
// level (brightness control) CH0 is enough; computing exact lux (CH1/CH0
// ratio, gain, integration time) is not needed here.
//
// CONTRACT — who calls this, and what breaks if someone else does:
//   - hal/Board.h OWNS the instance, probes it in begin() (`hasLtr553()`) and
//     refreshes the level into its OWN cache on a slow poll. Nothing else
//     talks to the part; a second reader would only add bus traffic to a bus
//     the Brain is already sharing.
//   - TWO consumers, both reading that cache and never this class directly:
//       `auto_brightness` — loop() smooths the cached level and POSTS a
//         brightness to the renderer, which applies it (A2.1: one single task
//         touches the display) and only on a noticeable change (A2.2:
//         setBrightness is a PMIC I2C transaction, never once per frame).
//       the `light` FIELD — published into the FieldStore, where the built-in
//         `dark_sleepy` rule watches it (behavior/RuleEngine.h). loop() is a
//         SOURCE of fields only: thresholds, debounce and hysteresis live in
//         the rule, not here and not in loop().
//   - FAILURE CONVENTION: visible() returns -1, NEVER 0 (see the comment on
//     it). A failed read that looked like darkness would let `dark_sleepy`
//     put the robot to sleep in broad daylight.
//   - Every register read takes `sce::i2cbus::Guard`, ONE transaction at a
//     time, so the Brain can slip its 100 Hz IMU reads in between (I2cBus.h /
//     A2.21).
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include "I2cBus.h"
// The part's register map, its calibration and the level curve live ONE place
// (A2.23): flight-radar drove the same sensor from its own copy of these
// numbers, kept in step by a comment. What differs between the two sides is the
// TRANSACTION POLICY, and only that — see the shared header.
#include "../../firmware/common/Ltr553.h"

namespace sce {

class Ltr553 {
public:
    // PART_ID (0x86) = 0x92 on the LTR-553. The sensor is nearly BLOCKED by
    // the K151 enclosure: a lit desk reads 0 counts at gain 1x, 2 counts at
    // gain 8x (measured 2026-07-20). → MAXIMUM sensitivity: gain 96x
    // (ALS_CONTR = 0b111<<2|0x01 = 0x1D) + 400 ms integration / 500 ms rate
    // (ALS_MEAS_RATE = 0b011'011 = 0x1B), i.e. about ×48 vs the original init.
    // The level01 scale is calibrated on that reduced range (ln 4096).
    bool begin() {
        _present = (rd(REG_PART_ID) == sce::ltr553::PART_ID);
        if (_present) {
            sce::i2cbus::Guard g;
            M5.In_I2C.writeRegister8(ADDR, REG_ALS_MEAS,  sce::ltr553::INIT_MEAS,  FREQ);
            M5.In_I2C.writeRegister8(ADDR, REG_ALS_CONTR, sce::ltr553::INIT_CONTR, FREQ);
        }
        return _present;
    }
    bool isPresent() const { return _present; }

    // Visible channel CH0 (16 bits) — proxy for the ambient light level. Reads
    // CH1 first, then CH0 (latch order recommended by the datasheet). Each
    // register is ONE guarded transaction (the CH0 latch lives inside the
    // sensor and is unaffected by traffic to another address) → the Brain can
    // slip its IMU reads between the bytes (honours "1 transaction per lock").
    // Returns -1 if a CH0 read FAILS: readRegister8 used to return 0 on
    // failure (value_or(0)), indistinguishable from genuine darkness → on a
    // faulty bus, dark_sleepy could put the robot to sleep in broad daylight
    // (review 07-21).
    int32_t visible() const {
        if (!_present) return -1;
        uint8_t d = 0, lo = 0, hi = 0;
        (void)tryRd(REG_CH1_LOW, d);
        (void)tryRd(REG_CH1_HIGH, d);
        if (!tryRd(REG_CH0_LOW, lo) || !tryRd(REG_CH0_HIGH, hi)) return -1;
        return (int32_t)(((uint16_t)hi << 8) | lo);
    }

    // Light level 0..1 from an ALREADY VALIDATED raw value (log scale
    // calibrated on the REAL range of the enclosure-blocked sensor, gain
    // 96x/400 ms: dark = 0, lit desk ~50-100 counts, clamped at 4096). The
    // caller (Board) keeps the last valid value on failure (visible() < 0).
    static float level01From(int32_t v) { return sce::ltr553::level01From(v); }

private:
    static constexpr uint8_t  ADDR          = sce::ltr553::ADDR;
    static constexpr uint32_t FREQ          = sce::ltr553::FREQ;
    static constexpr uint8_t  REG_ALS_CONTR = sce::ltr553::REG_ALS_CONTR;
    static constexpr uint8_t  REG_ALS_MEAS  = sce::ltr553::REG_ALS_MEAS;
    static constexpr uint8_t  REG_PART_ID   = sce::ltr553::REG_PART_ID;
    static constexpr uint8_t  REG_CH1_LOW   = sce::ltr553::REG_CH1_LOW;
    static constexpr uint8_t  REG_CH1_HIGH  = sce::ltr553::REG_CH1_HIGH;
    static constexpr uint8_t  REG_CH0_LOW   = sce::ltr553::REG_CH0_LOW;
    static constexpr uint8_t  REG_CH0_HIGH  = sce::ltr553::REG_CH0_HIGH;

    // Reading an 8-bit register = ONE guarded transaction (shared 11/12 bus).
    static uint8_t rd(uint8_t reg) {
        sce::i2cbus::Guard g;
        return M5.In_I2C.readRegister8(ADDR, reg, FREQ);
    }
    // DETECTABLE-FAILURE variant (readRegister8 returns 0 on failure — not
    // distinguishable from a genuine zero read): false = transaction failed.
    static bool tryRd(uint8_t reg, uint8_t& out) {
        sce::i2cbus::Guard g;
        return M5.In_I2C.readRegister(ADDR, reg, &out, 1, FREQ);
    }

    bool _present = false;
};

} // namespace sce
