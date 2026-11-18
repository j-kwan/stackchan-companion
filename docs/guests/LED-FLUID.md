> **English** · [Français](LED-FLUID.fr.md)

# led-fluid — liquid in a box

A guest `.bin` that turns the CoreS3 into a liquid you tilt. A particle fluid
runs under the glass, its gravity IS the inclination of the board, and it is
painted as a grid of round dots — the look of an LED matrix. Shake it and it
splashes; hold it still and it settles.

Nothing about it needs the network: the WiFi is there because `SceGuest` brings
it, and because a settings page is easier to type into than a 320 px panel.

## The screen

The fluid area is the whole panel, 320×240. Each cell of the grid is one dot,
and a dot says two things at once:

| What you see | What it means |
|---|---|
| Saturation — pale to full colour | **Density**: how much fluid piled up in that cell |
| Value — dark to bright | **Speed**: how fast it is moving there |

So a deep still pool reads as strong dark colour, and fast thin spray reads as
pale and bright — the white-hot highlight the eye already expects from a
splash. The hue and a saturation **ceiling** are yours (swipe left); the fluid
only ever moves under that ceiling.

A **trail** setting fades a cell rather than clearing it, which is what makes a
moving drop leave a streak instead of a stroboscopic dotted line.

## The gestures

| Gesture | What happens |
|---|---|
| **Tilt the robot** | gravity follows — this is the whole point |
| **Shake it** | a splash; rotate it and the fluid swirls |
| **Tap the fluid** | an impulse at your finger, pushing the fluid away |
| **Swipe right** | the settings panel: physics, then the render switches |
| **Swipe left** | the colour rectangle: hue, saturation, brightness |
| **Swipe left or right in a panel** | back to the fluid (either direction) |
| **Long swipe down** | back to the companion (the `SceGuest` exit) |

**The exit is switched off while a panel is open.** A slider dragged downwards
would otherwise offer to reflash the robot, which is the most expensive thing a
misread gesture can do here.

## The settings

The panel, the web page and the card all read **one table**. Adding a setting
is adding a row of it; none of the three can drift from the others.

### Swipe right, tab PHYSICS

| Key | Range | Default | What it does |
|---|---|---|---|
| `particles` | 60-400 | 240 | how much fluid there is — and the main CPU cost. Capped by `dot_pitch`: grains that cannot be kept apart are not created |
| `dot_pitch` | 8-20 px | 12 | the grid pitch, and the grain size with it: small dots, fine fluid, more of it |
| `viscosity` | 0-100 | 30 | water at the bottom, honey at the top |
| `gravity` | 0-200 % | 100 | 0 is weightlessness, and it is worth trying |
| `bounce` | 0-90 % | 25 | how much of a wall impact comes back |
| `trail` | 0-95 % | 60 | how long a dot takes to fade once the fluid has left |
| `gyro_gain` | 0-200 % | 100 | how strongly rotation becomes a swirl |

Under the sliders, five **presets** — water, honey, mercury, lava, weightless.
They set the physics and **never the hue**: a preset that undid the choice made
on the panel next door would be one control fighting another.

### Swipe right, tab RENDER

Six switches, every one **off** by default — like every intrusive automation in
this repository, and, for the last three, because off is the mapping the robot
actually needs:

| Key | What it does |
|---|---|
| `leds` | the twelve WS2812 of the K151 echo the fluid — twelve angular sectors, each taking its brightest cell, through the same colour mapping as the dots |
| `sound` | a short chirp on a splash |
| `auto_bright` | the panel brightness follows the LTR-553 ambient light sensor |
| `tilt_swap` | exchange the accelerometer's two axes |
| `tilt_inv_x` | invert the horizontal one |
| `tilt_inv_y` | invert the vertical one |

The three tilt switches are here rather than buried in the card because the
person who can see which way the fluid runs is **holding the robot**, not
reading `/config` on a laptop. Tilting it and tapping a switch is one gesture;
the alternative was a reboot per hypothesis.

