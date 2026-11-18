#pragma once
// =============================================================================
// Si12T.h — StackChan-Companion (hal) — driver validated on hardware
// =============================================================================
// Arduino driver for the 3-zone capacitive touch panel on the StackChan head.
// Direct port of the official StackChan firmware (hal/drivers/Si12T/,
// Apache-2.0, M5Stack/meganetaaan) to Wire.h.
//
// Hardware:
//   Part: Si12T (capacitive touch, 3 zones)
//   I2C address: 0x68
//   Bus: Wire1 (StackChan body bus, G11=SCL / G12=SDA)
//   Speed: 100kHz
//
// Registers (faithful port of the source firmware):
//   0x02..0x06 : SENSITIVITY1..5 (5 sensitivity channels)
//   0x08 : CTRL1 — Auto Mode, FTC=01, Interrupt(Mid+High), Response 4
//   0x09 : CTRL2 — S/W Reset, Sleep Mode
//   0x0A..0x0F : REF_RST, CH_HOLD, CAL_HOLD (per-channel enable)
//   0x10 : OUTPUT1 — touch result (3 zones × 2 bits)
//
// OUTPUT1 layout:
//   bits [1:0] = zone 0 (rear)
//   bits [3:2] = zone 1 (middle)
//   bits [5:4] = zone 2 (front)
//   Values: 0=NONE, 1=LOW, 2=MID, 3=HIGH
//
// Centroid position (port of hal_head_touch.cpp):
//   pos = (-100 × ch0 + 0 × ch1 + 100 × ch2) / (ch0+ch1+ch2)
//   → -100 = hand at the rear, 0 = middle, +100 = front
//
// Gestures detected:
//   PRESS         : first contact (max_intensity >= touch_threshold)
//   RELEASE       : end of contact
//   SWIPE_FORWARD : position delta > swipe_threshold (rear → front)
//   SWIPE_BACKWARD: position delta < -swipe_threshold (front → rear)
//
// Usage:
//   Si12T touch;
//   touch.begin(Wire1);          // Wire1 = body bus G11/G12
//   touch.setSensitivity(3);     // level 0..7, default 3 (LOW type)
//
//   // Callbacks
//   touch.onPress    = []() { /* ... */ };
//   touch.onRelease  = []() { /* ... */ };
//   touch.onSwipeForward  = []() { /* rear -> front */ };
//   touch.onSwipeBackward = []() { /* front -> rear */ };
//
//   // in loop()
//   touch.update();
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <functional>
#include "I2cBus.h"   // lock for the 11/12 bus shared with the IMU (In_I2C)

namespace sce {

// ------------------------------------------------------------------
// Registers (port of the firmware's Si12T.h)
// ------------------------------------------------------------------
namespace Si12TReg {
    static constexpr uint8_t SENSITIVITY1 = 0x02;
    static constexpr uint8_t SENSITIVITY2 = 0x03;
    static constexpr uint8_t SENSITIVITY3 = 0x04;
    static constexpr uint8_t SENSITIVITY4 = 0x05;
    static constexpr uint8_t SENSITIVITY5 = 0x06;
    // Note: register 0x07 (SENSITIVITY6) exists in the datasheet, but the
    // official firmware only writes SENSITIVITY1..5 — reproduced as is.
    static constexpr uint8_t CTRL1        = 0x08;
    static constexpr uint8_t CTRL2        = 0x09;
    static constexpr uint8_t REF_RST1     = 0x0A;
    static constexpr uint8_t REF_RST2     = 0x0B;
    static constexpr uint8_t CH_HOLD1     = 0x0C;
    static constexpr uint8_t CH_HOLD2     = 0x0D;
    static constexpr uint8_t CAL_HOLD1    = 0x0E;
    static constexpr uint8_t CAL_HOLD2    = 0x0F;
    static constexpr uint8_t OUTPUT1      = 0x10;
}

// ------------------------------------------------------------------
// Gesture types (port of the firmware's HeadPetGesture)
// ------------------------------------------------------------------
enum class HeadGesture {
    None,
    Press,
    Release,
    SwipeForward,   // rear → front (hand sliding toward the robot's front)
    SwipeBackward,  // front → rear
};

// ------------------------------------------------------------------
// Touch data (port of the firmware's TouchData struct)
// ------------------------------------------------------------------
struct Si12TData {
    uint8_t intensity[3] = {0, 0, 0}; // zone 0=rear, 1=middle, 2=front

