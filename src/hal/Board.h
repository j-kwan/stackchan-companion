#pragma once
// =============================================================================
// Board.h — StackChan-Companion (hal)
// =============================================================================
// ORDERED hardware init of the StackChan K151 (CoreS3). Wraps the mandatory
// sequence (CLAUDE.md, "K151 initialisation order"):
//
//   1. M5.begin()            — AXP2101 PMIC: powers the display + SD
//   2. Display               — immediate fillScreen(BLACK) (without it the M5
//                              boot screen stays visible)
//   3. SPI + SD              — SPI2 shared LCD/SD (MISO=35, CS=4)
//   4. Wire1(12, 11, 100kHz) — body I2C bus (G12=SDA, G11=SCL)
//   5. PY32 VM_EN            — servo power, BEFORE any servo.begin()
//   6. I2C probes            — Si12T (0x68): presence ONLY (the full driver
//                              is hal/Si12T.h, started by main.cpp)
//   7. M5.Power.setExtOutput — K151: no takao base
//
// The servo itself is NOT initialised here: it depends on stackchan-arduino
// (#ifdef SCE_USE_SERVO) and lives in behavior/ServoMotion.h (P4). Board only
// guarantees that its power rail (VM_EN) is ready.
//
// Usage:
//   sce::Board board;
//   board.begin();                       // the whole sequence, M5_LOG logs
//   if (board.hasSD()) { ... }
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include "../../firmware/common/SdPins.h"  // the SD wiring, shared with the
                                           // guest bins: four copies had drifted
                                           // in discipline if not in value
#include "I2cBus.h"
#include "Py32Expander.h"
#include "Ina226.h"
#include "Ltr553.h"

namespace sce {

class Board {
public:
    // K151 pins (source: official ESP-IDF firmware + measured calibration)
    // FROM the shared header, not written again: the guest bins carry the
    // same four numbers and the copies had already drifted in discipline
    // if not in value (firmware/common/SdPins.h says how).
    static constexpr int PIN_SD_SCK  = SCE_SD_SCK;
    static constexpr int PIN_SD_MISO = SCE_SD_MISO;
    static constexpr int PIN_SD_MOSI = SCE_SD_MOSI;
    static constexpr int PIN_SD_CS   = SCE_SD_CS;
    static constexpr int PIN_BODY_SDA = 12;  // Wire1 — body bus
    static constexpr int PIN_BODY_SCL = 11;
    static constexpr int PIN_SERVO_RX = 7;   // G7 = K151 Servo_TX (servo → MCU)
    static constexpr int PIN_SERVO_TX = 6;   // G6 = K151 Servo_RX (MCU → servo)
    static constexpr uint8_t I2C_ADDR_SI12T = 0x68;  // head touch panel

    Board() : _py32(Wire1) {}

    // ------------------------------------------------------------------
    // Full sequence. brightness: initial screen brightness (default 76).
    // The optional peripherals (SD, PY32, Si12T) can then be queried through
    // hasSD()/hasSi12T()/py32().
    // ------------------------------------------------------------------
    void begin(uint8_t brightness = 76) {
        // 1. M5 — the PMIC powers the SD card here, not before
        auto cfg = M5.config();
        cfg.serial_baudrate = 115200;
        M5.begin(cfg);
        M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_NONE);
        M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_INFO);
        M5.Log.setEnableColor(m5::log_target_serial, false);
        // Step markers via DIRECT Serial.printf: during P0 validation an init
        // hang happened without a single M5_LOGI line ever showing up. Each
        // step prints BEFORE running → the last visible marker names the step
        // at fault. (Kept: zero cost, ~6 lines at boot, invaluable for any
        // field debugging.)
        Serial.printf("[board] M5.begin OK\n");

        // 2. Display — immediate black background (BOOT-1), before any other step
        M5.Display.setBrightness(brightness);
        M5.Display.fillScreen(TFT_BLACK);
        Serial.printf("[board] display OK\n");

        // 2b. LCD SPI bus write frequency.
        // MUST come BEFORE SD.begin(): the SPI2 bus is shared LCD/SD;
        // reconfiguring the lgfx Bus_SPI after the SD card is attached freezes
        // the ESP-IDF spi_bus_lock (hang observed on hardware, P0 2026-07-06).
        // ⚠ 80 MHz (P-1 spike verdict: ×1.9 throughput) produces visual
        // ARTIFACTS on this panel (offset ghost image + flicker, seen in P0)
        // → 40 MHz by default, the parameter stays exposed for experiments.
        // Performance consequence: eye zone @40 ≈ 45 fps bare — the CRT budget
        // WAS re-measured in P1: 21.5 ms bare / 27.8 ms with CRT, inside the
        // 33 ms frame (ROADMAP §3.8), full-night endurance with CRT ON (P7).
        setLcdWriteFreq(40000000);
        Serial.printf("[board] LCD 40MHz OK\n");