The LED and light-sensor chips are **probed at boot**, never assumed. On a board
where one is absent or dead, its switch stays available but does nothing, and
the serial line at 115200 says which of the two answered.

**The ring is always put out, three times over**, and each one is a case where
it stayed lit: a WS2812 latches its last colour and keeps it across a reboot and
across a reflash, so a bin that merely stops writing leaves twelve lit LEDs on
a robot that has moved on. They are blacked out when the option is switched
**off** (not just ignored), when the bin **hands control back** to the companion
by any of its exits, and once at **boot** — that last one because configuring
the expander's data line is itself enough to make an unwritten chain latch
noise, which showed up on the robot as twelve LEDs stuck white.

### Swipe left, the colour rectangle

X is the hue, Y is the saturation **ceiling**, saturated at the top. Under it,
the screen brightness, and a row of six sample dots showing what the choice will
actually look like as fluid — from an empty cell to a packed one. The rectangle
shows the choice; the swatches show the consequence.

### Not on any panel

`hue`, `sat` and `bright` belong to the colour rectangle. They are still saved
and still appear on `/config` — they simply have no second widget on the
settings panel, because two controls writing one value disagree on screen the
moment somebody moves only one of them. That is the whole membership rule for
this list: **something that already has a widget**.

### The tilt mapping

**All three are off, and a correct robot shows them off.** The mapping the K151
actually needs — `gx = -accel.x`, `gy = +accel.y` — was measured on the robot
standing upright and lives in the code that reads the vector, not in the default
of a switch. Carried as `tilt_inv_x = 1` it behaved identically and read
terribly: the settings opened on a robot that was working correctly while
announcing that an axis had been inverted, so the one honest state of the
machine looked like somebody had already been fiddling with it. A switch has to
mean *depart from what is right*.

They stay tunable because a K151 is not the only body this could end up in: if
the fluid runs the wrong way on yours, it is a yaml edit and not a rebuild.

The project did already have an accelerometer mapping
([`hardware/PERIPHERALS.md`](../hardware/PERIPHERALS.md)) — what it did not have
was a check of the in-plane **signs** against a known physical direction. Only
`accel.z` carried one, from the face-down gesture. `accel.x` and `accel.y` were
validated as *behaviour* — the companion's tilt target is held on a slope rather
than decaying — which is a different claim: a target held in the wrong direction
is held just as firmly. Watching which way a liquid falls is the first test on
this robot that reads those two signs directly.

## The configuration file

`/stackchan-companion/ledfluid.yaml`, flat, one key per line — the same keys as the
table above. The bin **rewrites the whole file** on every save: values survive,
hand-written comments do not.

```yaml
particles: 240
dot_pitch: 12
viscosity: 30
gravity: 100
bounce: 25
trail: 60
gyro_gain: 100
hue: 200
sat: 85
bright: 120
leds: 0
sound: 0
auto_bright: 0
tilt_swap: 0
tilt_inv_x: 0
tilt_inv_y: 0
```