    // Centroid position (-100..+100), port of get_position()
    int16_t getPosition() const {
        uint16_t total = intensity[0] + intensity[1] + intensity[2];
        if (total == 0) return 0;
        int32_t weighted = (int32_t)intensity[0] * (-100)
                         + (int32_t)intensity[1] * 0
                         + (int32_t)intensity[2] * 100;
        return static_cast<int16_t>(weighted / total);
    }

    uint8_t getMaxIntensity() const {
        return max(intensity[0], max(intensity[1], intensity[2]));
    }

    bool isTouched(uint8_t threshold = 1) const {
        return getMaxIntensity() >= threshold;
    }
};

// ------------------------------------------------------------------
// Si12T — full driver
// ------------------------------------------------------------------
class Si12T {
public:
    // Thresholds (port of the firmware's TouchConfig)
    uint8_t touchThreshold = 1;   // min intensity to consider it a contact
    int16_t swipeThreshold = 40;  // position delta that qualifies as a swipe

    // Gesture callbacks
    std::function<void()>           onPress;
    std::function<void()>           onRelease;
    std::function<void()>           onSwipeForward;   // rear → front
    std::function<void()>           onSwipeBackward;  // front → rear

    Si12T() : _wire(nullptr), _initialized(false),
              _state(State::IDLE), _initialPos(0),
              _lastUpdateMs(0), _updateIntervalMs(50) {}

    // ------------------------------------------------------------------
    // Initialization — port of si12t_init() + si12t_setup()
    // wire: Wire1 for the StackChan body bus (G11/G12)
    // sensitivityLevel: 0..7, default 3 (TYPE_LOW, LEVEL_3 = 0x33)
    // ------------------------------------------------------------------
    bool begin(TwoWire& wire = Wire1, uint8_t sensitivityLevel = 3) {
        _wire = &wire;

        // Check that the sensor answers (probe taken under the bus lock: this
        // runs after the Brain has started, and it reads the IMU on the same
        // pins).
        {
            sce::i2cbus::Guard g;
            _wire->beginTransmission(SI12T_ADDRESS);
            if (_wire->endTransmission() != 0) return false;  // absent / bus down
        }

        // Port of si12t_enable_channel() — enable every channel
        writeReg(Si12TReg::REF_RST1,  0x00);
        writeReg(Si12TReg::REF_RST2,  0x00);
        writeReg(Si12TReg::CH_HOLD1,  0x00);
        writeReg(Si12TReg::CH_HOLD2,  0x00);
        writeReg(Si12TReg::CAL_HOLD1, 0x00);
        writeReg(Si12TReg::CAL_HOLD2, 0x00);

        // Port of si12t_set_ctrl2() — S/W Reset, then Sleep Disable
        writeReg(Si12TReg::CTRL2, 0x0F); // reset
        delay(10);
        writeReg(Si12TReg::CTRL2, 0x07); // sleep disable

        // Port of si12t_set_ctrl1() — Auto Mode, FTC=01, Interrupt(Mid+High)
        writeReg(Si12TReg::CTRL1, 0x22);

        // Port of si12t_set_sensitivity() with TYPE_LOW, level 0..7
        // TYPE_LOW values: 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
        setSensitivity(sensitivityLevel);

        _initialized = true;
        return true;
    }

    // ------------------------------------------------------------------
    // Set the sensitivity — 0 (lowest) to 7 (highest)
    // Type LOW (value 0xNN where N=level) — same as the firmware
    // ------------------------------------------------------------------
    void setSensitivity(uint8_t level) {
        if (level > 7) level = 7;
        uint8_t val = (level << 4) | level; // e.g. level=3 → 0x33
        writeReg(Si12TReg::SENSITIVITY1, val);
        writeReg(Si12TReg::SENSITIVITY2, val);
        writeReg(Si12TReg::SENSITIVITY3, val);
        writeReg(Si12TReg::SENSITIVITY4, val);
        writeReg(Si12TReg::SENSITIVITY5, val);
    }

