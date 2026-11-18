> **English** · [Français](PERIPHERALS.fr.md)

# The parts, one by one

Each entry below states what the part is for, how it is reached, what had to be
calibrated or worked around, and **how it fails** — the last one being the part
that costs the most time when it is missing.

For what it costs to *talk* to any of these, see [`BUSES.md`](BUSES.md).

## SCS0009 servos — the neck

Two serial servos on UART2, **ID 1 = yaw**, **ID 2 = pitch**. Calibration
measured on the robot:

| Axis | Value | Source |
|---|---|---|
| Yaw centre | **166°** (raw 463) | measured — head facing forward |
| Yaw range | **±130°** | derived, see below |
| Yaw *reflex* range | **±40°** | deliberate, see below |
| Pitch HOME | **93°** | measured; was 103, raised to leave 10° of room to LOWER the head |
| Pitch min | **19°** = 85° official | M5Stack spec |
| Pitch max | **99°** = 5° official | M5Stack spec |

### Why ±130° when the head physically turns all the way round

This is the most counter-intuitive number on the robot, and the answer is that
**nothing mechanical stops it — our own command path does.**

Three facts, in the order they matter:

1. **The servo has no horizontal limit.** M5Stack's own StackChan page says it
   outright: *"No angle restriction is required for the X-axis"*. The
   restriction they do state, loudly, is the Y-axis one — and that one comes
   with a stall-and-permanent-damage warning. The SCS0009 is specified as
   360° continuous on the horizontal axis.

2. **Measured with the torque released, the head turns several full
   revolutions.** No resistance, no stop. The cable does **not** cross the yaw
   axis; an earlier note in this project guessed that it did, and that guess
   was simply wrong.

3. **What actually binds is `writeDeg`.** Our command path maps a `0–300°`
   span onto the SCS0009's 10-bit position register and clamps there:

   ```cpp
   if (deg < 0) deg = 0; if (deg > 300) deg = 300;
   uint16_t pos = (uint16_t)(1023.0f - deg * (1023.0f / 300.0f));
   ```

   With the centre at 166, the reachable envelope is therefore **+134 / −166**.
   The widest *symmetric* range that fits inside it is **±134**.

So ±130 is `±134` minus **four degrees of margin**, so that a commanded extreme
never lands exactly on the conversion clamp. There is no stall risk here —
nothing is being pressed against a stop, unlike the pitch limits — the margin
only keeps the arithmetic honest.

**To go further you must leave the 0–300 mapping altogether**, i.e. drive the
servo's multi-turn mode. That is a different protocol path, not a bigger
constant: raising `YAW_RANGE` past 134 buys nothing, because `writeDeg` clamps
first. The 360° you can turn by hand is real, and it is simply not addressable
through the position register we use.

### Why reflexes get only ±40°

`YAW_REFLEX_RANGE` is a **second, narrower** clamp, and it exists because
widening the first one broke something. Two reflex paths walk the yaw
*relatively*, with no bound of their own — sound tracking and the post-startle
turn both do `moveTo(yawDeg() + step, ...)`. At ±40 such a walk stopped after a
step or two. At ±130 a sustained off-axis noise carries the head 130° off
centre, leaving the screen turned away from the person it is supposed to face.

A dance *asks for* a big amplitude; a reflex should merely **look towards**
something. Absolute commands — dances, `POST /api/servo` — keep the full range.

### The pitch limits are the dangerous ones

The official M5Stack spec is *"Y-axis recommended within 5 ~ 85°. Operating at
extreme angles may cause servo stall and permanent damage."* Their frame is the
inverse of our measured raw frame (`raw ≈ 104 − official`), which gives our
19..99. The **old bounds of 15/103 held the mechanical end stop** during the
NOD, SHY and cry dances — that is a stalled servo, and stalled servos die.

Never hold a pitch end stop. `PITCH_DOWN_MAX` (= 6) is derived from these
bounds and must never be hardcoded elsewhere: beyond it the target exceeds
`PITCH_MAX`, `ServoMotion` clamps it, and the head-at-home rest condition never
converges — a servo re-command loop.

### Torque, and the release a reboot cannot undo

`EnableTorque(id, 0)` is a register **inside** the servo, and the SCS0009 power
up with their torque **enabled**. So a release survives only until the next
reset: hand the robot to a guest bin, the reboot re-asserts VM_EN, the servos
come back energised, and the carefully acknowledged release is erased.

The only release that sticks is cutting **VM_EN** on the PY32, because the PY32
keeps its GPIO state across our reset.

## BMI270 — the IMU

Read through `M5.Imu.getImuData()` — **never** `getAccel()`. `hal/ImuReader.h`
delivers `headVel` already expressed in °/s in **screen axes with
viewer-centric signs**, so the VOR has no mapping left to do.

