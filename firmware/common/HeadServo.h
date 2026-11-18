#pragma once
// =============================================================================
// HeadServo.h — the K151 neck, for the guest bins that point the head
// =============================================================================
// flight-radar points the head at the tracked flight, space at the satellite
// overhead. Both need the same four things — power the servos through the PY32
// expander, open the SCS bus, write a position WITHOUT blocking, release the
// torque when nothing moves — and the radar's copy was the only one until
// space needed it. One implementation, so the two cannot drift.
//
// Include it only under the bin's SCE_HAS_SERVO: it pulls stackchan-arduino,
// which a board without servos (the Fire profiles) does not link.
//
// Not for the companion: its ServoMotion runs a 50 Hz task with the efference
// copy the VOR needs. A guest has no Brain; one command a second is plenty.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Stackchan_servo.h>

namespace sce {

class HeadServo {
public:
    // Powers the servos and opens the bus. BLOCKS ~1.8 s (the PY32 boot wait
    // and the library's own delays): call it from loop() or setup(), NEVER
    // from an HTTP handler. Idempotent.
    void begin() {
        if (_ready) return;
        // ⚠ G11/G12 are SHARED with the internal bus (LTR-553, PMIC, touch):
        // Wire1 is opened only ONCE per session, never again.
        if (!_wireUp) { Wire1.begin(12, 11, 100000); _wireUp = true; }
        py32VmEnable();
        delay(300);                              // PY32 boot (~200 ms)
        _s.begin(7, 166, 0, 6, 93, 0, ServoType::SCS);   // rx=G7, tx=G6
        _ready  = true;
        _torque = true;                          // begin() applied the start pose
        _yaw = 166.0f; _pitch = 93.0f;
        _moveMs = millis();                      // → released 1.5 s later
    }
    bool ready() const { return _ready; }

    // Moves towards (yaw, pitch) in `ms`, raw servo degrees (Units.h scale).
    // NaN pitch = leave the pitch where it is (the radar only turns). Sends
    // nothing for a change under a degree, so a target held still costs no
    // bus traffic and lets the torque release. Never blocks.
    void moveTo(float yaw, float pitch, uint16_t ms = 900) {
        if (!_ready) return;
        const bool dYaw   = fabsf(yaw - _yaw) > 1.0f;
        const bool dPitch = !isnan(pitch) && fabsf(pitch - _pitch) > 1.0f;
        if (!dYaw && !dPitch) return;
        if (!_torque) { _s.torque(true); _torque = true; }   // re-engaged on demand
        if (dYaw)   { _yaw = yaw;     _s.writeDeg(1, _yaw, ms); }
        if (dPitch) { _pitch = pitch; _s.writeDeg(2, _pitch, ms); }
        _moveMs = millis();
    }

    // Call about once a second. Releases the torque once nothing has moved for
    // 1.5 s (the setpoints last 900 ms), or at once when the option is off:
    // at rest the SCS0009 draw current and heat up for nothing, and a released
    // head can be turned by hand. Same policy as the companion.
    void service(uint32_t now, bool wanted) {
        if (_ready && _torque && (now - _moveMs > 1500 || !wanted)) {
            _s.torque(false);
            _torque = false;
        }
    }

private:
    // StackchanSERVO::moveX() does a vTaskDelay for the whole move — it
    // BLOCKS. From loop(), 900 ms of blocking a second made the screen, the
    // touch and the remote stop unusable (review 07-27c; the companion's A2
    // rule "moveXY lib = BLOCKING → raw WritePos per tick"). So the bus is
    // reached directly.
    class Raw : public StackchanSERVO {
    public:
        void torque(bool on) {                   // IDs X=1, Y=2
            _sc.EnableTorque(1, on ? 1 : 0);
            _sc.EnableTorque(2, on ? 1 : 0);
        }
        // deg 0-300 → SCS0009 position, CLAMPED: the library does not bound
        // it and an out-of-range degree yields a negative position.
        void writeDeg(uint8_t id, float deg, uint16_t timeMs) {
            if (deg < 0)   deg = 0;
            if (deg > 300) deg = 300;
            _sc.WritePos(id, (uint16_t)(1023.0f - deg * (1023.0f / 300.0f)), timeMs);
        }
    };

    // Servo power: PY32 GPIO0 (I2C 0x6F, body bus Wire1). Without VM_EN the
    // SCS0009 stay mute (K151 init order).
    static void py32VmEnable() {
        auto setBit = [](uint8_t reg, uint8_t bit) {
            uint8_t v = 0;
            if (!Wire1.requestFrom((uint8_t)0x6F, (uint8_t)1)) return;
            Wire1.beginTransmission(0x6F); Wire1.write(reg); Wire1.endTransmission(false);
            Wire1.requestFrom((uint8_t)0x6F, (uint8_t)1);
            if (Wire1.available()) v = Wire1.read();
            Wire1.beginTransmission(0x6F); Wire1.write(reg); Wire1.write(v | bit);
            Wire1.endTransmission();
        };
        setBit(0x03, 0x01);   // DIR: GPIO0 as output
        setBit(0x09, 0x01);   // pull-up
        setBit(0x05, 0x01);   // OUT HIGH → VM_EN
    }

    Raw      _s;
    bool     _ready  = false;
    bool     _wireUp = false;
    bool     _torque = false;
    float    _yaw    = 166.0f;   // last setpoints (deg, Units.h centre/level)
    float    _pitch  = 93.0f;
    uint32_t _moveMs = 0;
};

} // namespace sce
