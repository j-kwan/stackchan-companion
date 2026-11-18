#pragma once
// =============================================================================
// Py32Expander.h — StackChan-Companion (hal)
// =============================================================================
// Driver for the StackChan K151 PY32 IO expander (I2C 0x6F, body bus Wire1).
// Protocol source: StackChan-main/firmware/main/hal/drivers/PY32IOExpander_Class/
//
// Roles:
//   - VM_EN (PY32 GPIO 0): power rail of the SCS0009 servos. MUST be turned
//     on BEFORE servo.begin() — otherwise the servos get no current.
//     Sequence: direction → pull-up → output HIGH.
//   - WS2812C LEDs ×12 (data line = PY32 GPIO 13): the REAL protocol, read out of the vendor
//     firmware (PY32IOExpander_Class.cpp, found via graphify refs — P0):
//       REG_LED_CFG (0x24)  : bits 0-5 = LED count, bit 6 = REFRESH
//       REG_LED_RAM (0x30+) : 2 bytes/LED, little-endian RGB565 color
//     Sequence: setLedCount(12) → write the colors → refreshLeds().
//     (An earlier `[0xAA, r, g, b, lum]` protocol was guesswork and plain
//     wrong — it ACKed on I2C without lighting anything up.)
//
// Timing: the PY32 boots slowly (~200 ms). detect() retries the version read
// for up to 1200 ms before declaring the chip absent.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include "I2cBus.h"   // lock for the 11/12 bus shared with the IMU (In_I2C)
// The PAYLOAD — RGB565 rounding and the little-endian order of the twelve
// entries — is shared with the guest bins that light the same LEDs. Only the
// arithmetic is shared: the transaction below stays here, under the bus lock,
// because only this firmware has another task on those pins (A2.23's split).
#include "../../firmware/common/Py32Leds.h"

namespace sce {

class Py32Expander {
public:
    // Registers (named)
    static constexpr uint8_t I2C_ADDR    = 0x6F;
    static constexpr uint8_t REG_VERSION = 0x02;  // 0x00/0xFF = not detected
    static constexpr uint8_t REG_DIR_L   = 0x03;  // GPIO 0-7 direction (1 = output)
    static constexpr uint8_t REG_OUT_L   = 0x05;  // GPIO 0-7 output value
    static constexpr uint8_t REG_PU_L    = 0x09;  // GPIO 0-7 pull-up
    static constexpr uint8_t VM_EN_BIT   = 0x01;  // GPIO 0 = servo power rail
    // High-byte GPIO registers (GPIO 8-15) — the LED data line is PY32 GPIO 13
    // (bit 5 of the _H registers). Init sequence taken from hal_io_expander.cpp
    // in the vendor firmware: output direction + pull-up (PD off) + push-pull
    // drive, THEN setLedCount, THEN 200 ms.
    static constexpr uint8_t REG_DIR_H   = 0x04;  // GPIO 8-15 direction
    static constexpr uint8_t REG_PU_H    = 0x0A;  // GPIO 8-15 pull-up
    static constexpr uint8_t REG_PD_H    = 0x0C;  // GPIO 8-15 pull-down
    static constexpr uint8_t REG_DRV_H   = 0x14;  // drive mode (1 = open-drain)
    static constexpr uint8_t LED_PIN_BIT = 0x20;  // GPIO 13 → bit 5 of the _H regs
    static constexpr uint8_t REG_LED_CFG = 0x24;  // bits 0-5: count, bit 6: refresh
    static constexpr uint8_t REG_LED_RAM = 0x30;  // 2 bytes/LED, RGB565 LE
    static constexpr uint8_t LED_REFRESH = 0x40;  // bit 6 of REG_LED_CFG
    static constexpr uint8_t LED_COUNT   = 12;    // K151 bars: WS2812C ×12

    explicit Py32Expander(TwoWire& wire) : _wire(wire) {}

    // ------------------------------------------------------------------
    // Detection with retry — the PY32 boots in ~200 ms, we allow 1200 ms.
    // Returns true once a valid version is read (≠ 0x00 and ≠ 0xFF).
    // ------------------------------------------------------------------
    bool detect(uint32_t timeoutMs = 1200) {
        uint32_t start = millis();
        do {
            uint8_t ver = readReg(REG_VERSION);
            if (ver != 0x00 && ver != 0xFF) {
                _version  = ver;
                _detected = true;
                return true;
            }
            delay(50);
        } while (millis() - start < timeoutMs);
        _detected = false;
        return false;
    }

    // ------------------------------------------------------------------
    // Turn VM_EN on (servo power). Must be called before servo.begin().
    // The closing delay(300) lets the power rail settle (measured value).
    // ------------------------------------------------------------------
    bool enableServoPower() {
        if (!_detected) return false;
        setBit(REG_DIR_L, VM_EN_BIT);  // GPIO 0 as output
        setBit(REG_PU_L,  VM_EN_BIT);  // pull-up
        setBit(REG_OUT_L, VM_EN_BIT);  // HIGH → VM_EN
        delay(300);
        return true;
    }

    // ------------------------------------------------------------------
    // Turn VM_EN OFF — the servo power rail, cut.
    //
    // THE ONLY RELEASE A REBOOT CANNOT UNDO. `EnableTorque(id, 0)` is a
    // register inside the SCS0009, and the SCS0009 power up with their torque
    // ENABLED: hand the machine to a guest bin and the reboot re-asserts VM_EN,
    // the servos come back energised, and the release we carefully sent and
    // acknowledged is erased by the power-up that follows (measured 08-02 —
    // limp under the companion, locked again under the guest).
    //
    // The PY32 keeps its GPIO state across OUR reset, which is exactly what
    // makes this stick. Whoever wants the servos back calls enableServoPower()
    // — the companion does it at boot, and a guest that drives the neck does it
    // in its own init, so nothing is stranded.
    // ------------------------------------------------------------------
    bool disableServoPower() {
        if (!_detected) return false;
        clearBit(REG_OUT_L, VM_EN_BIT);   // LOW → rail cut
        return true;
    }