        // 3. SD on SPI2, shared with the LCD (ESP-IDF spi_bus_lock arbitration).
        // 25 → 15 MHz (2026-07-12, docs/sderror.txt): at 25 MHz multi-block
        // WRITES were losing tokens ("no token received", Card Failed cmd
        // 0x18) → diskio retries that held the bus and FROZE the renderer for
        // ~0.7 s. 15 MHz = signal margin; throughput is still plenty
        // (launcher/bins ~1.5 MB/s).
        Serial.printf("[board] SD.begin...\n");
        SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
        _hasSD = SD.begin(PIN_SD_CS, SPI, SCE_SD_HZ);
        Serial.printf("[board] SD %s\n", _hasSD ? "OK" : "absente");

        // 4. Body I2C bus — required by the PY32 (VM_EN) and the Si12T
        Serial.printf("[board] Wire1.begin...\n");
        Wire1.begin(PIN_BODY_SDA, PIN_BODY_SCL, 100000UL);

        // 5. PY32: detection (retry ≤ 1.2 s — slow boot), then VM_EN
        Serial.printf("[board] PY32 detect...\n");
        if (_py32.detect()) {
            Serial.printf("[board] PY32 v0x%02X — VM_EN...\n", _py32.version());
            _py32.enableServoPower();
            Serial.printf("[board] VM_EN actif (servos alimentés)\n");
        } else {
            Serial.printf("[board] PY32 non détecté — servos sans alimentation\n");
        }

        // 6. Si12T probe — presence only (the full driver, hal/Si12T.h, is
        // begun by main.cpp once the app decides to use head touch)
        _hasSi12T = i2cPing(Wire1, I2C_ADDR_SI12T);
        Serial.printf("[board] Si12T %s\n", _hasSi12T ? "présent" : "absent");

        // 6b. INA226 gauge (Wire1 0x41) + LTR-553 light sensor (In_I2C 0x23)
        // — optional, detected by ID. Absent => fallback sources are used.
        _hasIna = _ina226.begin();
        Serial.printf("[board] INA226 %s\n", _hasIna ? "présent" : "absent");
        _hasLtr = _ltr553.begin();
        Serial.printf("[board] LTR-553 %s\n", _hasLtr ? "présent" : "absent");

        // 7. External power output (K151: no takao base)
        M5.Power.setExtOutput(true);