Build, deploy and launch — the robot running its companion firmware:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e led-fluid
curl -X POST "http://<ip>/api/bins" -F "file=@.pio/build/led-fluid/firmware.bin;filename=led-fluid.bin"
curl -X POST "http://<ip>/api/sd/put?path=/stackchan-companion/ledfluid.yaml" -F "file=@sdcard/stackchan-companion/ledfluid.yaml"
curl -X POST "http://<ip>/api/bins/launch?name=led-fluid.bin"
curl -X POST "http://<ip>/api/bins/stop"      # once the guest has started
```

Settings entered with no card apply and are lost at the next boot; the no-card
screen says so before the fluid ever draws.

## The physics

The solver is a **particle viscoelastic fluid** (double density relaxation).
It carries no iterative pressure solve, stays stable at a large time step, and
takes viscosity as an explicit parameter rather than as a side effect — which
matters, because viscosity is the knob this bin is about. Neighbours come from
a uniform grid, so the cost is linear in the particle count.

**A grain is a dot, and no two grains stand in the same place.** The particle
diameter is the dot pitch, so one grain lights one LED; the fluid holds itself
apart with a non-penetration pass that runs after the walls, as the last word on
where a grain is. Pressure alone cannot make that promise — it is a restoring
force, so it always yields a little under gravity, and a little at 240 grains is
a column of dots stacked on one another. A separation of one diameter is a
constraint instead, and gravity has nothing to trade against it.

That constraint cannot invent room, so the **grain count is capped by the dot
pitch**: a coarse grid means fewer grains, and the panel's slider is a wish that
`count()` answers with what the screen can actually keep apart. At 20 px the 400
maximum becomes 153.

Every length in the solver is a multiple of the dot pitch and every stiffness is
an acceleration in px/s², so each can be compared with the one number that
decides whether a liquid holds itself up: gravity, 900 px/s² at 100 %. Nothing
in it is expressed in the units of the paper it came from.

The solver is a pure header with no Arduino in it, and it is tested on the PC
(`test/test_fluid`): particles never leave the box, a violent shake never
tunnels through a wall, ten thousand steps produce no NaN, the same seed
produces the same film, more viscosity dissipates more energy, the settled fluid
keeps a **volume** — a body several dots deep, not a line along one edge — and no
two grains sit closer than three quarters of a diameter.

**The cost of a step is capped**, and that is not an optimisation. A neighbour
grid bounds the pair count only while the particles are spread out, and gravity
spends its whole life doing the opposite: once a pile fits inside one cell,
every pair is a neighbour again and the step is quadratic after all. So the
neighbours examined per particle are capped at 24 — about twice a full
neighbourhood at rest density, so ordinary fluid never notices while a
collapsed pile is approximated rather than integrated exactly.

## The frame budget

Two surfaces, on purpose. The fluid is painted **straight to the display**,
cell by *changed* cell: a cell whose quantised appearance has not moved is not
drawn again, which is what makes the frame fit. The panels are composed in a
PSRAM sprite and pushed once, because they change rarely and a sprite is what
the shared deferred-text helper draws into.

Measured on hardware at the finest pitch (8 px, a 40×30 grid), with gravity
driven in a circle so the fluid never settles:

| | |
|---|---|
| Worst paint | 16-18 ms |
| Worst loop | 26 ms, with an occasional 38 ms outlier from the network stack |
| Painted frames | 50 per second |

That is why the pitch floor is 8 px and not 6: at 6 px the grid is 2120 cells
instead of 1200 and the budget breaks.

The simulation runs on **core 0** and touches no bus at all — every I2C
transaction in this bin happens in `loop()` on core 1, which is what lets it
skip the shared-bus lock entirely rather than add one more lock to hold
correctly. It also **yields unconditionally**: a step that outruns its period
re-syncs its deadline and serves a one-tick floor, because a task that only
yields when it finished on time stops yielding at all the first time it does
not, and takes the idle task of its core down with it.

## Design notes

- **Three pure headers, tested natively** like `engine/` and `behavior/`
  (rule 7): `fluid.h` (the solver), `look.h` (colour mapping and presets),
  `panels.h` (panel geometry, hit-test, slider maths). `main.cpp` keeps only
  what touches the hardware.
- **One `PARAMS[]` table, four consumers.** Key, EN/FR label, kind, bounds and
  step live once in `panels.h` and are read by the panel drawing, the touch
  hit-test, the yaml load/save and the `addSetting()` calls of `/config`, so a
  parameter cannot exist on the panel and be missing from the web page.
- **The simulation hands frames to `loop()` through a two-buffer mailbox**:
  one producer, one consumer, the Brain/Renderer discipline of A2.5 rewritten
  in the bin (a guest does not include `src/engine/`). `sce::CoopStop` is wired
  on that task with a short `windowMs` (2000): a step is under a millisecond,
  there is no long request to drain.
- **Repaint with hysteresis**: a cell is redrawn only when its quantised colour
  changed, with a margin so it cannot flicker between two values. `fillCircle`,
  never an anti-aliased primitive, one draw call site per shape (A2.22, checked
  inside the ELF by `check-a222.py`).
- **The downward exit is neutralised while a panel is open**: the bin's
  gestures live below `EXIT_PX` (`common/Gesture.h`), and without that guard a
  slider dragged downwards would reflash the robot.
- **The WS2812 echo** needs per-LED addressing. The pure half (`scale565`, the
  12×RGB565 payload) is `firmware/common/Py32Leds.h`, shared with the
  companion; the `Wire` transaction stays on each side (the companion under
  `i2cbus::Guard`, the bin without, all of its I2C being in one task). Same
  split as rule 17: computation shared, bounded I/O local.
- **Presets set the physics only**, never the hue: a preset that undid the
  colour panel next door would be one control fighting another.
- **The settings write is synchronous**, unlike the radar: it is short, holds
  no lock, and the simulation runs on the other core, so `onSettingsSaved`
  returns the real result and a full card gives an honest `?ko=1`.

Open questions:

- The accelerometer-to-screen mapping is validated for the gyro rates
  (`CONVENTIONS §3`) but not for the gravity vector, hence the three runtime
  `tilt_*` keys. Confirming the signs needs someone holding the robot.
- A step costs more wall-clock than its arithmetic explains (25-30 ms for
  8 000-10 000 pair evaluations). The likely cause is preemption: the task sits
  at priority 1 on core 0 under WiFi and lwIP. Harmless (50 fps), but the
  number should not be quoted as a cost until the two are separated.
- The 400-particle ceiling is bounded by the neighbour cap rather than measured
  on its own.

## Second board

`led-fluid-fire` — the **same source**, as a standalone application on an
M5Stack Fire: no K151, no companion, three buttons instead of a touch panel.

```powershell
pio run -e led-fluid-fire -t upload --upload-port (.\scripts\dev\find-port.ps1 -Board fire)
```

This bin was long described as unportable *because its UI is built on sliding*
— a swipe opens a panel, a drag moves a slider, a tap pokes the liquid where you
touched. That was true of the **interface**, not of the bin. So the port does
not put the same gestures on buttons; it says what each gesture **meant** and
maps that onto what a Fire has. The vocabulary is
[`firmware/led-fluid/input.h`](../../firmware/led-fluid/input.h), pure and
natively tested (`test_fluidinput`), and `applyEvent()` in `main.cpp` is its
only consumer — touch and buttons are two producers, exactly as on the radar and
the space bin.

The mapping does not depend on the screen you are on, which is what makes it
memorable:

| | short | long |
|---|---|---|
| **A** | previous / decrease | open the colour rectangle |
| **B** | act | back to the fluid |
| **C** | next / increase | open the physics panel |

"Walk" and "act" are roles, and each screen spends them once: on the settings
panel A and C move the value under the cursor while B steps to the next row (and
wraps into the other tab — with no dedicated tab button, that wrap *is* the tab
control); on the colour rectangle A and C turn the hue and B steps the
saturation; on the fluid B pokes it. A button build draws a highlight around the
row it is pointing at, because three buttons acting on an invisible selection is
not a control at all.

**One thing genuinely differs between the two boards, and the vocabulary says so
rather than hiding it**: poking the liquid needs a point, and a button has none.
A tap pokes where the finger landed; a button press pokes the centre.

What the Fire has and has not: it does have an IMU (MPU6886), so the tilt — the
whole point of the bin — works. It is mounted differently from the CoreS3's,
which is exactly what the three `tilt_*` switches are for. It has **no WS2812
ring**: that lives on the K151 body behind a PY32 expander, and the profile
declares its absence with `SCE_HAS_PY32=0` rather than probing for it. That is
the one capability here that is a *flag* and not a probe, and the reason is
sharp — probing means calling `Wire1.begin(12, 11)` first, and on a classic
ESP32 the GPIO 6-11 are the **SPI flash**. Pin 11 is not a free pin there, it is
the one the program is being read from. A capability that cannot be asked about
safely has to be declared. The light sensor stays probed (`M5.In_I2C`), as in
the space bin.
