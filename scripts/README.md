> **English** · [Français](README.fr.md)

# `scripts/` — the gates, the chores, the build step

One command sits at the top and the rest is sorted by **who runs it**, because
that is the question you actually have when you open this directory.

```
scripts/
  check-all.ps1     the single entry point — run this
  gates/            what check-all runs, and nothing else
  dev/              what YOU run by hand during a session
  build/            what PlatformIO runs on its own
  release/          what the maintainer runs to cut a release
```

## `check-all.ps1` — the one command

```powershell
.\scripts\check-all.ps1          # everything: gates, 37 native suites, eight firmwares, A2.22
.\scripts\check-all.ps1 -Fast    # skip the firmware build and the A2.22 pass
.\scripts\check-all.ps1 -Hook    # install it as the git pre-push hook, run nothing
```

## `gates/` — run by `check-all`, and by nothing else

That is the rule, and it is what makes the directory readable: a file in here is
a file the gate runs. Adding a checker means adding a `Step` line; a checker
that no `Step` calls is dead weight pretending to be a guarantee.

| Gate | What it refuses to let through |
|---|---|
| [`check-doc-parity.py`](gates/check-doc-parity.py) | an English document without its `.fr.md` twin, or a twin whose headings drifted |
| [`check-contrast.py`](gates/check-contrast.py) | a theme colour that fails the readability threshold on the panel — the flight-radar and space themes, and the **Liquid Glass** palette shared by the launcher, the guest lobby and ha-remote, judged against the CARD it is drawn on. Three files claimed `RGAA ≥ 4.5:1` in a comment and none of the three was ever measured |
| [`check-vendored.py`](gates/check-vendored.py) | one of the **five** copies vendored inside `SceGuest.h` (`Yaml.h`, `I18n.h`, `FirmwareInfo.h`, `Trace.h`, `Gesture.h`) drifting from its `firmware/common/` original. The stub has to stay copyable alone into a third-party project, which is the only reason those copies exist — and the reason they must be policed |
| [`check-mirrors.py`](gates/check-mirrors.py) | a fact written twice that stopped agreeing: the servo bounds the choreography editor restates by hand, `MIN_SUITES`/`MIN_TESTS` across the `.ps1`/`.sh` twins, the board USB VID/PIDs, the `rules.txt` template against its C-string literal, every console slider default against `Tuning.h`, and the two `CLAUDE.md`. It also enforces two rules that are not constants at all: a guest `main.cpp` calling `xTaskCreate` **must** wire `guest.netGuard`, and one calling `SD.begin` **must** hold an `sce::SdWatch` — the cooperative stop is a discipline that was applied to one bin and silently missing from another, so it is checked rather than remembered |
| [`check-console.py`](gates/check-console.py) | a `Tuning` key with no control in the embedded console: served by the API, so it looks finished, and unfindable for anyone who does not open Swagger. It covers the **79** keys of `Tuning::table()`, with exactly two named exemptions (`band_mode`, driven by `/api/statusbar`; `cfg_version`, the internal schema number) — the floor moves on its own when a key is added |
| [`check-guest-config.py`](gates/check-guest-config.py) | a guest setting that falls out of one of the four hand-written lists it has to appear in: `addSetting` declares it, `settingGet` shows it, `settingSet` accepts it, `saveConfig` writes it. The fourth is the dangerous one — `saveConfig` **truncates** the file and rewrites it from its own list, so a key the yaml can be read from but the save never writes is not merely unpersisted, it is DESTROYED on the first save from `/config`, hand-edited value included. That is the 08-05 `auto_bright` outage, whose lesson had been written in a comment and never tooled. It also counts the fields against `MAX_SETTINGS`, which only announces its own ceiling on the serial console. Two named exemptions: `track` (a command, not a setting) and `ll2_poll_min` (an accepted alias rewritten under its canonical name) |
| [`check-doc-coverage.py`](gates/check-doc-coverage.py) | a document that ENUMERATES what the code defines and stopped matching it: the 44 method+path pairs `WebApi::route()` registers, against `docs/reference/API.md`, and the 30 `eEmotions` values against `docs/reference/EMOTIONS.md`. Both directions — a missing line hides a route, an extra one promises an API that does not exist |
| [`check-a222.py`](gates/check-a222.py) | a shape drawn from two call sites in the compiled binary (A2.22) |