The mapping is **validated on hardware**, and every axis and sign is re-tunable
at runtime without reflashing (`gyro_yaw_axis/sign`, `gyro_pitch_axis/sign`;
the console has "VOR calibration" buttons and the telemetry exposes
`gX/gY/gZ`).

| Sensor | Screen axis | Validated default |
|---|---|---|
| `accel.x` | X (left/right tilt) | +(accX − rest baseline) × `TILT_SENS_X` 0.67 |
| `accel.y` | Y (front/back, carries gravity at rest) | +(accY − rest baseline) × `TILT_SENS_Y` 0.86 |
| *physical direction of those two* | **+X = the observer's right, +Y = the top of the screen** | measured 2026-08-22 with `led-fluid`: a liquid falls where gravity points, so the gravity vector in screen pixels is `(-accel.x, +accel.y)` |
| `gyro.y` | yaw rotation (world vertical = sensor Y) | + (`gyro_yaw_axis=1`, `sign=+1`) |
| `gyro.x` | pitch rotation | + (`gyro_pitch_axis=0`, `sign=+1`) |
| `accel.z` | screen normal (= gyro roll axis) | raw, threshold ±0.75 g → face up/down (`ImuReader::FaceOrient`), sign validated on HW |

⚠ **The in-plane accelerometer SIGNS had never been read directly** before that
measurement, and the distinction matters. `accel.z` has always had one, from the
face-down gesture. The other two were validated as *behaviour* — "static tilt
held" (`validation/VALIDATION.md`), i.e. the companion's tilt target stays put on
a slope instead of decaying to zero — which says nothing about its direction: a
target held the wrong way is held just as firmly. Watching which way a liquid
falls is the first test on this robot that reads them directly.

What it settles, and what it does not: the physical axes are now known, so
`ImuReader::_tilt` can be reasoned about instead of guessed at. Whether its
signs are the ones the *behaviour* wants is a separate question — a true
otolith response counter-rotates (tilt right → eyes left), a cartoon one drifts
with the fall, and the two want opposite signs. The two axes used to answer it
differently: X counter-rotated while Y followed the fall, an asymmetry nothing
could have caught while the physical directions were unknown. **Both
counter-rotate now**, which is what this class's own documentation always
claimed and what the vestibulo-ocular reflex does. Tipped back, the gaze goes
down with the head.

