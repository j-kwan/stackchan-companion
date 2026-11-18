#pragma once
// =============================================================================
// Ina226.h — StackChan-Companion (hal)
// =============================================================================
// INA226 power gauge on the StackChan K151 base (I2C 0x41, body bus Wire1 —
// shared pins 11/12, see I2cBus.h). Official M5Stack documentation:
// docs.m5stack.com/en/StackChan (dedicated battery monitor).
//
// The INA226 measures the BUS VOLTAGE (battery node, 1.25 mV/LSB) and the
// SHUNT VOLTAGE (2.5 µV/LSB, signed, ∝ current). The bus voltage is usable
// WITHOUT calibration (unlike the current register, which needs the shunt
// value — not documented here): we expose it as an accurate battery reading
// COMPLEMENTING the AXP2101 estimate, plus the raw shunt voltage (sign and
// relative magnitude of the charge/discharge current). The part's default
// mode is continuous, so no configuration is required.
//
// CONTRACT — who calls this, and what breaks if someone else does:
//   - hal/Board.h OWNS the instance (constructed on Wire1), probes it once in
//     begin() (`hasIna226()`) and refreshes bus + shunt into its OWN cache on
//     a slow poll. Nothing else in the firmware talks to the part.
//   - Readers take that cache, never the sensor: GET /api/sensors (`ina_v`,
//     `ina_shunt_mv`) and GET /api/status (`inaV`). An AsyncTCP callback must
//     not touch I2C (A2.6: only post() and Tuning writes) — the cache is what
//     makes those endpoints legal, not a convenience.
//   - Every register read takes `sce::i2cbus::Guard`: pins 11/12 are shared
//     with the IMU the Brain reads at 100 Hz (I2cBus.h / A2.21). One
//     transaction per lock, never a lock held across a delay.
//   - FAILURE CONVENTION: busVoltage() returns -1, NEVER 0 — a caller has to
//     be able to tell "no reading" from "flat battery". percentFromVoltage()
//     is INDICATIVE only (the voltage sags under load); the AXP2101 estimate
//     stays the authoritative battery level, this part only complements it.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include "I2cBus.h"

namespace sce {

class Ina226 {
public:
    explicit Ina226(TwoWire& wire = Wire1) : _wire(wire) {}

    // Detection through the Texas Instruments manufacturer ID (0x5449 = 'TI').
    bool begin() {
        _present = (read16(REG_MANUF_ID) == 0x5449);
        return _present;
    }
    bool isPresent() const { return _present; }

    // Bus voltage (≈ battery) in volts, -1 when absent OR on a failed read
    // (a raw value of 0 is never a real battery voltage).
    float busVoltage() const {
        if (!_present) return -1.0f;
        uint16_t raw = read16(REG_BUS_V);                // 1.25 mV/LSB
        return raw ? raw * 1.25e-3f : -1.0f;
    }
    // Shunt voltage in mV (signed) — ∝ current (positive = discharge, given
    // this wiring).
    float shuntMilliV() const {
        if (!_present) return 0.0f;
        return (int16_t)read16(REG_SHUNT_V) * 2.5e-3f;   // 2.5 µV/LSB → mV
    }

    // Rough charge estimate (%) from the bus voltage, 1S LiPo curve
    // (3.30 V ≈ 0 %, 4.20 V ≈ 100 %). -1 when absent. Indicative only (the
    // voltage sags under load) — the AXP2101 stays the primary source.
    int8_t percentFromVoltage() const {
        float v = busVoltage();
        if (v < 0.0f) return -1;
        float p = (v - 3.30f) / (4.20f - 3.30f) * 100.0f;
        return (int8_t)(p < 0 ? 0 : (p > 100 ? 100 : p));
    }

private:
    static constexpr uint8_t ADDR         = 0x41;
    static constexpr uint8_t REG_SHUNT_V  = 0x01;
    static constexpr uint8_t REG_BUS_V    = 0x02;
    static constexpr uint8_t REG_MANUF_ID = 0xFE;

    uint16_t read16(uint8_t reg) const {
        sce::i2cbus::Guard g;   // bus 11/12 shared with the IMU
        _wire.beginTransmission(ADDR);
        _wire.write(reg);
        if (_wire.endTransmission(false) != 0) return 0;
        if (_wire.requestFrom(ADDR, (uint8_t)2) != 2) return 0;
        uint16_t hi = _wire.read();
        uint16_t lo = _wire.read();
        return (uint16_t)((hi << 8) | lo);
    }

    TwoWire& _wire;
    bool     _present = false;
};

} // namespace sce