    // ------------------------------------------------------------------
    // LEDs — vendor firmware protocol (see the file header).
    // setAllLeds: same color on all 12 LEDs. brightness (0-255) is a factor
    // applied to the components (the PY32 has no dedicated brightness reg).
    // ------------------------------------------------------------------
    bool setAllLeds(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 255) {
        uint16_t c = sce::py32::scale565(r, g, b, brightness);
        return writeLedRam(c, c);
    }

    // ------------------------------------------------------------------
    // The 12 LEDs, EACH ITS OWN COLOUR. The colour RAM always carried twelve
    // entries — only the API pretended otherwise, offering all-twelve-alike
    // and two bars. A guest bin echoing a fluid across the ring needs the
    // twelve, and there was no reason for it to reimplement the burst.
    // ------------------------------------------------------------------
    bool setLedsRaw(const uint16_t colors[sce::py32::LED_COUNT]) {
        if (!_detected) return false;
        if (!_ledInit) initLeds();             // GPIO 13 + count, once only
        uint8_t payload[sce::py32::PAYLOAD_LEN];
        sce::py32::buildPayload(colors, payload);
        {
            sce::i2cbus::Guard g;              // 11/12 bus, shared with the IMU
            _wire.beginTransmission(I2C_ADDR);
            _wire.write(REG_LED_RAM);
            _wire.write(payload, sizeof(payload));
            if (_wire.endTransmission() != 0) return false;
        }
        return refreshLeds();
    }

    // The 12 LEDs as TWO INDEPENDENT bars: LEDs 0-5 = left bar, 6-11 = right
    // bar (viewer-centric; the caller can swap them if the wiring differs).
    // Same color, per-bar brightness (0-255) — used to track each bar to the
    // height of the matching eye (closed = 0).
    bool setLedsBars(uint8_t r, uint8_t g, uint8_t b,
                     uint8_t briLeft, uint8_t briRight) {
        return writeLedRam(sce::py32::scale565(r, g, b, briLeft),
                           sce::py32::scale565(r, g, b, briRight));
    }

    bool ledsOff() { return setAllLeds(0, 0, 0); }

    // ------------------------------------------------------------------
    // LED init — exact sequence from the vendor firmware (hal_io_expander):
    // GPIO 13 as push-pull output with pull-up, count=12, 200 ms settling.
    // Called lazily on the first setAllLeds (or explicitly at boot).
    // ------------------------------------------------------------------
    void initLeds() {
        if (!_detected || _ledInit) return;
        setBit  (REG_DIR_H, LED_PIN_BIT);            // GPIO 13 = output
        clearBit(REG_PD_H,  LED_PIN_BIT);            // pull-down off...
        setBit  (REG_PU_H,  LED_PIN_BIT);            // ...pull-up on
        clearBit(REG_DRV_H, LED_PIN_BIT);            // push-pull (not open-drain)
        writeReg(REG_LED_CFG, LED_COUNT & 0x3F);     // 12 LEDs
        delay(200);                                  // settling (vendor value)
        _ledInit = true;
    }

    // Push the color RAM out to the WS2812 chain (bit 6 of LED_CFG)
    bool refreshLeds() {
        uint8_t v = readReg(REG_LED_CFG);
        return writeReg(REG_LED_CFG, v | LED_REFRESH);
    }

    bool    isDetected() const { return _detected; }
    uint8_t version()    const { return _version; }

private:
    TwoWire& _wire;
    bool     _detected = false;
    bool     _ledInit  = false;
    uint8_t  _version  = 0;

    // Two bars → the twelve entries, then the ONE burst of setLedsRaw. The
    // RGB565 rounding that used to live here is now sce::py32::scale565, shared
    // with the guest bins that light these same LEDs: the tint-as-it-dims bug
    // (blue truncated to zero before green, cyan eyes turning GREEN) is fixed
    // in one place, or it comes back in the other.
    bool writeLedRam(uint16_t cLeft, uint16_t cRight) {
        uint16_t colors[sce::py32::LED_COUNT];
        sce::py32::buildBars(cLeft, cRight, colors);
        return setLedsRaw(colors);
    }

    uint8_t readReg(uint8_t reg) {
        sce::i2cbus::Guard g;   // repeated-start = ONE transaction under lock
        _wire.beginTransmission(I2C_ADDR);
        _wire.write(reg);
        _wire.endTransmission(false);          // repeated start
        _wire.requestFrom(I2C_ADDR, (uint8_t)1);
        return _wire.available() ? _wire.read() : 0xFF;
    }

    bool writeReg(uint8_t reg, uint8_t val) {
        sce::i2cbus::Guard g;   // 11/12 bus shared with the IMU (Brain, core 1)
        _wire.beginTransmission(I2C_ADDR);
        _wire.write(reg);
        _wire.write(val);
        return _wire.endTransmission() == 0;
    }

    // Read-modify-write of one bit (other GPIOs in the register are preserved)
    void setBit(uint8_t reg, uint8_t mask)   { writeReg(reg, readReg(reg) |  mask); }
    void clearBit(uint8_t reg, uint8_t mask) { writeReg(reg, readReg(reg) & ~mask); }
};

} // namespace sce
