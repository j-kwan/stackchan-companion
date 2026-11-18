> **English** · [Français](README.fr.md)

# StackChan-Companion

[![Licence](https://img.shields.io/badge/licence-AGPL--3.0-green)](LICENSE)
[![Plateforme](https://img.shields.io/badge/plateforme-M5Stack%20StackChan%20K151-red)](https://docs.m5stack.com/en/StackChan/)

> **Personal project.** Built solo, on my own robot, in my spare time — with
> heavy use of AI coding assistance (Claude Code) throughout the design,
> implementation and docs. Not an official M5Stack or StackChan product, no
> support SLA, no roadmap commitment. Read the code and docs with that in
> mind; issues and PRs are welcome regardless.

Companion firmware for **StackChan** — M5Stack CoreS3, K151 kit. It gives the
robot smooth, natural animation with an unapologetically robotic identity.

The direction comes from **Wall-E, Cozmo and Vector**: the eyes lead and the
head follows, saccades are crisp and fixations are held, shapes squash and
stretch, and mechanical gestures keep a deliberate metronomic beat.

The rest is in the table below — gyroscopic VOR, 30 expressions, 15 dances, a
REST API with an embedded console, an SD `.bin` launcher, and switchable
CRT / LEDs / sound.

<video src="https://github.com/user-attachments/assets/d958b53c-875e-495d-b876-cf4547b22917" controls muted></video>

![The embedded web console — Pilot tab](docs/assets/screenshots/Webconsole%20-%20pilot.png)

<details>
<summary>More of the console — Options, Files, System</summary>

![Options tab — screen/LEDs/sound, camera, fine tuning](docs/assets/screenshots/Webconsole%20-%20options.png)
![Files tab — the SD card, unified](docs/assets/screenshots/Webconsole%20-%20files.png)
![System tab — NTP, CORS, WiFi, VOR calibration, OTA update, language](docs/assets/screenshots/Webconsole%20-%20system.png)

</details>

---

## Features

| Feature | Detail |
|---|---|
| **30 expressions** | 18 ported from esp32-eyes + 12 of our own (Dead, Excited ✦, Blush, Squint…); asymmetries mirrored at random, dynamic shapes (Sad "looking at the sky", Curious "edge-of-eye") |
| **Gyroscopic VOR** | Vestibulo-ocular reflex: immediate counter-rotation (gyro), catch-up saccades (nystagmus), tilt hold; sensor mapping **tunable at runtime** |
| **Living idle** | Fixation → crisp saccade → micro-overshoot; micro-jitter on long fixations; log-uniform blinks, per-expression policies |
| **Sleepy "struggle"** | Slow droop → fall → laborious capped reopening → 1-in-4 startle |
| **15 dances** | Unified servo+expression+eyelid+gaze keyframes; direction mirrored at random; customizable via CSV on the SD card |
| **Preemptive reflexes** | Shake → Scared, pick-up → Curious + "dangling feet" (torque released) — they cut EVERYTHING immediately |
| **IMU gestures** | Double-tap (accel peak) → Happy + wink; sustained screen-face-down rest → Sleepy held (going to sleep) |
| **Touch gestures** | Screen: L/R swipe → emotion ±1, tap → blink/wink, up swipe → dance; Si12T head petting → Happy/Glee. **Manual interactions take priority**: they cut the dance in progress (rule A2.5). L/R swipe **inside the status band** → cycles through the modes (persisted) |
| **Battery + sensors** | PMIC gauge (%, charging) + INA226 gauge (voltage/current) + night volume (real sunset→sunrise, NTP + `lat`/`lon`) + **automatic screen brightness** and **night mode** (LTR-553 sensor: in the dark, Sleepy dominates the roulette — the robot dozes off; it wakes when the light comes back) + magnetic heading (BMM150) — telemetry `GET /api/sensors` |
| **Safe I2C bus** | Short FreeRTOS lock on the shared 11/12 bus (IMU/AXP/SCCB/LEDs/touch): the VOR stays alive during the camera stream (only the SCCB config burst at power-on is exclusive, ~0.5 s) |
| **Camera (option)** | GC0308 for Home Assistant / Frigate: **lightweight live** view (320×240 compressed — commands stay responsive even while streaming) + **full-quality VGA** snapshot `?full=1` + **MJPEG stream**; dedicated task, off by default, auto-shutdown, settings tunable at runtime ([`docs/integrations/HOMEASSISTANT.md`](docs/integrations/HOMEASSISTANT.md)) |
| **Releasable servos** | `servos` option: torque released (head goes limp, movable by hand), the eyes keep going — smooth resume |
| **Status band & plugins** | Bottom of the screen driven by **fields** (`/api/field`): a **sound visualiser** (a triggered scope over both microphones, in three skins), gauge/timer/pomodoro modes + **maskable icons** + optional debug info + scrolling notifications (`/api/say`); `field→emotion/dance` **rules engine** loadable from the SD card (`rules.txt`, hot-reload; `GET /api/rules` lists the table **as loaded**, and the console has a Rules section of its own) — community reactive plugins without recompiling. **Claude Code** bridge via statusline ([`docs/reference/PLUGINS.md`](docs/reference/PLUGINS.md), [`docs/reference/STATUSBAR.md`](docs/reference/STATUSBAR.md)) |
| **SD import / export** | Download/replace/delete `config.yaml`, rules, choreographies and binaries through the API (`/api/sd/*`) and the console (whitelisted paths) |
| **Overlay effects** | Blush (anime-style pink cheeks, they follow the gaze), sparkles ✦, sweat drop — per emotion |
| **CRT effect** | Scanlines + phosphor persistence + halo + flicker (LUT, switchable) |
| **LEDs + sound** | Emotional LED emphasis (WS2812×12) + chirps by valence (off by default) |
| **Head towards the noise** | `sound_track` option (2 ES7210 microphones): turn ∝ L/R imbalance, `shocked` startle on a loud hit; **muted while the servos move** (gear noise does not trigger another turn) — off by default |
| **Embedded web console** | `http://<ip>/` — zero CDN (works in AP mode), consistent "Liquid Glass" design. **Four tabs** — Pilot (emotions, dances, head, status band, rules), Options (grouped switches, live camera, fine tuning), Files (**unified SD manager**), System (NTP, **cross-origin access**, WiFi, VOR, Basic Auth, **OTA update**, language) — over a full-bleed live telemetry band (status chips + heap/frame graph + the band's icon pills) that every tab shares. The tab is in the URL hash, so a reload lands where you left off |
| **REST API + Swagger** | `/api/*` + OpenAPI; **runtime tuning persisted to SD** (**73 keys**, automatic schema migration). A gate proves every one of them is reachable from the console: a key the API serves but the console never shows looks finished, when in practice you can only find it by opening Swagger |
| **Build identity** | `GET /api/firmware` says which OTA partition actually booted (`slot`), fingerprints **this exact build** (`sha` = the app ELF's sha256, `console` = the embedded console's digest — both reproducible from a working copy, by `sha256sum` and `gen_console_gz.py --check`), and why the board last started (`reset`: `panic`/`task_wdt`/`brownout` name a crash). A USB upload writes one slot and never touches `otadata`, so a flash reporting success does not prove the board boots it |
| **Runtime debug trace** | Tagged serial narration (`net`/`cfg`/`http`/`sd`/`ui`/`task`, lines `[dbg][tag] +millis …`) switchable **without reflashing** — in every firmware here: the companion (`debug` tuning key, persisted, applied within a frame) and the four guest bins (a Debug box on their `/config`, kept in NVS). The moments worth tracing are the ones where a reflash would destroy the evidence. No secrets: URLs are cut at the query string, tokens reported present/absent only |
| **.bin launcher** | Top-to-bottom swipe → SD `/bins/` menu; also managed through the API (upload/delete/launch/stop); third-party binaries can be stopped remotely — **boot lobby** (an escape hatch independent of the guest code and of the network) and **web configuration page** provided by the `SceGuest` stub ([`docs/guests/README.md`](docs/guests/README.md)) |
| **PC tool** | **Choreography** editor: timeline, eye simulator (firmware geometry, real presets), orientation cube, playback at true durations, CSV export ([`tools/choregraphies/`](tools/choregraphies/)) |
| **FreeRTOS** | brain 100 Hz (core 1 prio 4) · renderer 30 Hz (core 1 prio 3) · servo 50 Hz (core 0 prio 3); lock-free TripleBuffer + CommandQueue |

![The choreography editor — timeline, eye simulator, orientation cube, CSV](docs/assets/screenshots/Companion%20-%20Choregraphies.png)

## Guest bins

A guest bin is **not** a mode of the firmware above: it is a separate program
that takes the robot over. You drop it on the SD card, start it from the
launcher (top-to-bottom swipe) or from the API, and it runs instead of the
companion until you send it back. Four ship with this repository, and what
makes them cheap to write is the [`SceGuest`](docs/guests/README.md) contract —
it hands any third-party binary a way back to the companion, a boot lobby that
works even when the guest crashes, and a web settings page it does not have to
write.

| Bin | What it does | Second board |
|---|---|---|
| [**`flight-radar`**](docs/guests/FLIGHT-RADAR.md) | Real-time ADS-B radar: tracking one flight (route, cities, ETA, progress), symbols by ICAO category, velocity vectors, 4 paired day/night themes, aero ⇄ metric units, pinch zoom, automatic brightness and theme, METAR/TAF/NOTAM decks, head pointing at the tracked aircraft | M5Stack Fire, standalone |
| [**`space`**](docs/guests/SPACE.md) | Desktop space instrument: the ISS live (Celestrak TLE + SGP4 computed on board), visible passes worked out locally, the Moon (phase, orbital diagram, umbral eclipses), the naked-eye planets, the next launches, and optionally the **head following the satellite** across the sky | M5Stack Fire, standalone |
| [**`ha-remote`**](docs/guests/HA-REMOTE.md) | Home Assistant remote: home screen by category (shutters, lights, plugs, cameras), one entity at a time on ←/→ swipe, **a shutter's position and a lamp's color / temperature / power**, near real-time state feedback, camera thumbnails | — |
| [**`led-fluid`**](docs/guests/LED-FLUID.md) | Liquid in a box: a particle fluid whose gravity IS the tilt of the robot, painted as a grid of dots (density gives the colour, speed gives the light), shake to splash, tap to push. Physics panel on a swipe right — viscosity, gravity, bounce, trail, five presets — hue/saturation rectangle on a swipe left, and the twelve WS2812 can echo the fluid | — |

The two marked **standalone** build for an M5Stack Fire from the *same source*,
with the hardware differences declared as capability flags rather than forked —
no companion, no K151, three buttons instead of a touch panel.

## Quick start

**Install a release without building anything**: download the files of the
[latest release](https://github.com/j-kwan/stackchan-companion/releases/latest)
and follow [`docs/INSTALL.md`](docs/INSTALL.md) (flashing from the browser, SD
card, WiFi, updates).

**From source**:

```powershell
# Native tests (no hardware — MinGW)
.\scripts\gates\test-native.ps1

# Build + flash the main firmware (CoreS3 on COM6)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e companion -t upload --upload-port COM6

# A guest bin — dropped on the SD card, started from the launcher. Same two
# lines for flight-radar, ha-remote and led-fluid: the four share one contract.
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e space
curl -X POST "http://<ip>/api/bins" -F "file=@.pio/build/space/firmware.bin;filename=space.bin"
```

At boot: WiFi **STA** if credentials are configured (SD card or
`POST /api/wifi`), falling back to the **`StackChan-AP`** / `goodlife` hotspot
otherwise. Console: `http://<ip>/` (or `http://stackchan.local/` in STA) — in
AP mode the captive portal redirects there automatically.

### SD card (optional)

A ready-to-copy template lives in [`sdcard/`](sdcard/README.md):

```
/companion.bin               SD-Updater restore ([SauverFW] from the launcher)
/bins/*.bin                  launchable binaries (launcher + API)
/dances/*.csv                custom choreographies
/stackchan-companion/config.yaml  persisted wifi + tuning (schema docs/reference/CONFIG.md)
```

## Architecture

```
src/
├── engine/     rendering + PURE primitives: EyeRig (30 emotions), Renderer (task),
│               FaceState/TripleBuffer, EyeGeometry (anti-overflow invariants),
│               CrtEffect, EyeEffects (overlays), Blender, Tuning, Units, Clock/Rng
├── behavior/   PURE behavior (Clock/Rng injected, tested natively):
│               Brain (100 Hz task, the ONLY FaceState writer, CommandQueue),
│               EmotionRoulette, IdleBehavior, BlinkController, VestibularSystem
│               (VOR), PickupDetector, Sequencer/Dances, ServoMotion (50 Hz task)
├── hal/        Board (ordered K151 init), Py32Expander (VM_EN+LEDs),
│               ImuReader (axis mapping), Si12T (head touch), ArduinoClock
├── interact/   TouchGestures (screen swipes/taps)
├── app/        WebApi (REST+mDNS+portal), WebConsole (embedded pages),
│               EmotionLeds, SoundFx, Launcher, SdConfig (YAML)
└── guest/      SceGuest.h — stub to embed in your own .bin files (remote stop,
                details: docs/guests/README.md)

firmware/common/  the 17 headers shared across firmwares, so a rule lives in
                ONE place: Yaml (the only line decoder), I18n (T(en,fr)), Trace
                (runtime debug), FirmwareInfo (which build is running), Gesture
                (swipe thresholds), ButtonFsm, SunClock, Ltr553, PsJson,
                CellText, CfgBool, SdPins, SdWatch, SdRoot, Py32Leds, HeadServo and
                headtrack (the neck and its aim, for the guest bins that move it). Five are
                vendored into SceGuest.h so the stub stays copyable on its own
                — a gate proves the copies never drift

scripts/        gates/ (what check-all runs, and nothing else), dev/ (run by
                hand against a board), build/ (run by PlatformIO)
                — details: scripts/README.md
tools/          PC-side, never on the robot: choregraphies/ (dance editor),
                generators/, probes/ — details: tools/README.md
```

Two structural rules, among the 26 in [`docs/ROADMAP.md`](docs/ROADMAP.md) §A2.
The **Brain is the only writer** of the facial state: everything else goes
through the CommandQueue. And the **Brain is the only source of smoothing**:
the Renderer draws what it receives, unchanged — a ramp added on the rendering
side would crush blinks and the VOR.

For an overview of the key mechanisms with diagrams (FreeRTOS loop, VOR
pipeline, dance sequencing, network portal…), see
[`docs/architecture/WORKFLOWS.md`](docs/architecture/WORKFLOWS.md).

## PlatformIO environments

| Environment | Usage |
|---|---|
| **`companion`** | **main firmware** (`firmware/companion/`) |
| `flight-radar` | guest bin: real-time ADS-B radar (`firmware/flight-radar/`) |
| `flight-radar-fire` | the same radar source, standalone on an M5Stack Fire (buttons, no K151, no companion) |
| `ha-remote` | guest bin: Home Assistant remote (`firmware/ha-remote/`) |
| `space` | guest bin: desktop space instrument (`firmware/space/`) — ISS, passes, Moon, planets, launches |
| `space-fire` | the same space source, standalone on an M5Stack Fire (buttons, no K151, no companion) |
| `led-fluid` | guest bin: a particle fluid tilted by the IMU (`firmware/led-fluid/`) |
| `led-fluid-fire` | the same fluid source, standalone on an M5Stack Fire (buttons, no K151, no companion) |
| `native` | **429 unit tests in 37 suites** on the PC (`scripts/gates/test-native.ps1`) — `check-all` asserts both counts, since `pio test` exits 0 on what it ran and says nothing about what it skipped |

With two boards plugged in, resolve the upload port by USB identity rather
than by COM number — `.\scripts\dev\find-port.ps1 -Board fire` — otherwise a bare
`-t upload` can overwrite the StackChan's companion.

## Documentation

Full map: [`docs/README.md`](docs/README.md). The steering document is
[`docs/ROADMAP.md`](docs/ROADMAP.md).

| Section | Contents |
|---|---|
| [`docs/architecture/`](docs/architecture/) | how the firmware works — [mechanisms as diagrams](docs/architecture/WORKFLOWS.md), [conventions](docs/architecture/CONVENTIONS.md) |
| [`docs/hardware/`](docs/hardware/) | the machine it drives: [inventory and boot order](docs/hardware/README.md), [the shared buses](docs/hardware/BUSES.md), [part by part](docs/hardware/PERIPHERALS.md), [budgets and dead ends](docs/hardware/LIMITS.md) |
| [`docs/reference/`](docs/reference/) | [the REST API](docs/reference/API.md), [the 30 expressions](docs/reference/EMOTIONS.md), [security](docs/reference/SECURITY.md), [the eyes](docs/reference/EYES.md), [config.yaml](docs/reference/CONFIG.md), [status band](docs/reference/STATUSBAR.md), [dances](docs/reference/CHOREGRAPHIES.md), [plugins](docs/reference/PLUGINS.md) |
| [`docs/guests/`](docs/guests/) | the guest `.bin` files: [SceGuest contract](docs/guests/README.md), [flight-radar](docs/guests/FLIGHT-RADAR.md), [ha-remote](docs/guests/HA-REMOTE.md), [space](docs/guests/SPACE.md), [led-fluid](docs/guests/LED-FLUID.md) |
| [`docs/integrations/`](docs/integrations/) | [Home Assistant drives the robot](docs/integrations/HOMEASSISTANT.md) |
| [`docs/validation/`](docs/validation/) | [hardware playbook](docs/validation/PLAYBOOK-HW.md), [dashboard](docs/validation/VALIDATION.md) |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | building, the nine gates and what each one will tell you, the rules that catch people out |
| [`CHANGELOG.md`](CHANGELOG.md) | current state of the firmware (single version) |

## Hardware — StackChan SKU:K151

- **M5Stack CoreS3**: ESP32-S3, 320×240 IPS touchscreen (FT6336U), 9-axis IMU
  **BMI270 + BMM150** (magnetometer), **LTR-553** ambient light
- **SCS0009 servos** on a serial bus (yaw 166±130°, pitch **19..99°** = M5Stack
  official spec 5-85°, home 93°) — powered through the **PY32 IO Expander**
  (I2C 0x6F, VM_EN GPIO0) + WS2812C×12 LEDs
- **Si12T head touch** (0x68) + **INA226 gauge** (0x41), `Wire1` bus G12/G11
  (⚠ the same physical pins as the internal bus — `hal/I2cBus.h` lock)
- **Constraints measured on this hardware**: LCD bus **40 MHz** (80 = panel
  artifacts), **SD 15 MHz** (25 = token losses on write → display freeze), LCD
  reconfiguration BEFORE `SD.begin()` (spi_bus_lock deadlock), serial with
  **DTR alone** (DTR+RTS = bootloader), never a `setBrightness()` per frame
  (I2C PMIC).

**[`docs/hardware/`](docs/hardware/) is the full account**: every part with its
address and driver, the boot order and why it is an order, what sharing each
bus costs, the calibrations that must not drift — and a table of what was
tried on this board and **failed**, with the measurement that closed it.

## Acknowledgements

This project builds on the work and ideas of several communities:

- **[esp32-eyes](https://github.com/playfultechnology/esp32-eyes)** (Alastair
  Aitchison, Playful Technology) and **[ESP32_Faces](https://github.com/luisllamasbinaburo/ESP32_Faces)**
  (Luis Llamas) — the rounded rectangular eye style and the original
  transition/drawing engine, ported and extended here (`engine/EyeRig.h`,
  `EyeDrawer.h`, `Transitions.h`, `Animations.h`, presets).
- **[RoboEyes](https://github.com/FluxGarage/RoboEyes)** (FluxGarage) —
  conceptual inspiration for idle gaze behavior.
- **[StackChan](https://github.com/meganetaaan/stack-chan)** (meganetaaan) and
  the M5Stack-Avatar / stackchan-arduino ecosystem — the K151 hardware
  platform itself, along with the reference dances/gestures and the Si12T
  driver.
- **Anki Cozmo/Vector** — the artistic reference that guided the whole
  animation direction of this firmware (eyes-lead-head-follows, crisp
  saccades, thin-line blink).
- **Disney/Pixar's *Inside Out* (2015) and *Inside Out 2* (2024)** — not for
  the characters, but for the emotion colour-combination system the films'
  own marketing charts lay out: a handful of families plus a directional grid
  of what two feelings blended together look like. The 30-emotion palette in
  `src/engine/Emotions.h` is built directly on that chart, credited in code
  (`docs/assets/insideout - combination.png`).
- **Mobile Suit Gundam** (Sunrise/Yoshiyuki Tomino) — the companion console's
  "Gundam" theme, and the `flight-radar` guest bin's own "Gundam" / "Gundam
  night" palette (its default, even on a bare card), are both the actual
  RX-78-2 colour scheme (blue, gold, red, white). Haro — one of the two
  characters the firmware ships compiled-in — borrows its name from the
  franchise's own round companion robot.

- **[autorouter.aero](https://www.autorouter.aero/)** — the NOTAM source of
  the `flight-radar` guest bin, drawn from the EUROCONTROL EAD. Free, and the
  only service that answered for this aerodrome after several others returned
  nothing or demanded a commercial key. Its OAuth 2.0 API is documented,
  honest about its limits, and its support answers.
- **[aviationweather.gov](https://aviationweather.gov/)** (NOAA) — METAR, TAF
  and the flight category, free and without a key.
- **[airplanes.live](https://airplanes.live/)**, **[adsb.lol](https://adsb.lol/)**,
  **[adsb.fi](https://adsb.fi/)** — community ADS-B feeds, and
  **[hexdb.io](https://hexdb.io/)** / **[adsbdb.com](https://www.adsbdb.com/)**
  for routes and aerodromes.
- **[OurAirports](https://ourairports.com/)** — the public-domain runway
  database behind the METAR compass.

Thanks to those projects and their authors. A word in particular for the
aviation data services above: they publish real operational data, for free, to
anyone — including a hobby project displaying it on a desk. This project would not 
exist without them.

## Licences

This project is distributed under **AGPL-3.0** (see [`LICENSE`](LICENSE)).

Some files ported from third-party sources keep their original licence notice
in their header; since they are compiled into the same binary, distribution of
the complete firmware is governed by the terms of the AGPL-3.0:

| Source | Licence | Files concerned |
|---|---|---|
| [esp32-eyes](https://github.com/playfultechnology/esp32-eyes) (Aitchison) / [ESP32_Faces](https://github.com/luisllamasbinaburo/ESP32_Faces) (Llamas) | AGPL-3.0 | `EyeRig`, `EyeConfig`, `EyeDrawer`, `Transitions`, `Animations`, `EyePresetsM5`, `EmotionRoulette` |
| StackChan firmware (M5Stack / meganetaaan) | Apache-2.0 | dance keyframes, Si12T driver (reference) |
