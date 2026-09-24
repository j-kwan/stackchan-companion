#pragma once
// =============================================================================
// Ltr553.h — StackChan-Companion (shared: companion + guest bins)
// =============================================================================
// WHAT IS SHARED HERE IS THE PART, NOT THE POLICY.
//
// The LTR-553ALS-WA (I2C 0x23, CoreS3 internal bus) was being driven from two
// places with two copies of the same magic numbers: `src/hal/Ltr553.h` on the
// companion side and an eighteen-line block inside flight-radar. The radar's
// own comment said "scale calibrated on the reduced range measured on the
// companion side (hal/Ltr553.h)" — a cross-reference kept by hand, which is
// the definition of a twin waiting to diverge (A2.23). What follows makes that
// reference structural.
//
// THE CALIBRATION IS THE PART OF IT THAT MUST NOT DRIFT. The sensor is nearly
// OCCLUDED by the K151 enclosure: a lit desk reads 0 counts at gain 1x and 2
// counts at gain 8x (measured 2026-07-20). Hence maximum sensitivity — gain 96x
// (ALS_CONTR = 0b111<<2 | 0x01 = 0x1D) with 400 ms integration on a 500 ms rate
// (ALS_MEAS_RATE = 0b011'011 = 0x1B), about x48 versus the stock init — and a
// level curve normalised on ln(4096), which is the range that init actually
// produces. Change one of those three and the other two are wrong.
//
// WHAT IS DELIBERATELY *NOT* SHARED is the transaction policy, because it
// genuinely differs and pretending otherwise would be worse than the copy:
//   - the COMPANION reads one register per `sce::i2cbus::Guard`, so the Brain
//     can slip its 100 Hz IMU reads between the bytes (A2.21). The CH0 latch
//     lives inside the sensor and is unaffected by traffic to another address,
//     so splitting is safe there.
//   - a GUEST BIN has no Brain and no bus lock; it reads the four data bytes in
//     one burst, which is simpler and strictly fewer transactions.
// Each side therefore keeps its own reader and takes its numbers from here.
//
// FAILURE CONVENTION, and it is shared because getting it wrong is what nearly
// put the robot to sleep in broad daylight (review 07-21): a failed read
// returns -1 and NEVER 0. Zero is a legal reading — it means darkness.
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>

namespace sce {
namespace ltr553 {

static constexpr uint8_t  ADDR          = 0x23;
static constexpr uint32_t FREQ          = 100000;
static constexpr uint8_t  REG_ALS_CONTR = 0x80;
static constexpr uint8_t  REG_ALS_MEAS  = 0x85;   // integration + rate
static constexpr uint8_t  REG_PART_ID   = 0x86;
static constexpr uint8_t  REG_CH1_LOW   = 0x88;   // the burst starts here:
static constexpr uint8_t  REG_CH1_HIGH  = 0x89;   // CH1 then CH0, and reading
static constexpr uint8_t  REG_CH0_LOW   = 0x8A;   // CH1 first is the latch
static constexpr uint8_t  REG_CH0_HIGH  = 0x8B;   // order the datasheet asks for
static constexpr uint8_t  PART_ID       = 0x92;
static constexpr uint8_t  INIT_MEAS     = 0x1B;   // 400 ms / 500 ms
static constexpr uint8_t  INIT_CONTR    = 0x1D;   // gain 96x, active

// Light level 0..1 from an ALREADY VALIDATED raw count (log scale calibrated on
// the real range of the enclosure-blocked sensor: dark = 0, lit desk ~50-100
// counts, clamped at 4096). Callers keep their last valid value on a failure.
inline float level01From(int32_t v) {
    if (v <= 0) return 0.0f;
    constexpr float invLogMax = 1.0f / 8.3178f;         // 1 / ln(4096)
    const float l = logf((float)v + 1.0f) * invLogMax;
    return l < 0 ? 0 : (l > 1 ? 1 : l);
}

// Screen brightness from a raw count — the same curve, expressed once. This was
// a THIRD copy of the calibration, written out longhand inside flight-radar's
// loop as `60 + 195 * ln(1+v) / ln(4097)`.
//
// THE FLOOR IS NOT DECORATION. The sensor is nearly blind behind the K151
// shell, so a normally lit room reads low enough that a floorless curve turned
// the screen BLACK — the setting looked broken and the only recourse was to
// find the toggle on a screen you could no longer read.
inline uint8_t brightnessFrom(int32_t v, uint8_t floorB = 60) {
    const int span = 255 - (int)floorB;
    int b = (int)floorB + (int)(span * level01From(v));
    if (b < (int)floorB) b = floorB;
    if (b > 255) b = 255;
    return (uint8_t)b;
}

// ---- the GUEST-side reader -------------------------------------------------
// One burst for the four data bytes. No bus lock: a guest bin owns the board
// alone — there is no Brain reading the IMU at 100 Hz to make room for.
struct Lite {
    bool begin() {
        uint8_t id = 0;
        _present = M5.In_I2C.readRegister(ADDR, REG_PART_ID, &id, 1, FREQ) &&
                   id == PART_ID;
        if (_present) {
            M5.In_I2C.writeRegister8(ADDR, REG_ALS_MEAS,  INIT_MEAS,  FREQ);
            M5.In_I2C.writeRegister8(ADDR, REG_ALS_CONTR, INIT_CONTR, FREQ);
        }
        return _present;
    }
    bool isPresent() const { return _present; }

    // CH0 (visible + IR), 16 bits. -1 on a failed read — never 0.
    int32_t visible() const {
        if (!_present) return -1;
        uint8_t b[4];
        if (!M5.In_I2C.readRegister(ADDR, REG_CH1_LOW, b, 4, FREQ)) return -1;
        return (int32_t)b[2] | ((int32_t)b[3] << 8);    // CH0 low, CH0 high
    }

private:
    bool _present = false;
};

} // namespace ltr553
} // namespace sce