        // IMU — M5Unified initialises it inside M5.begin(); just log the state
        Serial.printf("[board] IMU %s (type %d) — init terminée\n",
                      M5.Imu.isEnabled() ? "OK" : "ABSENT", (int)M5.Imu.getType());
    }

    bool hasSD()    const { return _hasSD; }

    // ------------------------------------------------------------------
    // RE-PROBE THE CARD. `_hasSD` used to be a verdict pronounced ONCE at boot
    // and believed forever, so pulling the card out left every consumer — the
    // API's `sd` field, the console, the guards on config/dances/bins — saying
    // "present" while every access failed. Seen on 08-03: the card was moved to
    // another board, `/api/status` still answered `sd:1`, and a 1.7 MB upload
    // went into a FAT layer with nothing behind it, freezing the renderer for
    // 3.4 s on the timeout.
    //
    // Re-inserting needs MORE than a flag flip: the driver holds a mount that
    // no longer describes anything, so a re-probe that only looked would report
    // a card that still cannot be written to. Hence the full `end()` + `begin()`
    // on the way back in.
    //
    // CALLED FROM loop() ONLY, and NOT on every pass — it touches SPI2, which
    // the LCD shares (A2.16), so the caller pauses the renderer around it and
    // rate-limits it. Returns true when the state CHANGED, so the caller can
    // resynchronise what depends on it (name caches, config) instead of
    // re-doing that work every tick.
    bool probeSD() {
        // Cheap presence test: open the root. On a card that has been pulled,
        // the mount is stale and this fails — which is exactly the question,
        // and it costs one directory open rather than a remount every time.
        File root = SD.open("/");
        const bool alive = root && root.isDirectory();
        if (root) root.close();
        if (alive == _hasSD) return false;
        if (!alive) {
            _hasSD = false;
            Serial.println("[board] SD RETIREE");
            return true;
        }
        return false;   // re-insertion is handled by remountSD(), see below
    }

    // Try to bring a re-inserted card back. Separated from probeSD() because it
    // is the EXPENSIVE half (a full remount) and only makes sense while the
    // flag says absent — polling a remount on a board that simply has no card
    // would pay for it forever.
    bool remountSD() {
        if (_hasSD) return false;
        SD.end();
        if (!SD.begin(PIN_SD_CS, SPI, SCE_SD_HZ)) return false;
        _hasSD = true;
        Serial.println("[board] SD REMONTEE");
        return true;
    }
    bool hasSi12T() const { return _hasSi12T; }
    bool hasIna226() const { return _hasIna; }
    bool hasLtr553() const { return _hasLtr; }
    Py32Expander& py32()  { return _py32; }
    Ina226&  ina226()             { return _ina226; }
    Ltr553&  ltr553()             { return _ltr553; }
    const Ina226& ina226() const  { return _ina226; }
    const Ltr553& ltr553() const  { return _ltr553; }

    // Battery gauge (AXP2101 PMIC, reached through M5.Power — INTERNAL bus,
    // NOT Wire1). 0-100, or -1 if the PMIC cannot answer (unrecognised
    // model). isCharging()/vbusPresent() tell mains/USB apart from a plain
    // level reading.
    // The AXP2101 sits on the shared 11/12 bus (not thread-safe): the lock
    // lives HERE, at the HAL choke point, to cover ALL callers — loop() AND
    // the AsyncTCP handlers (/api/status polled ~1/s, /api/sensors) —
    // otherwise these reads would race the Brain's IMU read (core 1).
    int8_t batteryPercent() const {
        sce::i2cbus::Guard g;
        return (int8_t)M5.Power.getBatteryLevel();
    }
    bool isCharging() const {
        sce::i2cbus::Guard g;
        return M5.Power.isCharging() == m5::Power_Class::is_charging;
    }
    bool vbusPresent() const {
        sce::i2cbus::Guard g;
        return M5.Power.getVBUSVoltage() > 3000;   // mV; absent reads ~0-100 mV
    }

    // Refreshes the cache of the SLOW sensors (battery, INA226, LTR-553).
    // Call ONLY from loop() (blocking I2C on a shared bus) — NEVER inside an
    // AsyncTCP callback. /api/status and /api/sensors then serve these cached
    // values without touching the bus from within the callback.
    void pollSlowSensors() {
        int8_t b = batteryPercent();          // these take the bus lock
        if (b >= 0) _cBatt = b;               // keep the last valid value
        _cChg  = isCharging();
        if (_hasIna) {
            float v = _ina226.busVoltage();   // -1 = failed read (0 impossible)
            if (v >= 0.0f) { _cInaV = v; _cInaShunt = _ina226.shuntMilliV(); }
        }
        // LTR: failure GUARD like the battery — visible() < 0 = failed
        // transaction, so we KEEP the last valid value. Without it a failure
        // read as 0 = "dark" and dark_sleepy put the robot to sleep in broad
        // daylight (review 07-21 — the only sensor cache missing its guard).
        if (_hasLtr) {
            int32_t v = _ltr553.visible();
            if (v >= 0) {
                _cLightRaw = (int)v;          // raw counts (calibration)
                _cLight    = (int)(Ltr553::level01From(v) * 100.0f);
            }
        }
    }
    int8_t cachedBattery()  const { return _cBatt; }
    bool   cachedCharging() const { return _cChg; }
    float  cachedInaV()     const { return _cInaV; }
    float  cachedInaShunt() const { return _cInaShunt; }
    int    cachedLight()    const { return _cLight; }
    int    cachedLightRaw() const { return _cLightRaw; }   // CH0 counts (diag)

    // LCD write frequency — exposed for the 40/80 MHz A/B experiments.
    // Call ONLY when no push is in flight (at boot, or with the renderer
    // paused) and preferably before SD.begin (see step 2b).
    static void setLcdWriteFreq(uint32_t hz) {
        auto panel = (lgfx::Panel_Device*)M5.Display.getPanel();
        auto bus   = (lgfx::Bus_SPI*)panel->getBus();
        auto bcfg  = bus->config();
        if (bcfg.freq_write == hz) return;
        bcfg.freq_write = hz;
        bus->config(bcfg);
    }

private:
    Py32Expander _py32;
    Ina226       _ina226{Wire1};
    Ltr553       _ltr553;
    bool _hasSD    = false;
    bool _hasSi12T = false;
    bool _hasIna   = false;
    bool _hasLtr   = false;
    // Slow-sensor cache (refreshed by pollSlowSensors from loop())
    int8_t _cBatt = -1;
    bool   _cChg  = false;
    float  _cInaV = -1.0f, _cInaShunt = 0.0f;
    int    _cLight = -1;
    int    _cLightRaw = -1;   // raw CH0 counts (calibration/diagnostics)

    // I2C ping: an empty transmission that gets ACKed = device present
    static bool i2cPing(TwoWire& w, uint8_t addr) {
        w.beginTransmission(addr);
        return w.endTransmission() == 0;
    }
};

} // namespace sce
