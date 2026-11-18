> **English** · [Français](CONTRIBUTING.fr.md)

# Contributing

This project is opinionated in ways that will surprise you, and most of those
opinions are enforced by a gate rather than by review. That is deliberate — the
rules exist because each of them was paid for once — but it means a first
contribution can fail for reasons that look arbitrary until you know why.

This page is the part you would otherwise learn by failing. Read
[`docs/ROADMAP.md`](docs/ROADMAP.md) §A2 for the rules themselves.

## Getting a build

You need [PlatformIO](https://platformio.org/) and Python 3. No hardware is
needed for most of the work: the pure modules are tested natively.

```bash
git clone <this repo> && cd stackchan-companion

# the native suites — no board, ~20 s
pio test -e native

# the whole gate: docs, gates, tests, eight firmware builds, A2.22 in the ELF
./scripts/check-all.sh          # Windows: .\scripts\check-all.ps1
```

`check-all` is the same thing CI would run, and it is what you should get green
before opening anything. `-Fast` / `--fast` skips the firmware build and the
A2.22 pass when you are only touching documentation or native code.

Install it as a pre-push hook once and stop thinking about it:

```bash
./scripts/check-all.sh --hook   # Windows: .\scripts\check-all.ps1 -Hook
```

## The eight gates, and what each one will tell you

| Gate | It fails when |
|---|---|
| `check-doc-parity` | a `.md` changed without its `.fr.md` twin — **structure** (headings, table rows, status markers) *and* **volume** (a translation may not fall behind by more than ~10 %) |
| `check-contrast` | a theme colour, or the shared UI palette, fails the readability threshold on the panel |
| `check-vendored` | one of the five copies vendored inside `SceGuest.h` drifted from its `firmware/common/` original |
| `check-mirrors` | a fact written twice stopped agreeing — servo limits, test floors, USB IDs, the shared palette, `CLAUDE.md` |
| `check-console` | a `Tuning` key has no control in the embedded console |
| `check-guest-config` | a guest setting fell out of one of the four lists it must appear in |
| `check-doc-coverage` | `API.md` or `EMOTIONS.md` no longer matches what the code defines |
| `check-a222` | a draw call count changed **in the linked binary** |

None of them are advisory. If one fails, the fix is the code or the doc — not
the gate. Loosening a gate is a change that needs its own argument in the
commit message.

## The rules that catch people out

**Documentation is bilingual, and English is canonical.** Every `docs/*.md` has
a `.fr.md` twin, updated in the same commit. Code comments are **always
English**, whatever the document language.

**Docs describe the system as it is.** No dates, no "we used to", no narrative.
History belongs in `CHANGELOG.md`. The two exceptions are
`validation/VALIDATION.md` and `validation/PLAYBOOK-HW.md`, which carry dated
verdicts on purpose.

**One draw call site per shape.** GCC 8.4 Xtensa may delete the *second* of two
similar drawing calls in one function body. This is invisible in the source, so
`check-a222` counts calls in the ELF. If you add a shape drawn from a loop,
expect to add an entry there — and read the count out of the binary rather than
counting `canvas.` in your source.

**Nothing heavy in an AsyncTCP callback.** A handler may post a command or set
a tuning field. SD writes, flashes and reboots are deferred to `loop()`.

**One producer, one consumer, one owner of the screen.** The Brain writes
`FaceState`; the renderer reads it; only the renderer touches `M5.Display`.

**No smoothing outside the Brain.** Ramps and filters live in one place. A
second one downstream low-passes values that were already smoothed, and the
symptom is subtle — a blink reduced to a few pixels.

## Adding things

**A tuning key** — add it to `Tuning::table()`, give it a control in the
console, and `check-console` will confirm you did both.

**A guest setting** — it has to appear in four lists: `addSetting`,
`settingGet`, `settingSet` and `saveConfig`. The fourth is the dangerous one:
`saveConfig` **truncates** the file, so a key readable from the yaml but absent
from the save is *destroyed* on the first save from `/config`.
`check-guest-config` exists because that happened.

**A shared header** in `firmware/common/` — if `SceGuest.h` needs it, it must
be vendored there under `__has_include`, with the markers, and registered in
`check-vendored`.

**A route** — register it through `WebApi::route()`, never `_server.on()`, and
put the specific path **before** its prefix. Then add it to
[`docs/reference/API.md`](docs/reference/API.md), which `check-doc-coverage`
will require.

## Tests

`test/test_*/` holds the native suites — 37 suites, 427 cases. Everything in
`engine/`, `behavior/` and `firmware/common/` is pure (Clock and Rng injected)
precisely so it can be tested without a board. If you are adding logic there
and it cannot be tested natively, that is usually a sign the logic is in the
wrong layer.

The floors in `check-all` are asserted, not decorative: a suite that stops
being collected would otherwise pass silently. Raise them when you add cases —
`check-mirrors` holds every place the numbers are quoted.

## Hardware changes

Read [`docs/hardware/`](docs/hardware/README.md) first, and
[`LIMITS.md`](docs/hardware/LIMITS.md) in particular: it lists what has already
been tried on this board and **failed**, with the measurement that closed each
one. Several of those looked like they worked at first.

If you validate something on the robot, record it in
[`docs/validation/VALIDATION.md`](docs/validation/VALIDATION.md) with what you
measured. "It looked fine" is not a verdict this project accepts — most of the
expensive bugs here looked fine.

## Commits

Explain **why**, not what — the diff already says what. State the failure the
change prevents where there is one. Reference the rule (`A2.x`) you are obeying
or amending.

Commit messages avoid accented characters: `git commit -m` through PowerShell
mangles them.

⚠ Never `git add -A` blindly: `sdcard/stackchan-companion/config.yaml` is tracked
and can hold real WiFi credentials on a working robot.

## Licence

AGPL-3.0. Some ported files (esp32-eyes, ESP32_Faces) keep their original
AGPL-3.0 headers; they compile into the same binary, so AGPL-3.0 governs the
distribution of the whole firmware. By contributing you agree your work ships
under it.
