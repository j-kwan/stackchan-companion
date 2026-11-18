#pragma once
// =============================================================================
// I2cBus.h — StackChan-Companion (hal)
// =============================================================================
// Lock for the I2C bus shared on pins 11/12. On the K151's CoreS3, the
// INTERNAL bus `M5.In_I2C` (BMI270 IMU, AXP2101 PMIC, RTC, FT6336 screen
// touch, audio codecs, GC0308 camera SCCB) AND the BODY bus `Wire1` (PY32
// LEDs, Si12T head touch, INA226 gauge) are wired to THE SAME physical pins
// G11/G12 (official M5Stack documentation). Two driver stacks, a single
// 2-wire bus, no hardware arbitration → transactions interleaved between the
// Brain (core 1, IMU read at 100 Hz) and the Arduino loop (core 0: touch,
// battery, LEDs, camera SCCB) corrupt the exchanges (flat/black camera
// frames, corrupted gyro sample → VOR glitch / spurious Scared reflex).
//
// Design choice (keep the VOR USABLE): a SHORT FreeRTOS mutex with priority
// inheritance, taken PER TRANSACTION. The Brain no longer SKIPS its IMU read
// (the old advisory gate froze the VOR for ~1.5 s when the camera powered
// up) — at worst it BLOCKS for the length of one transaction (~0.3-2 ms).
// The camera SCCB init burst (~300 writes) is locked write BY write (NEVER
// as one block, NEVER across a delay()), so the Brain slips its 100 Hz reads
// between the writes: VOR stays alive even during init.
//
// RULES: NEVER hold the lock across a delay()/vTaskDelay or any blocking
// wait; keep critical sections down to a single transaction.
// =============================================================================

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace sce {
namespace i2cbus {

#if defined(ARDUINO)

inline SemaphoreHandle_t& handle() {
    static SemaphoreHandle_t h = nullptr;
    return h;
}

// Call ONCE from setup(), BEFORE starting the tasks (Brain/Renderer/
// ServoMotion) that touch the bus. Idempotent.
inline void init() {
    if (!handle()) handle() = xSemaphoreCreateRecursiveMutex();
}

inline void lock() {
    SemaphoreHandle_t h = handle();
    if (h) xSemaphoreTakeRecursive(h, portMAX_DELAY);
}
inline void unlock() {
    SemaphoreHandle_t h = handle();
    if (h) xSemaphoreGiveRecursive(h);
}

#else   // native build (PC tests): no FreeRTOS, everything is a no-op

inline void init()   {}
inline void lock()   {}
inline void unlock() {}

#endif

// RAII guard: locks the bus for the duration of ONE I2C transaction.
//   { sce::i2cbus::Guard g; M5.Imu.update(); }
struct Guard {
    Guard()  { lock(); }
    ~Guard() { unlock(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

} // namespace i2cbus
} // namespace sce
