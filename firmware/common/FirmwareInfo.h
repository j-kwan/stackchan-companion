#pragma once
// =============================================================================
// FirmwareInfo.h - WHICH BUILD IS RUNNING, AND WHY IT LAST STARTED
// =============================================================================
// A USB flash lands in ONE OTA slot; the boot slot is chosen by `otadata`,
// which esptool never writes. So `pio -t upload` can report success, verify its
// own hash, and leave the robot running something else entirely - that is not a
// hypothesis, it happened (2026-08-10): a guest handing back the robot reflashed
// the STALE /companion.bin from the SD card over a fresh USB flash, and nothing
// anywhere said so. Identifying the build took a flash dump over USB.
//
// Nothing here is new information - the chip knew all of it the whole time. It
// simply had no way out. These three values close that hole, and what publishes
// them (`GET /api/firmware`, the console footer, the guest's own /config) is the
// cheapest possible answer to "am I actually looking at my own code?".
//
// IN `firmware/common/` AND NOT `src/app/`: the companion is not the only side
// that needs this. The GUESTS are what reflash `/companion.bin`, so a guest that
// cannot say which build it is is exactly the blind spot the incident ran
// through. This is the directory both sides already include - the same reasoning
// that put `Yaml.h` here (rule 17 / A2.23), and for the same reason: one
// implementation, no assumed twins.
//
// EVERY value is constant for the whole boot and costs a mapped-flash read, so
// this is safe from an AsyncTCP callback (A2.6): no I2C, no SD, no allocation.
// =============================================================================

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <string.h>

// >>> VENDORABLE BEGIN FirmwareInfo <<<
// NEEDS: <esp_ota_ops.h>
// NEEDS: <esp_system.h>
// NEEDS: <string.h>
namespace sce {
namespace fw {

// Why the chip last started. Printed at boot AND published, because the serial
// line is not an option after the fact: opening it on native USB RESETS the
// board, which destroys the very evidence you opened it to read.
inline const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "poweron";    // plug / battery
        case ESP_RST_EXT:       return "ext";        // reset pin
        case ESP_RST_SW:        return "sw";         // ESP.restart: OTA, launcher, /api/reboot
        case ESP_RST_PANIC:     return "panic";      // exception - a CRASH
        case ESP_RST_INT_WDT:   return "int_wdt";    // interrupt watchdog
        case ESP_RST_TASK_WDT:  return "task_wdt";   // a task starved the IDLE task
        case ESP_RST_WDT:       return "wdt";        // other watchdog
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";   // supply sagged (servos!)
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

// The OTA slot actually running ("app0" / "app1"). THE decisive field: a flash
// that went to the other slot is invisible in every other way.
inline const char* slot() {
    const esp_partition_t* p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}

// First 8 hex of the app ELF sha256 - a fingerprint of THIS build, and the one
// value that cannot be confused with a neighbouring version.
//
// REPRODUCIBLE FROM A WORKING COPY, which is what makes it useful rather than
// merely unique: this is the sha256 of the .elf file itself, so
//     sha256sum .pio/build/<env>/firmware.elf   (first 8 hex)
// prints the same thing. Verified on target 2026-08-10.
//
// There is deliberately NO build date next to it. The obvious source,
// `esp_ota_get_app_description()->date`, is the date the PRECOMPILED ARDUINO
// LIBRARIES were built - it answered "Mar 5 2024" on a firmware compiled
// minutes earlier. A field that looks authoritative and is wrong is worse than
// no field, and it is the exact failure this whole header exists to end.
inline const char* sha8() {
    static char buf[9] = {0};
    if (!buf[0]) {
        char full[65] = {0};
        // Writes a NUL-terminated hex string, so `size` counts the NUL.
        esp_ota_get_app_elf_sha256(full, sizeof(full));
        memcpy(buf, full, 8);
        buf[8] = '\0';
    }
    return buf;
}

}  // namespace fw
}  // namespace sce
// >>> VENDORABLE END FirmwareInfo <<<