Two files in here are **not** in that list, on purpose:

- [`check-comments-only.py`](gates/check-comments-only.py) proves a change
  touched comments *only*, so it exits 1 as soon as code changed — the normal
  case for any real commit. It is a tool for one kind of sweep (a translation, a
  rule renumbering), not a gate. It was in the list once and made the gate fail
  on its own first run.
- [`test-native.ps1`](gates/test-native.ps1) runs the native suites on their own
  when you want them fast; `check-all` runs `pio test` itself so it can assert
  the suite and case COUNTS — **37 suites, 427 cases** (`$MIN_SUITES`/
  `$MIN_TESTS`) — which `pio`'s exit code does not: it exits 0 on everything it
  ran, and says nothing about a suite that stopped being picked up.

```powershell
.\scripts\gates\test-native.ps1                 # all 37 suites
.\scripts\gates\test-native.ps1 test_soundviz   # one of them
```

## `dev/` — run by hand, against a board

| Script | Use |
|---|---|
| [`find-port.ps1`](dev/find-port.ps1) | resolve a serial port **by USB VID/PID**, never by number. With two boards attached, an upload without `--upload-port` picks one on its own and can overwrite the companion |
| [`test-rules.ps1`](dev/test-rules.ps1) | exercises the rule file the robot is ACTUALLY running: it reads `/api/rules` rather than hard-coding a list, so it follows a personality switch and an edited file. It **skips the sensor-backed fields by name and says why** — `loop()` republishes them about once a second, so a pushed `batt=3` is gone before any sustain can see it, and a PASS on one of those would be reporting on the sensor, not on the rule |
| [`endurance-log.ps1`](dev/endurance-log.ps1) | long run: samples `/api/status` and flags heap or stack drift |
| [`test-mag.ps1`](dev/test-mag.ps1) | exercises the magnetometer over HTTP — opening the serial port resets the board over native USB, which is how the first session lost half its rotations |
| [`statusbar-push.ps1`](dev/statusbar-push.ps1) · [`.sh`](dev/statusbar-push.sh) | pushes a field to the robot's status band — Windows, macOS and Linux |
| [`claude-statusline.ps1`](dev/claude-statusline.ps1) · [`.sh`](dev/claude-statusline.sh) | the Claude Code statusline bridge, on the three platforms — the `.sh` is written to bash 3.2 so macOS runs it unchanged (install: [`docs/reference/PLUGINS.md`](../docs/reference/PLUGINS.md)) |

```powershell
.\scripts\dev\find-port.ps1 -List              # what is attached
pio run -e space-fire -t upload --upload-port (.\scripts\dev\find-port.ps1 -Board fire)
```

## `build/` — run by PlatformIO, not by you

[`gen_console_gz.py`](build/gen_console_gz.py) pre-compresses the embedded web
pages and is wired as a `pre:` action in `platformio.ini`. It is here rather
than in `gates/` because nothing about a normal session calls it: the build
does.

## `release/` — run by the maintainer, once per release

[`make-release.ps1`](release/make-release.ps1) builds the eight firmwares from
clean (never an incremental build) and
assembles the files of a GitHub release in `dist/<version>/` (gitignored): the
companion as a full USB image and as `companion.bin`, the four guest apps, the
three M5Stack Fire images, a ready-to-unzip SD card and `SHA256SUMS.txt`. What a
user does with each file is [`docs/INSTALL.md`](../docs/INSTALL.md), so a file
renamed in the script is renamed there too.

It refuses a dirty working tree (the binaries would match no commit) and a set
`SCE_WIFI_SSID`/`SCE_WIFI_PASS`, because the `-fire` environments compile those
into the binary. The SD zip is taken from `git archive`, so only committed
template files can end up in it. Uploading the files and tagging are separate,
deliberate steps. No `.sh` twin: this is a maintainer chore, not something a
user of the project needs.

```powershell
.\scripts\release\make-release.ps1                  # version from library.json
.\scripts\release\make-release.ps1 -Version v1.1.0
```

## Linux

Every script that a Linux user needs has a `.sh` twin beside it, taking the same
arguments. The gates themselves are Python and were always portable.

```bash
./scripts/check-all.sh
./scripts/gates/test-native.sh test_soundviz
./scripts/dev/find-port.sh --list
```