    // Configure the polling interval (default 50ms = 20Hz)
    void setUpdateInterval(unsigned long ms) { _updateIntervalMs = ms; }

    // ------------------------------------------------------------------
    // update() — call this from loop()
    // Port of the firmware's _head_touch_update_task()
    // ------------------------------------------------------------------
    HeadGesture update() {
        if (!_initialized || !_wire) return HeadGesture::None;
        if (millis() - _lastUpdateMs < _updateIntervalMs) return HeadGesture::None;
        _lastUpdateMs = millis();

        // Read OUTPUT1 (port of si12t_read_touch_result + si12t_parse_touch_result_to)
        uint8_t raw = readReg(Si12TReg::OUTPUT1);
        Si12TData data;
        for (int i = 0; i < 3; i++) {
            data.intensity[i] = (raw >> (i * 2)) & 0x03;
        }

        // State machine identical to the firmware (port of GestureRecognizer::update)
        HeadGesture gesture = HeadGesture::None;

        switch (_state) {
            case State::IDLE:
                if (data.isTouched(touchThreshold)) {
                    _state      = State::TOUCHED;
                    _initialPos = data.getPosition();
                    gesture     = HeadGesture::Press;
                }
                break;

            case State::TOUCHED:
                if (!data.isTouched(touchThreshold)) {
                    _state  = State::IDLE;
                    gesture = HeadGesture::Release;
                } else {
                    int16_t delta = data.getPosition() - _initialPos;
                    if (delta > swipeThreshold) {
                        _state  = State::SWIPING;
                        gesture = HeadGesture::SwipeForward;
                    } else if (delta < -swipeThreshold) {
                        _state  = State::SWIPING;
                        gesture = HeadGesture::SwipeBackward;
                    }
                }
                break;

            case State::SWIPING:
                if (!data.isTouched(touchThreshold)) {
                    _state  = State::IDLE;
                    gesture = HeadGesture::Release;
                }
                break;
        }

        // Fire the callbacks
        if (gesture != HeadGesture::None) {
            dispatchGesture(gesture);
        }

        return gesture;
    }

    // ------------------------------------------------------------------
    // Raw data access (for debugging / calibration)
    // ------------------------------------------------------------------
    Si12TData readRaw() {
        Si12TData data;
        if (!_initialized || !_wire) return data;
        uint8_t raw = readReg(Si12TReg::OUTPUT1);
        for (int i = 0; i < 3; i++) {
            data.intensity[i] = (raw >> (i * 2)) & 0x03;
        }
        return data;
    }

    bool isInitialized() const { return _initialized; }

    // I2C address of the Si12T
    static constexpr uint8_t SI12T_ADDRESS = 0x68;

private:
    enum class State { IDLE, TOUCHED, SWIPING };

    TwoWire*      _wire;
    bool          _initialized;
    State         _state;
    int16_t       _initialPos;
    unsigned long _lastUpdateMs;
    unsigned long _updateIntervalMs;

    void writeReg(uint8_t reg, uint8_t val) {
        sce::i2cbus::Guard g;   // 11/12 bus shared with the IMU (Brain, core 1)
        _wire->beginTransmission(SI12T_ADDRESS);
        _wire->write(reg);
        _wire->write(val);
        _wire->endTransmission();
    }

    uint8_t readReg(uint8_t reg) {
        sce::i2cbus::Guard g;   // repeated-start = ONE transaction under lock
        _wire->beginTransmission(SI12T_ADDRESS);
        _wire->write(reg);
        _wire->endTransmission(false); // repeated start
        _wire->requestFrom(SI12T_ADDRESS, (uint8_t)1);
        if (_wire->available()) return _wire->read();
        return 0;
    }

    void dispatchGesture(HeadGesture g) {
        switch (g) {
            case HeadGesture::Press:         if (onPress)         onPress();         break;
            case HeadGesture::Release:       if (onRelease)       onRelease();       break;
            case HeadGesture::SwipeForward:  if (onSwipeForward)  onSwipeForward();  break;
            case HeadGesture::SwipeBackward: if (onSwipeBackward) onSwipeBackward(); break;
            default: break;
        }
    }
};

} // namespace sce
