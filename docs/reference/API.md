> **English** · [Français](API.fr.md)

# The REST API

Every route the companion serves, what it is for, and the four conventions
that apply to all of them. The **exhaustive, always-current** description is
the OpenAPI the robot generates from its own code — `http://<ip>/swagger` to
read it, `http://<ip>/api/openapi.json` to feed a tool. This page exists
because that one needs a running robot, and because a list of routes does not
tell you the rules they all obey.

The table below is held against the firmware's routing table by
`scripts/gates/check-doc-coverage.py`: a route added without a line here fails
the gate, and a line here naming a route that does not exist fails it too.

## Four conventions

**Everything is a query parameter.** No JSON bodies except file uploads, which
are `multipart/form-data`. A `POST` with parameters in the query string is the
norm, not a shortcut — it keeps every call reachable from `curl`, from a
browser bar, and from Home Assistant's `rest_command` without a template.

**Errors are bilingual and structured.** `{"error":"…"}` with a real status
code — 400 for a missing or invalid parameter, 404 for a name that does not
exist, 409 when the robot cannot do it in its current state (no SD card, AP
mode), 503 while a job already holds the slot.

**Anything heavy is deferred.** An AsyncTCP callback may only post a command
or set a tuning field (rule A2.6), so SD writes, flashes, reboots and NTP
resyncs answer *immediately* and happen on the next pass of `loop()`. A `202`
therefore means "accepted", never "done" — and for the ones that matter there
is a separate endpoint to read back the outcome (`GET /api/rules` after a
reload, `GET /api/firmware` after an update).

**Route order is a real constraint.** ESPAsyncWebServer swallows `/api/x/y`
into the `/api/x` handler when `/api/x` is registered first. Every route goes
through `WebApi::route()`, and `checkRouteOrder()` denounces a bad order on
the serial console at boot. Three endpoints were broken this way before the
check existed.

## Authentication

Optional and off by default: with no password the API is open on the LAN. See
[`SECURITY.md`](SECURITY.md) before deciding that is acceptable for your
network — it is a robot with a camera and a microphone.

| Route | Method | What it does |
|---|---|---|
| `/api/security` | POST | Basic Auth on the fly, console and API. An empty password disables it. Persisted on the card |

## State and telemetry

| Route | Method | What it does |
|---|---|---|
| `/api/status` | GET | the general state: emotion, gaze, battery, WiFi, SD, uptime |
| `/api/sensors` | GET | the K151 extras: battery, INA226 bus voltage and shunt, LTR-553 light, BMM150 heading, IMU rest-pose calibration state (`imu_cal`: 0 calibrating / 1 done / 2 done on the deadline), NTP clock, night flag. Cached values — no I2C inside the callback |
| `/api/firmware` | GET | **which build is running**: `slot`, `sha`, `console`, `reset`. Read once, not polled |
| `/api/clock` | GET | wall clock: epoch, whether it is plausible, whether NTP really landed |
| `/api/clock/sync` | POST | force an NTP resync. 409 in AP mode |
| `/api/servo/pos` | GET | the **measured** head pose, as opposed to the commanded one. Only valid while `servos=0` — the servo bus is write-only in operation |

## Expression and movement

| Route | Method | What it does |
|---|---|---|
| `/api/emotion` | POST | hold one of the 30 expressions ([`EMOTIONS.md`](EMOTIONS.md)). Aborts a running dance |
| `/api/animation` | POST | `blink`, `winkLeft`, `winkRight` |
| `/api/dances` | GET | the dances available, built-in and from the card |
| `/api/dance` | POST | play one, or stop |
| `/api/servo` | POST | drive the head: absolute `yaw`/`pitch` or relative `dyaw`/`dpitch`, in degrees |

## The status band

The `sources → fields → widgets` contract; see [`STATUSBAR.md`](STATUSBAR.md).

