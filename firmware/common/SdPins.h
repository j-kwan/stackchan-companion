#pragma once
// =============================================================================
// SdPins.h — the SD card's shared SPI2 wiring, in ONE place
// =============================================================================
// Four copies of the same four numbers existed: the three guest bins and
// `src/hal/Board.h`. They agreed on the VALUES and not on the DISCIPLINE, which
// is the more dangerous half:
//
//   · `flight-radar` guarded each macro separately and wrote down why;
//   · `space` guarded all four behind the first — exactly the trap the radar's
//     comment describes. A board that shares the CoreS3 trio but wires CS
//     elsewhere defines only `SCE_SD_CS`, leaves `SCE_SD_SCK` undefined, and
//     has the block put CS silently back to 4. The redefinition warning
//     scrolls past in a PlatformIO build, the mount fails, and then there are
//     no WiFi credentials and no STA — bug 07-25, a second time;
//   · `ha-remote` had no macros at all: `SPI.begin(36, 35, 37, 4)` written
//     twice as literals, so no board profile could reach it.
//
// FOUR GUARDS, ONE PER PIN, deliberately. A single guard covering the group is
// the bug above; that is the whole reason this header exists rather than a
// tidier-looking block.
//
// The defaults are the CoreS3/K151. A board profile overrides them from
// `platformio.ini` (see `space-fire` and `flight-radar-fire`, which move the
// bus to the classic M5Stack VSPI wiring).
// =============================================================================

#ifndef SCE_SD_SCK
#define SCE_SD_SCK  36
#endif
#ifndef SCE_SD_MISO
#define SCE_SD_MISO 35
#endif
#ifndef SCE_SD_MOSI
#define SCE_SD_MOSI 37
#endif
#ifndef SCE_SD_CS
#define SCE_SD_CS    4
#endif

// 15 MHz, and A2.16 is the reason it is not 40: the ~0.7 s freeze was traced to
// SPI2 contention with the LCD, not to the clock — the real fix is bracketing
// deferred SD access with `renderer.pause()`/`resume()`. 15 MHz stays because
// it keeps signal margin on the K151's ribbon, not because it fixed anything.
#ifndef SCE_SD_HZ
#define SCE_SD_HZ 15000000
#endif