Both in-plane axes also carry a **rest-pose baseline**, and X had none before —
invisible precisely because `accel.x` is nominally zero upright, so any mounting
bias in X went straight into the gaze as a permanent off-centre offset that no
calibration could remove. The baseline is measured over **quiet** ticks at boot
(a robot booted in somebody's hand no longer calibrates the hand), and
thereafter tracked only while the reading stays *near* it: thermal drift wanders
around the rest pose, a held tilt leaves it, so only the first is followed. A
robot that rests on a slope still calibrates to that slope, which is the correct
answer — that slope is its rest pose. `/api/sensors` publishes `imu_cal`:
`0` still calibrating (the tilt channel reads zero), `1` calibrated, `2`
calibrated **on the deadline** — the twenty-second fallback fired because no
quiet tick ever arrived, so the rest pose is a guess made while the robot was
moving. A gaze that sits off-centre has its explanation there rather than in the
VOR.

⚠ **`gyro.z` is the screen normal, i.e. ROLL.** Mapping yaw onto it makes the
VOR go mute on rotation — the classic misconfiguration on this board.

Servo efference: the VOR stays active during dances and head-follow. Only
*shake detection* is inhibited while the motion is self-generated (the
`selfMotion` guard) — without it, every dance would trigger `Scared`.

**Double-tap is detected in software** (a peak in `|accel|`): M5Unified does
not expose the BMI270's hardware tap interrupt.

## BMM150 — the magnetometer, and why it is unused

Reached through the same `M5.Imu` (`imu_data_t.mag`). `ImuReader::headingDeg()`
publishes an absolute 0–360° heading in telemetry.

**It is not fused into the VOR, and it will not be.** The verdict is ❌ and it
was measured, not assumed: the sensor mostly sees the **body's own servo
magnets** through a steep gradient — **370 µT of head-yaw dependence per 80° of
movement**, six times Earth's field. Worse, the reading is **irreproducible at
an identical commanded pose** (±100–175 µT between repeats: servo slop and
hysteresis), with ~50 µT steps depending on torque state, and dancing triples
the noise.

A pose-map calibration cannot converge on that irreproducibility. The code and
its three native tests stay, `vor_mag_alpha` stays **0**, and the only path
left is an **external** magnetometer mounted away from the motors (Grove).

## AXP2101 — the PMIC

Reached through `M5.Power` on the *internal* bus, not `Wire1`. It powers the
display and the SD card, which is why it must come first at boot.

- `getBatteryLevel()` — 0–100, or −1 when the PMIC cannot answer.
- `isCharging()` / `getVBUSVoltage()` — tell mains and USB apart from a plain
  level reading. VBUS above 3000 mV counts as present; absent reads ~0–100 mV.
- `setExtOutput(true)` — required on the K151, which has no takao base.

⚠ **Never call `setBrightness()` per frame.** It is an I2C transaction on the
shared bus. Brightness has exactly one target, computed in one place (sensor
*or* manual) and then posted to the renderer.

## INA226 — the battery gauge

`Wire1`, address `0x41`, detected by the Texas Instruments manufacturer ID
(`0x5449`). It measures the **bus voltage** at the battery node (1.25 mV/LSB)
and the **shunt voltage** (2.5 µV/LSB, signed, proportional to current).

The bus voltage is usable **without calibration**, unlike the current register,
so that is what is exposed — as a reading that *complements* the AXP2101
estimate rather than replacing it. The AXP2101 remains authoritative for
battery level; the INA226 voltage sags under load and is indicative only.

**Failure convention: `busVoltage()` returns −1, never 0.** A caller has to be
able to tell "no reading" from "flat battery".

## LTR-553ALS — ambient light

`In_I2C`, address `0x23`. The reason this part needs a whole section is that it
is **nearly occluded by the K151 enclosure**: a lit desk reads **0 counts at
gain 1×** and **2 counts at gain 8×**.

The calibration that makes it usable is three numbers that only work together:

| Setting | Value | Meaning |
|---|---|---|
| `ALS_CONTR` | `0x1D` | gain **96×**, active |
| `ALS_MEAS_RATE` | `0x1B` | 400 ms integration on a 500 ms rate |
| level curve | `ln(4096)` | normalised on the range that init actually produces |

Change one and the other two are wrong. That is why the calibration lives once
in `firmware/common/Ltr553.h`, shared by the companion and the guest bins.

What is deliberately **not** shared is the transaction policy, because it
genuinely differs: the companion reads one register per `Guard` so the Brain
can slip its 100 Hz IMU reads between the bytes, while a guest bin — which has
no Brain and no bus lock — reads the four data bytes in one burst.

**Failure convention: −1, never 0**, because 0 is a legal reading meaning
darkness. Getting this wrong once put the robot to sleep in broad daylight: a
failed read was taken as "dark" and `dark_sleepy` did the rest.

## Si12T — head touch

`Wire1`, address `0x68`, 3 zones, ported from the vendor firmware. Sensitivity
is a level 0–7 (default 3).

`OUTPUT1` (`0x10`) packs the three zones as 2 bits each — rear, middle, front —
with values 0 = none, 1 = low, 2 = mid, 3 = high. Position is a centroid:

```
pos = (-100 × ch0 + 0 × ch1 + 100 × ch2) / (ch0 + ch1 + ch2)
```

giving −100 at the rear, 0 in the middle, +100 at the front, which is what
turns a stroke into `SWIPE_FORWARD` / `SWIPE_BACKWARD`.

**Init order is mandatory**: `Wire1.begin(12,11)` → PY32 VM_EN (300 ms) →
servo → Si12T. `hal/Board.h` enforces it.

`Wire Error 263` on the serial console is a **recurring Si12T timeout** and is
benign — known log noise, not a fault to chase.

## PY32 — IO expander: the servo rail and the LEDs

`Wire1`, address `0x6F`. It does two unrelated jobs.

**VM_EN (PY32 GPIO 0)** is the servo power rail: direction → pull-up → output
HIGH, then a 300 ms settle. It must be on before `servo.begin()`. The chip
boots slowly (~200 ms), so `detect()` retries the version read for up to 1.2 s
before declaring it absent — a version of `0x00` or `0xFF` means "not there".

**Twelve WS2812C LEDs** hang off PY32 GPIO 13 (bit 5 of the `_H` registers).
The protocol is not documented publicly; it was read out of the vendor
firmware:

| Register | Role |
|---|---|
| `0x24` `REG_LED_CFG` | bits 0–5 = LED count, bit 6 = REFRESH |
| `0x30+` `REG_LED_RAM` | 2 bytes per LED, **little-endian RGB565** |

Sequence: `setLedCount(12)` → write the colours → `refreshLeds()`.

**The two bars run PERPENDICULAR to the display**: each one's six LEDs are
staggered front to back, not across the robot's width. That is the fact any
per-LED animation has to be designed against — the axis available is DEPTH.
Gaze direction and an eyelid's travel are left/right and up/down, so neither
has anywhere to be drawn here; what does map is a sweep along the body, a
level, a progress reading, or a forward/backward tilt. It also decides who sees
what: from the side all six are legible, from the front they mostly stack up
into a single brightness.

**Each of the twelve takes its own colour**, and always could: the colour RAM
is twelve entries wide, one per LED. Only the driver used to pretend otherwise,
offering all-twelve-alike and **two bars of six** (0–5 left, 6–11 right,
viewer-centric) — the bars being what the companion wants, since it tracks each
bar's brightness to the height of the matching eye. `setLedsRaw()` writes the
twelve individually; both helpers are now callers of it. The `led-fluid` guest
uses that to split the screen into twelve vertical slices and give each LED the
colour of the busiest cell in its slice.

An earlier `[0xAA, r, g, b, lum]` protocol was guesswork and plain wrong — it
**ACKed on I2C without lighting anything up**, which is worth remembering: on
this bus, a write being acknowledged proves only that a chip is there.

LED colour is always `Renderer::eyeColorRgb()` — the colour actually being
displayed, transition included — never recomputed on the LED side.

**A WS2812 latches, so the ring is ASSERTED at boot and not merely driven.** It
keeps its last colour until something writes another one, across a reboot and
across a reflash — there is no power-on default to fall back on. The runtime
path only blacks the LEDs when it was itself lighting them, which says nothing
about what the *previous* firmware left behind: a companion starting with the
`leds` option off would never write the ring at all, and a colour burnt in by a
guest bin would simply stay. So the companion writes the known state once at
startup, whatever the option says. It is the firmware that owns the hardware,
and a guest cannot be relied on to clean up after itself — it may be a
third-party binary, or one a watchdog killed before any cleanup of its own could
run.

## GC0308 — the camera

VGA sensor on the DVP bus, control over SCCB. Two hardware obstacles had to be
solved before a single frame arrived.

**1. The SCCB bus is the IMU's bus.** M5Unified drives pins 11/12 with its own
register implementation (`m5gfx::i2c`), *not* the ESP-IDF I2C driver that
esp32-camera expects for SCCB — so SCCB timed out (error 263).
`firmware/companion/sccb_m5.cpp` **redefines** esp32-camera's `SCCB_*`
functions to route the control bus through `M5.In_I2C`, linked with
`-Wl,--allow-multiple-definition`. Camera and IMU then serialise on the same
i2c and the VOR survives streaming.

**2. There is no hardware JPEG.** The GC0308 only outputs RGB565/YUV; asking
for `PIXFORMAT_JPEG` returns `ESP_ERR_NOT_SUPPORTED` (262). So capture is
RGB565 and JPEG encoding is done in software (`frame2jpg`) at serving time.

Pins: `XCLK` is nominally G2 but **unused** — the sensor runs off an external
20 MHz crystal. Data is `D0–D7` on 39/40/41/42/15/16/48/47, with `VSYNC` 46,
`HREF` 38, `PCLK` 45. No `PWDN` and no `RESET` pin is wired, which is why init
begins with a soft reset (`0xfe, 0x80`): the sensor's register state
**persists** across our reboot.

The renderer is paused around the DMA fill of a frame. Without it the display
SPI-DMA and the camera GDMA arbitrate badly and the camera FIFO overruns
mid-frame.

**Anti-brick discipline**: off by default (`camera` tuning key), init deferred
until requested (never at boot), failure **latched** so there is no retry loop,
and de-init after inactivity to free PSRAM and leave the bus idle.

## ES7210 microphones and AW88298 speaker

Stereo capture through `M5.Mic.record(..., stereo = true)`; playback through
`M5.Speaker`. They share I2S1 — the arbitration rules are in
[`BUSES.md`](BUSES.md) §3 and they are not optional.

The `sound_track` option turns the head towards noise, and deliberately mutes
itself while the servos are moving: otherwise the robot chases its own gear
noise. Off by default.

## BM8563 — the RTC

`M5.Rtc.isEnabled()` / `getTime()`, same internal bus. It exists here for one
job: knowing the real time so the night volume drop and the night theme follow
**true sunset and sunrise** (computed from latitude/longitude by
`firmware/common/SunClock.h`) rather than a fixed clock window.

## Not implemented

| Part | Address / pins | State |
|---|---|---|
| ST25R3916 (NFC) | `0x50` | pins and address known; needs a dedicated library and a hardware session |
| IRM56384 (IR) | RX G10, TX G5 | same |

Both are opportunities rather than gaps: nothing in the firmware depends on
them, and their specifications are written up in `ROADMAP.md` so the traps are
paid for in advance.