| Route | Method | What it does |
|---|---|---|
| `/api/field` | POST | push one or more fields onto the blackboard. This is how any outside source feeds the band |
| `/api/statusbar` | POST | the band's mode |
| `/api/say` | POST | a scrolling notification, which interrupts whatever the band was showing |
| `/api/timer` | POST | the timer and pomodoro modes |
| `/api/rules` | GET | the rule table **as loaded**, which is not the contents of `rules.txt`: a line that fails to parse is simply absent |
| `/api/rules/reload` | POST | re-read `rules.txt` from the card |

## Settings

| Route | Method | What it does |
|---|---|---|
| `/api/tuning` | GET | every tuning key and its value |
| `/api/tuning` | POST | write keys on the fly. Persisted to the card **only when a value actually changes** |
| `/api/personalities` | GET | the character table AS THE ROBOT HOLDS IT — compiled entries and the ones read from `/stackchan-companion/personalities/*.yaml`, with each one's colour, console theme, roulette cadence and resting weights. It is also the only way to see from outside whether a card file was picked up: the loader otherwise reports on the serial line, which cannot be opened without resetting the board |
| `/api/personalities` | POST | create or edit ONE character — `name` plus any of `color`, `theme`, `rules`, `roulette`, `min_ms`, `max_ms`, `w`. The whole character travels at once, so the robot never merges two half-updates; an omitted field keeps its current value. `w=Normal:1,Happy:.6` is a DECLARATION — clearing an emotion is leaving it out. Written to the card from `loop()`, never from the network callback (A2.6) |
| `/api/personalities` | DELETE | remove a character by `name`. Refuses the default one, and refuses the ACTIVE one — switch away first, or the deletion would silently drop `personality` back to 0 |
| `/api/config` | POST | the runtime options that are not tuning keys, including the UI language |
| `/api/config/reload` | POST | re-read `config.yaml` from the card |
| `/api/wifi` | POST | the network credentials, persisted |

## The camera

Off by default (`camera=0`), initialised on demand, shut down after
inactivity — see [`../hardware/PERIPHERALS.md`](../hardware/PERIPHERALS.md).

| Route | Method | What it does |
|---|---|---|
| `/api/camera/still.jpg` | GET | a JPEG snapshot; `?full=1` for full VGA quality |
| `/api/camera/stream` | GET | an MJPEG stream, for Frigate or a browser |

## The SD card

| Route | Method | What it does |
|---|---|---|
| `/api/sd/list` | GET | list the card's known files, by category |
| `/api/sd/get` | GET | download one |
| `/api/sd/put` | POST | upload one (`multipart/form-data`) |
| `/api/sd/delete` | DELETE | remove one |
| `/api/dances/files` | GET | the dance CSVs on the card |
| `/api/dances/file` | POST | write one |
| `/api/dances/file` | DELETE | remove one |
| `/api/dances/reload` | POST | re-read `/dances/` |

⚠ Two 1.7 MB uploads back to back fail. Space them, and wait for
`/api/status` to answer between them.

## Guest binaries and firmware

The chain these drive is drawn in
[`../architecture/WORKFLOWS.md`](../architecture/WORKFLOWS.md) §12.

| Route | Method | What it does |
|---|---|---|
| `/api/bins` | GET | the launchable `.bin` files in `/bins/` |
| `/api/bins` | POST | upload one |
| `/api/bins` | DELETE | remove one |
| `/api/bins/launch` | POST | flash a guest and reboot into it. Deferred, answers 202 |
| `/api/bins/stop` | POST | from a **guest**: reflash `/companion.bin` and come back |
| `/api/update` | POST | OTA of the companion itself |
| `/api/reboot` | POST | clean restart, deferred ~1 s so the response leaves first |
| `/api/poweroff` | POST | full shutdown through the PMIC. Does not restart on its own |

## The console and its own documentation

| Route | Method | What it does |
|---|---|---|
| `/` | GET | the embedded console, served pre-compressed |
| `/swagger` | GET | the API browser |
| `/api/openapi.json` | GET | the OpenAPI document, generated from the firmware |
