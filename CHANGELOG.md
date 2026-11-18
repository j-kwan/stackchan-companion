> **English** · [Français](CHANGELOG.fr.md)

# Changelog — StackChan-Companion

Current state of the StackChan companion firmware (M5Stack CoreS3, K151 kit).
This is a **single-version** document (no number, no date): it describes what
the firmware does today, by category. Main firmware:
`firmware/companion/`; engine/behaviour live in
`src/engine|behavior|hal|interact|app|guest`; steering document:
`docs/ROADMAP.md`.

---

### The guest bins get the companion's radio settings (09-25)

Measured on the same robot, same spot, RSSI around −75 dBm: pings of 71-295 ms under a guest bin, 2-8 ms under the companion. The companion has disabled modem sleep and maxed its TX power since 07-20; `SceGuest`, which joins the network for all four guests, did neither. It now raises TX power to 19.5 dBm **before** the join, since a weak link is exactly when association needs it, and disables modem sleep once connected, which matters most to the bins that fetch over TLS all day. Measured after the change, under flight-radar: 2-12 ms. Cost: about 40-60 mA while a guest runs.

### The head follows the satellite across the sky (09-25)

`space` gains the option it was always specified with: with `servo` on, once a second the bin propagates the tracked satellite and points the head at it while it is above the horizon, then goes home and releases the torque when it sets, when the element set is older than 14 days or when there is no clock. Off by default, like every guest automation that moves something. `servo_az` says where the robot's face points, since an azimuth means nothing to a servo without it. The pose is `firmware/common/headtrack.h`, pure and pinned by two new `test_astro` cases (429 native tests): yaw from the viewer-centric convention, clamped at ±130°, pitch one servo degree per degree of elevation up to the highest safe raise. Its four copies of `Units.h` constants are held by `check-mirrors`. The servo code the radar kept to itself (PY32 power, non-blocking SCS writes, torque release) moved to `firmware/common/HeadServo.h`, now shared by both bins; `space-fire` builds without servos (`SCE_HAS_SERVO=0`) and does not link the library. The direction was checked on the robot: with the satellite set 90° to the robot's left, the head turned to the robot's left (PLAYBOOK 4j.2).

**flight-radar turned its head away from the flight.** Its own formula, `166 + bearing`, had the sign backwards against the same convention, and nobody had watched it on the robot. It now takes its yaw from the same `headtrack.h` as space, and its private `YAW_REL_MAX` copy is gone. The radar still assumes the robot faces north: it has no `servo_az`.

### Launching a guest from the console or the API did nothing (09-25)

`POST /api/bins/launch` (and the console's 🚀 button, which calls it) answered 202 "flash + reboot in ~2 s" and the robot simply kept running the companion. Only the comment announcing the step had survived in `loop()`: the code that consumed the request and flashed the guest was gone, and git has no trace of it, as it was already missing from the root commit rebuilt when the history was reset on 09-19. The swipe-down launcher was not affected. Rewritten step for step on `Launcher::launch()`: the same three refusals before anything irreversible (no `/companion.bin` to come back to, no OTA partition, a file absent, empty or larger than the partition), the same flash screen, the servo torque released first and the rail cut last, and on any refusal or failed flash everything given back as when the menu is dismissed. Checked on the robot: `space.bin` launched from the API is running ten seconds later.

### Every rule file on the card is checked before it reaches one (09-25)

A `rules.txt` line the parser refuses is simply absent — no error, no trace, only a robot that does not react — and `GET /api/rules` can only show that for the file currently loaded, never for another personality's set. `scripts/gates/check-rules.py`, the ninth gate, reads every `rules*.txt` shipped in `sdcard/` and refuses: a line the parser would drop (over 127 bytes, under seven fields, unknown op, action or emotion, `set` with no key), a `PlayDance` naming a dance neither compiled nor on the card, a string field (`ip`, `clk`, `tmr`) compared as a number, a rule with no gate whose condition is true at 0 on a field the firmware never publishes (so it fires on its own), a threshold outside the field's range, more rules than the engine keeps, a field differing from a published one only by case, and a personality naming a rule file absent from the card. Emotions, dances, published fields and caps are read from the source rather than restated. The shipped files pass. `PLUGINS.md` also stopped saying an unknown dance name makes a line fail to parse: it has not since dances were resolved at fire time.

### The ROADMAP keeps steering, and stops carrying what lives elsewhere (09-25)

`docs/ROADMAP.md` went from 1 574 to 1 138 lines without losing a fact. The T0-T9 batch journal became a one-line-per-label glossary pointing at where each topic is documented today (the code and `validation/` still cite those labels); the two things it held that no reference page carried, the overlay effects and the per-emotion eye motion, moved to `reference/EYES.md`. The specifications of the two shipped bins became their documentation: §5 (space) and §9 (led-fluid) are now pointers, the numbered traps the space code cites live in `guests/SPACE.md` § The ten traps, and led-fluid's design rationale and open questions in `guests/LED-FLUID.md` § Design notes. Every code comment citing `ROADMAP §5.x`/`§9` was repointed, along with three dead references (`ROADMAP-v2`, a non-existent `§2.0`, the old `TESTS-SONNET` name of `PLAYBOOK-HW`). Also removed: the mention of the pre-merge `PLAN-V2.md`/`planv2.md` files and the notes that a document was written for a particular AI model.

### Published releases: ready-to-flash binaries and an install guide (09-25)

Running the project no longer needs PlatformIO. Each GitHub release carries the companion as a full USB image (bootloader + partitions + otadata + app, flashed at `0x0`, including from the browser with esptool-js) and as `companion.bin` for OTA and the SD card, the four guest apps, the three M5Stack Fire images, a ready-to-unzip SD card and `SHA256SUMS.txt`. The factory image resets `otadata`, so it boots what it just wrote — the "flash succeeded, old slot booted" trap of `hardware/LIMITS.md` does not apply to this path. `docs/INSTALL.md` is the single guide, the same for every release: flashing, SD card, first WiFi, configuration, guest apps, OTA update (including the `/companion.bin` step people forget), Fire, troubleshooting. `scripts/release/make-release.ps1` builds the set; it refuses a dirty tree and a set `SCE_WIFI_SSID`/`SCE_WIFI_PASS`, which the `-fire` environments would compile into a public binary.

### Switching the console's language used to leave half of it behind (09-22)

Found from a screenshot: an English console showing "23 règles · 23 actives" in the Rules section header, every other label correctly in English. Root cause: `applyLang()` only ever sweeps `data-i18n`-tagged elements — anything the JS builds by calling `t()` directly and writing the result straight into the DOM (the Rules table and its counter, the Files list, the dance dropdown's placeholder, the tuning table) was never wired to be re-rendered when the language changes. `setLang()` (the EN/FR button) retranslated every static label immediately but never re-ran the four loaders that produce that dynamic text, so whatever they last rendered stayed on screen — in whichever language was active *then* — until the matching manual refresh control was pressed.

Same gap, quieter version, at BOOT: `loadRules(null)` ran one line *before* `applyLang()`, so the very first render of the Rules table always used the hardcoded English default rather than the card's configured language, correcting itself only if the user later touched Rules again. `loadDances()`/`loadSdList()`/`refreshTun()` had the identical ordering problem. Fixed once: all four now run from a single `refreshDynamicText()`, called both after `applyLang()` at boot and from `setLang()`'s success and failure paths — the same fix serves both bugs, because they were the same bug at two different moments.

Separately, four strings used only by the microphone status line (`snd_mact`/`snd_moff`/`snd_mstby`/`snd_mwarm`) had no English entry at all and no `data-i18n` element to snapshot one from — an English console showed the literal key name instead of a sentence. Added the missing English text. Two more keys (`l_rules`, `perso_def`) turned out to be dead: declared in French, read by nothing. Removed.

### A full character table refuses a 9th file out loud, and never loses track of who is running (09-19)

Three bugs found by review, none seen on hardware yet. `PersonalityStore::loadAll()` used to `break` out of the ENTIRE directory scan the moment a new-character file found the table full — so a 9th file didn't just fail to create a character, it silently stopped every FILE AFTER IT in directory order from being read, including plain edits to characters already loaded. It now skips only the file that cannot get a slot, logs it (`[perso] '<id>' ignoree : table pleine`, counted in the reload's problem total), and keeps reading the rest of the directory.

Separately, the table's slots are reassigned by directory order on every reload, so creating or deleting a character can shift what index an unrelated one now sits at — and the console's create/edit/delete handler used to blindly re-apply whatever raw index `tuning.personality` still held, which could silently switch the ACTIVE character to a different one. It now carries the active character's name across the reload and re-resolves its index by name afterward (falling back to the compiled default only if that character was the one just deleted).

Also: the comment next to `PlayDance`'s stored `cmd.i` claimed it was "the value the sink falls back on if the name ever fails to resolve" — it never was; `rulePost` (main.cpp) has always resolved by name only, and a stored index resolving anything would reintroduce the exact stale-index bug the fire-time design exists to avoid. `cmd.i` stays (its compiled-index-or--1 value is pinned by `test_rulestore`), the comment now says what it actually is: informational only.

### The pinch-to-zoom gesture could go silent on real hardware, and now cannot (09-19)

`flight-radar`'s two-finger zoom read `M5.Touch.getDetail(0)`/`getDetail(1)`, guarded by `isPressed()` — but `getDetail()`'s slots are indexed by the FT6336's HARDWARE touch id, which rotates between contacts and is not packed, exactly the defect `TouchGestures.h`'s "Trap 2" already documents and fixes for every other gesture on this robot. Two real fingers down could land in slots 0 and 2, leaving `getDetail(1)` stale or empty and the pinch silently never registering even though `getCount() == 2`. Switched to `getTouchPointRaw(0)`/`getTouchPointRaw(1)` — packed by the panel read, so index 0/1 are simply the first two points reported this pass, whatever hardware id they carry — the same fix already in use everywhere else touch is read on this project.

### The project is StackChan-Companion now, on the card as much as in the docs (09-19)

Renamed from StackChan-Eyes: the eyes were always one piece of a companion
that also moves the head, dances, listens and hosts guest apps, and the old
name undersold that. Renamed everywhere the name appears — docs, `library.json`,
the console's own title, the LICENSE header, the git remote — with one
exception carried deliberately: `SD_ROOT_OLD` in `firmware/common/SdRoot.h`
still names the old path, because that is what makes an already-deployed card
self-heal.

**The SD config directory moves too, and it moves ITSELF.** Every firmware
that mounts the card — the companion and all four guest bins, including the
`*-fire` variants that run with no companion at all — calls
`sce::migrateSdRoot()` once a card is confirmed mounted: `/stackchan-eyes`
becomes `/stackchan-companion` via a single FAT rename (one directory-entry
rewrite; the files and the `personalities/` subdirectory underneath never
move), and every path in the firmware already points at the new name. A card
that has never seen the new firmware is untouched until it does; a card
already migrated is left alone (the new path existing is what stops the
migration, checked before the old path is even asked about). `test_sdroot`
pins the three-way decision — nothing to do on a fresh card, nothing to do
once migrated, move it otherwise — as a pure function, the same reasoning
that put the personality dials' clamp math in `Personalities.h` rather than
inline: a filesystem rename is not itself testable on native, but the
decision of WHETHER to make one is.

**The bug this rename shipped, found on the physical robot and nowhere
else.** `WebApi::sdPathAllowed()` gates every SD read, write and delete
through a whitelist that — for three checks — located the file name by
searching for a `/` starting past a hardcoded offset into the path (16, 16,
and a `strlen(PERSO)` of 30). Renaming the path STRINGS moved the boundary
those numbers described without moving the numbers: `/stackchan-companion/`
is five characters longer than `/stackchan-eyes/`, so all three searches now
landed on the ROOT's own trailing slash instead of a real subdirectory
separator, and every one of `config.yaml`, `rules.txt`, every guest yaml and
every personality file came back `403 chemin interdit` — a correctly-typed,
correctly-existing path, refused by an offset that no longer described
anything. Nothing in the 427 native tests could have caught it: the function
lives in `WebApi.h`, which needs Arduino's `String` and is not natively
testable, and every renamed literal LOOKED consistent by inspection — three
call sites, one string constant, and three plain integers were never tied
together in the source, so a global rename touched the first and stepped
past the second. Found only because the actual robot's `/api/personalities`
still reported an old path after the migration succeeded, which sent the
trail to the one route that could produce that: a read silently refused.
Fixed by deriving both offsets from the same string the path checks already
start with (`strlen(SD_ROOT)`, `strlen(PERSO)`) rather than writing their
length out by hand a second time — the next root rename cannot reopen this
exact gap, because there is no second number left to forget.

One consequence worth naming for anyone updating a card by hand rather than
letting the robot migrate it: a personality file SAVED from the console
*before* this update has its `rules:` field written as a literal absolute
path, and a directory rename does not reach text sitting inside a file — only
the directory entry moves. A personality whose rules file was never
resaved after updating will trace `regles ABSENTES … repli sur defaut` on
serial and fall back to the default personality's rules (already the
project's existing, tested behaviour for any missing rules file, not a new
failure mode) — resaving that one personality once from the Characters tab
rewrites the path under the new root and clears it for good.

### A personality can pace itself, not just pick a rule file (09-19)

Two FEEL dials join the personality table: `transition_scale` (0.3-3.0, how
fast the robot moves between moods) and `pitch_bias_scale` (0.0-2.0, how far
its posture leans into one — zero holds the head level regardless of mood).
Both are SCALES over the ~100 compiled per-emotion durations and head-tilts in
`transitionFor()`/`pitchBiasFor()`, never replacements, so a snappier or more
stoic character is still recognisably the same character. Console: two new
sliders in the Characters tab; API: `GET`/`POST /api/personalities` gained
`tscale`/`pbscale` and `transition_scale`/`pitch_bias_scale`.

The clamp math lives in `Personalities.h` as pure functions rather than inline
in `Brain::setTransitionScale`/`setPitchBiasScale`, because `Brain` cannot be
natively tested at all — its constructor opens a FreeRTOS queue and pins a
task. `test_personality_feel` sweeps both scales and the resulting duration
across a wide input range, including NaN and infinity, rather than trusting a
handful of sample points — and that sweep caught a real bug: the project's
shared `clampVal` (`src/engine/Units.h`) let NaN pass through unclamped,
because both of NaN's comparisons are false. Every hardware bound built on
`clampVal` — servo yaw, gaze — shared the same gap; fixed once, at the root,
rather than worked around locally for the two new dials.

### The console has a light face too, day or night, any theme (09-15)

Every console theme now declares a DARK face and a LIGHT one, switched by the
browser's own `prefers-color-scheme` -- no toggle to find, because the robot has
no opinion on what room you are in and asking the system once is more honest
than a switch nobody notices. A theme reshapes its own background, ink and
glass now, not only its accents: a page that only swapped a link colour under a
personality's name would not actually look like a different console.

**The Gundam theme became the actual RX-78-2 palette.** It shipped as a generic
green/gold guess; corrected to blue, gold, red and white/blue -- dark mode is
navy plating with white the interactive accent and gold the trim, light mode is
off-white plating with Gundam blue taking the accent role white held at night
(whichever of the two would blend into its OWN background is the one the other
mode needs instead). Gold stays close to the same hex across both modes -- it
reads against either surface -- deepened once so it stays visually apart from
white rather than reading as a pale lemon next to it in the icon row.

**Buttons stopped blending two unrelated hues on one shape.** `--acc-grad` used
to run primary accent straight into secondary accent -- cyan into violet by
default, blue into gold under Gundam -- which reads as a flag on a small filled
control rather than a sheen. It is now a MONOTONE gradient (`--acc` into a
lighter tint of itself, `--acc-lt`), and the secondary accent is reserved for
flat text only: section labels, markers, trim. Three raw two-stop declarations
(the header wordmark, the logo badge, the progress-bar fill) pointed at the old
pairing directly rather than through the shared token and needed the same fix
by hand.

**`check-contrast.py` now resolves the real cascade, not one block.** Reading a
theme's dark declaration and its light declaration separately would have missed
exactly the bug that shipped first: `[data-theme=gundam]{...}` appears twice in
the file, once plain and once inside the light media query, and a regex with no
notion of "inside a block" let the SECOND occurrence silently overwrite the
first as "dark" -- so the dark Gundam variant was being judged against colours
that were never meant to sit on that background at all. Fixed by computing
every `@media` block's span first and excluding matches inside it from the dark
collection; caught immediately once the gate was pointed at itself again
(`gundam` reported 2.28:1 on a channel that should have read 15:1). The gate now
resolves FOUR layers in the order the browser actually applies them -- base
dark, base light, theme dark, theme light -- and produces four measured
variants: default, default (light), gundam, gundam (light). All four conform.

**The header stopped being the one element with no theme.** Its translucent
backdrop was a fixed dark rgba regardless of mode -- invisible against a dark
aurora, a flat dark band floating over a light one. One more token
(`--head-glass`), same fix as everywhere else: declared per mode, read instead
of hardcoded.

**The Characters tab's inputs were never actually broken -- they were
unqualified.** Two fields had no `type` attribute at all; `input[type=text]`
cannot match an input that carries no `type`, HTML default or not, so they wore
the bare browser default instead of the console's own skin, which was the whole
of what "looks different" meant here. The four action buttons used an invented
`.btn` class alongside the ones the rest of the console already has --
`.b-acc` for the one primary action a panel offers, plain `button` for
secondary ones, `.ko` for delete -- so they now match every other button in the
console instead of a parallel system nobody else uses. The colour swatch is
sized like the other controls rather than stretched into a bar by a rule meant
for text inputs. And the panel gained one heading level nothing else in the
console has (`<h3>`, styled nowhere) -- split into its own card instead, one
heading per section like everywhere else.

**The logo's eyes are asymmetric now**, matching the one visual signature this
robot actually has on its own screen (A2.17: equidistant centres, never equal
size) -- two identical rounded pills read as a generic robot glyph, not as this
one.

Three stray files from this session's own testing -- `default.yaml` and
`haro.yaml` with their themes swapped, plus a leftover `zaku.yaml` -- were
removed from the card; the compiled defaults are what a fresh card runs.

---

### The console can make a character (09-15)

`http://<ip>/#perso` — a Characters tab that creates, edits, duplicates and
deletes, writing `/stackchan-companion/personalities/*.yaml` through
`POST`/`DELETE /api/personalities`. The whole character travels in one request,
so the robot never merges two half-updates, and an omitted field keeps its value:
sending `color` alone changes the colour and nothing else.

**The console wears the active character's skin.** `theme:` is stamped on the
page root and the palette follows the robot. Only the ACCENTS move — the readable
inks are fixed, because a skin able to shift those could make the page
unreadable, which is the one failure you cannot fix from the page.

**So the console palette is now contrast-checked**, for the first time. It sat
outside the gate while there was one palette nobody edited; a personality that
can pick a skin multiplies an unverified surface, so `check-contrast.py` grew a
pass over `WebConsole.h` — true colour, not RGB565, and judged against the glass
panel rather than the page, since black would flatter every ink. Both palettes
conform. Proven to bite: a deliberately muddy green fails at 2.22:1 and the gate
exits 1.

**Three things the first screenshot showed, which no test would have.** The
colour swatch displayed a confident green for the default character, which has no
colour at all — greyed now, with the emotion-palette switch driving it. Delete was
live on the character that cannot be deleted — disabled rather than merely
refused, because a button that scolds you is a worse way to learn a rule than one
never offered. And thirty sliders at zero read as "draws nothing" when the
default character in fact leaves the firmware's table alone — said in words
instead.

The selector and the random-moods switch MOVED out of Options, leaving a note
where they were. Two controls for one tuning key is the drift the dance-select
race already cost once.

---

### A character can now be born on the SD card (09-14)

Drop `zaku.yaml` in `/stackchan-companion/personalities/` and the robot has a new
character at the next boot: its own colour, console theme, roulette cadence and
resting expressions, with no recompiling. Proven on target — a personality that
exists nowhere in the firmware drew only its own four emotions.

**The file name is the id, and it decides edit versus create.** `haro.yaml`
matches the compiled Haro and overlays ONTO it, which is how an edit to a
shipped character survives a reboot; a name nobody holds takes the next free
slot. The first version of the loader appended blindly from slot 1 and the first
card file silently REPLACED Haro — visible only because `/api/personalities` was
added to see the table from outside, the serial line being unopenable without
resetting the board.

**It states differences, not a declaration.** An absent key keeps the value
underneath, so a half-written file degrades to "mostly the default" rather than
to a character with holes in it. `weights:` is the one exception: declaring the
section replaces the table, because a merge could never express "this character
does not do Sad".

**Loading is idempotent** — the table is reset to the firmware's own characters
before the directory is re-read — so a deleted file really removes its character
and an edit is never applied twice.

**Themes are compiled and picked by name.** A palette authored on the card could
not be contrast-checked at build time, and an unreadable console is the one thing
you cannot fix from the console.

### The reading accessor was not the writing one (09-14)

`at()` clamps an unknown index onto slot 0 so a reader always gets a working
robot. The card loader used it to WRITE, so filling slot 2 of a two-slot table
wrote into personality 0 — and the clearing loop, going through the same
accessor, then erased its name. The symptom on the robot: `personality=2` read
back as 0 and the default emotions kept coming.

Two accessors now, for two opposite duties: `at()` protects the READER, `slot()`
protects the TABLE by refusing an out-of-range index instead of redirecting it.
The bug was pure logic and needed no SD card to reproduce, so it is pinned by a
native test — watched to fail first, `Expected 3 Was 2`, the table declining to
grow exactly as it had on target.

---

### A rule can play any dance, and petting the robot is now data (09-14)

Two unlocks that only matter together: the robot can react to being touched, and
it can answer with a choreography written on the card rather than compiled in.

**`PlayDance` carries the NAME now, resolved when the rule FIRES.** It used to
carry an index looked up at parse time, which meant a rule naming an SD
choreography was **rejected outright and in silence** — the file had a line, the
engine had no rule, and nothing said so. Two separate facts forced late
resolution rather than merely favouring it: at boot the rules are read BEFORE
`danceStore.reload()`, so the bank is empty while the file is being parsed and no
lookup there could ever find a card dance; and `DanceStore` is double-banked and
reloads hot, so an index stored at parse time designates a different dance after
an upload. The name is interned in `RuleStore`'s own arena — a stable pointer, as
A2.17 demands of anything crossing the CommandQueue — and the app-side sink
resolves it per firing against the merged list. A name matching nothing is now
said out loud on the trace instead of doing nothing quietly, which from outside
is indistinguishable from a rule that never fires.

This unlocks EVERY SD choreography for rules, not just the four Haro ones.

**The head stroke publishes a field.** It went straight to the CommandQueue and
left no trace anywhere a rule could see, so the one genuinely intimate
interaction this robot has was the only one `rules.txt` could not react to — and
the reply could not differ between characters, which is what a personality is
for. It is a STATE (1 while a hand is there, 0 when it leaves), not an event:
`sustainMs` then expresses "held for a moment" on its own and the return to 0
re-arms the rule, so one stroke fires once however long it lasts.

Verified together on the robot, with the roulette switched OFF so nothing else
could produce the result: a stroke gave Happy (the direct reply), then Glee —
which with no roulette can only be keyframe 6 of `haro_call.csv`. Head sensor →
field → rule → a dance from the card.

The Haro rules take their own dances back: `haro_call` on a long conversation and
on the timer bell, `haro_float` on waking and on charge, `haro_roll` on a
successful build, plus the new stroke rule. Sixteen rules, 19/24.

`test_rulestore` gained the contract: an SD name is accepted and travels as a
name, and the pointer survives the next line being parsed over the same buffer.
Watched to fail first — restoring the parse-time rejection breaks both.

---

### The robot has a character, and you can choose which (09-13)

`POST /api/tuning?personality=1` and the robot becomes a Haro: green, resting in
six cheerful expressions instead of seventeen, obeying its own rule file.
`personality=0` gives back the robot this documentation has always described.

**The existing behaviour is personality 0, not "the absence of a costume".** It
is a character in its own right, which is why the key is an INDEX and not a
`haro_mode` boolean — a boolean would need renaming the day a third character
appears, and on this project renaming a key IS a migration. The name is paid for
now, while it costs nothing.

**A personality owns three things and nothing else**: its rule file, its roulette
(weights + cadence) and its identity colour. It never writes another tuning key,
so brightness, servos, sound and thresholds survive a switch in both directions
and there is never a question of which layer last wrote a value.

**The line it may not cross** is written as a rule and asserted as a test: a
personality changes what the robot FEELS and how long it SHOWS it; it may never
change what the hardware TOLERATES. Servo end stops, the frame budget and bus
timings are therefore absent from the table by construction. A "personality" able
to park a servo against its stop would be a damage mechanism wearing a costume.

**Personality 0 declines to declare any weights**, and that is the design rather
than a gap. It does not restate the historical table, it leaves
`EmotionRoulette`'s own alone — which makes "a card with no personality data
behaves exactly as before" true by construction instead of by careful copying.
The test asserts it by comparing a fresh roulette against one that has had
personality 0 applied, over 4000 draws with the same seed, so it never has to
know a single weight. Watched to fail first: giving personality 0 a table breaks
it immediately.

**A file per personality dissolves the rule ceiling.** The engine holds 24 rules,
3 built in, and refuses further ones SILENTLY — the default set was sitting at
23/24. Gating rules on the active character would have made every personality
share those 21 slots; one file each means only one is ever loaded, so each gets
all 21 — and the rules need no personality gate at all, because THE FILE IS THE
GATE. Raising `MAX_RULES` stopped being necessary.

### Random moods can be switched off (09-13)

`POST /api/tuning?roulette=0`. With the roulette on, the robot draws an emotion
every 6-12 s — which is what makes it feel alive when nothing is happening, and
also what stops any expression from MEANING something, since the next draw
overwrites whatever a rule just said.

It is not a freeze, and the distinction is load-bearing: timed emotions still
expire back to rest (`SetEmotion` carries a default duration and the Brain
restores `_preOverride`), and blinking, glancing, breathing and the gyroscopic
gaze are driven elsewhere and keep running. What stops is the random CHOOSING.

The tick is consumed even while off, so switching back on does not fire
instantly — the cadence stays measured from the clock rather than from the
unlock, or the robot would react to being freed instead of living at its own
pace.

### The Haro rules, corrected against the code (09-13)

The rule sets written for this character were reviewed line by line against the
parser rather than against the documentation, and six defects came out — four of
them silent.

`build le 0` FIRED ON EVERY BOOT. `RuleEngine` reads `getF(field, 0.0f)` with no
existence test, so with no PC script the field does not exist, reads 0, and
`0 <= 0` is true: the robot celebrated a build that never happened. Any `lt`/`le`
rule with a threshold above 0 on an optional field has this shape. Rewritten to
non-zero codes with `eq`.

THE TIMER RULES READ THE WRONG FIELD. `tmr_st` is a COLOUR code, not an ordered
state, and it is not monotonic: `Idle=0 Run=1 Paused=2 Ring=3 Work=1 Break=4
Hydrate=4 Done=4`. So `tmr_st ge 3` caught the ring but also every break and
every hydration prompt, and `ge 1` caught everything except Idle — putting
"Focused" on the face during breaks, which is backwards. The right field is
`tmr_ph`, published alongside precisely because `tmr_st` conflates phases.

`light gt 80` WAS UNREACHABLE, and that is arithmetic rather than opinion.
`light_pct = 100·ln(v+1)/ln(4096)`, so 80 % needs 775 raw counts, while the
measured calibration in `Ltr553.h` says a lit desk reads 50-100 behind the K151
shell. The rule asked for 8-15x a lit desk.

Also: `night` is not solar dusk (that lives only in `/api/sensors`) but the light
sensor's night mode, during which the roulette ALREADY makes Sleepy dominant — so
the rule duplicating it only pinned Sleepy over a robot already dozing. And the
set was at exactly 24/24, where `add()` refuses silently. Now 23/24, one slot
free, verified by script rather than by counting — the first rewrite came back at
21 rules and reproduced the defect.

### `haro_float.csv` could not float (09-13)

`holdMs` is the keyframe's TOTAL duration, not an addition (`Sequencer.h:44`), so
`servo 300 / hold 600` means "move 300 ms, then sit still 300 ms". Half the dance
was motionless — a series of nods rather than a drift. Rewritten with
`hold == servo`: dwell went from ~50 % to 2 %. The other three validate as they
are, and their dwell is deliberate — `haro_scan` at 51 % is look-hold-look, which
is good choreography.

---

### Two fingers, one answer -- a gesture that worked an even number of times (08-25)

Holding two fingers on the face for three seconds toggles the debug row
(`emotion . ip`). It did not work, and the reason it did not is worth keeping:
every individual part of it behaved.

Measured on the panel during ONE deliberate 8.9 s hold -- the counters were
published through `/api/sensors` rather than the serial line, because opening
COM6 resets the board over native USB and would have restarted the very state
being measured. The ">= 2 points" condition started FIVE separate times, the
glass read completely empty in between, and the gesture latched twice and fired
twice. Since it is a TOGGLE, the second answer undid the first. The report was
"it does not work"; the truth was "it works an even number of times".

**The defect was asymmetric debouncing.** Arming already waited 150 ms before
believing a second finger -- deliberately, because the panel reports one lifting
finger as two for a couple of updates, and latching on that tail once swallowed
every band gesture that followed it. Releasing waited for nothing: a single pass
reading zero ended the gesture. So the very dropout arming was built to survive
ended the contact instead, and the next pass started a second gesture inside the
first.

Both edges are debounced now, and the tolerance is granted to an ESTABLISHED
gesture and never to a candidate -- that asymmetry is what keeps the
release-tail trap from coming back in through the fix. The timing moved out of
`TouchGestures` into `src/interact/TwoFingerLatch.h`, which has no M5Unified in
it and is tested natively (`test_twofinger`, 8 cases). The regression test was
watched to fail against the old behaviour first: `Expected 1 Was 2`, the
measured symptom exactly.

**A first reading blamed the wrong thing.** With the fingers close together the
controller reported a single point for the whole attempt, and the honest reading
of that -- the panel cannot offer a second point -- would have led to redesigning
the gesture around another sensor. A direct read of the FT6336U's own
`TD_STATUS`, beneath the whole M5GFX stack, said 2 as soon as the fingers were
spread. The panel merges nearby contacts; that is a spacing requirement, not a
missing capability.

### The debug row draws where your fingers are (08-25)

While `band_debug` is on, each contact gets a vertical and a horizontal line
crossing under it, one colour per point. It exists because of the paragraph
above: until you can see one crosshair where you put two fingers, "move them
apart" is invisible advice.

Drawn in `pushEyeZone()` -- the single exit point every rendering path takes --
so it does not vanish on a blink or on the "..." screen, and after the CRT
post-process, since an instrument that is itself smeared measures nothing. The
four possible segments are a table walked by one loop: written as four
`fillRect` calls in one body they are exactly the group the Xtensa backend may
thin, and the symptom would be a crosshair permanently missing an arm. The gate
counts the call site in the linked binary -- and caught the entry on its first
try, because the mangled name had been written from memory with the wrong
length prefix.

---

### The pomodoro is two targets you can see, not a direction to remember (08-25)

The vertical swipe that cycled the session's shape lasted less than a day: it
was hard to aim on a forty-pixel strip, which is a fair verdict on a gesture
with no on-screen affordance. It is replaced by a TAP ON WHAT IS ALREADY
DRAWN -- the phase icon cycles the shape, the digits start and pause. A control
you can point at beats a direction you have to recall, and neither of the two
needed a new pixel: the band was already showing both.

**The boundary between them is the layout's own.** The digits are CENTRED, so
they and the icon both slide with the text: `1/4 25:00` and `1/4 120:00` put
the stamp twelve pixels apart. A hit box written as a constant would be right
for one text and quietly wrong for the rest, so the painter and the finger now
read the same `units::bandTextX0()` (Units.h gains a section 7 for it, and the
renderer's own copy of the formula is gone). Everything left of the first digit
is the icon, which makes the whole left margin the target rather than the 18 px
stamp -- on glass that is the difference between a control you hit and one you
stab at.

Still only between sessions, and during a run the WHOLE band is start/pause:
the edit lock the countdown has had since 08-04 stays, but the icon no longer
goes dead under it. A tap that does nothing is a hole in the band.

Pinned by two tests, and they were watched to FAIL first: moving the icon's
offset from 30 to 22 makes `Expected 12 Was 4` -- the twelve pixels of air the
layout is built on. What they prove is not the number but the AGREEMENT, since
the same two functions now decide where the stamp is painted and what a tap
meant.

### Every band gesture had stopped answering, and the count was lying (08-25)

The timer could no longer be set by holding and sliding, the sound band would
not cycle its skins, band taps did nothing. One cause for all three, and it was
the two-finger hold added the day before.

**`getCount()` does not count fingers.** It returns the number of detail SLOTS
whose state is not `none`, and M5Unified indexes those slots by the panel's own
HARDWARE touch id -- the high nibble of P1_YH on the FT6336, which ROTATES
between successive contacts. A slot also outlives the lift by two updates: one
pass to become `touch_end`, one more to clear. Put together, a finger landing
on id 1 less than ~60 ms after a lift from id 0 makes the count answer TWO with
one finger down. That is not a corner case -- it is what a slow slide does
every time the panel briefly loses contact and re-acquires it, which is
precisely what the timer's scroll is made of. The two-finger branch latched,
and it latches until the glass is clear, so the whole contact was swallowed.

`isPressed()` asks the question actually meant: true only for a slot the panel
reported in THIS pass. Press and release now come from that count's edges
rather than from `getDetail(0).wasPressed()/wasReleased()`, and the position
comes from the PACKED raw array -- which closes a second, older hazard in the
same breath: `getDetail(0)` is slot zero, and for a contact the panel numbered
1 that is somebody else's stale data, unreachable through M5Unified's
index-clamping accessor. An interrupted one-finger contact also dispatches its
drag ending now, or the consumer's "already scrolled" flag latches and eats the
next tap -- the 08-04 regression, arrived at from the other side.

**The sound analyser stuttered because a refused block was counted anyway.**
`M5.Mic.record()` returns false when the driver cannot take the buffer, and the
return was dropped. The in-flight counter then said two while the driver held
one, and that lie never heals: every later pass found more in flight than
recording, handed the visualiser a buffer NOBODY EVER FILLED, and stepped the
tail past the head. The scope replayed and jumped instead of scrolling. A
refused block is no longer counted; the queue is one short for a single pass
and the next pass tops it up.

**A vertical swipe on the pomodoro cycles the shape of a session** -- 25/5x4,
50/10x3, 90/20x2, 15/3x4. Mode 5 was the last band answering nothing to that
gesture. What it cycles is the shape rather than any single number, because
that is the setting worth a finger, and because the band already spells the
answer out: Idle reads "1/N MM:00", so the two numbers that move ARE the label.
Only between sessions -- a running pomodoro is not re-shaped by a brush of the
hand, the same edit lock the countdown has had since 08-04. Settings matching
no shape land on the first shape of the swipe's direction: a custom 30/7 typed
into the console is not "the classic one", and treating it as one would answer
the first swipe by changing nothing visible. The table and its wrap are pure
and natively tested; the touch wiring only applies the result.

**The end-of-timer dance now resolves the whole list.** `timer_dance` indexes
`/api/dances`, which is the built-in dances followed by the SD choreographies
-- but it was posted straight to `PlayDance`, which knows only the built-in
prefix. Pick a `.csv` from the card and the last block of a pomodoro ended in
silence, with no error anywhere, because the index is bounds-checked and
simply does nothing. Both halves resolve here now, in the order the endpoint
concatenates them, so the console's promise that its list cannot drift from the
robot's is true rather than merely written down.

**The console showed a hydration slider sitting at the browser's midpoint**
while its own readout said 1: no initial value attribute, and missing from the
list `refreshTun` syncs -- the one thing the comment beside that list warns
against. The dance selector had the mirror-image bug: two racing fetches wrote
it and whichever landed second won, so a saved choice read "none" while the
robot danced anyway. One function owns that value now, called from both ends.

### The build had stopped being C++17, and a gate had stopped being able to pass (08-25)

`build_unflags = -std=gnu++11` was written for the Arduino 2.x core, which
appended that flag after `build_flags`. The 3.x core appends `-std=gnu++2b`
then `-std=gnu++2a`, so the unflag removed nothing, our `-std=gnu++17` was
overridden, and the firmware compiled as C++20 while this repository, the
native tests (gnu++17) and every document said C++17. The same trap as the
first time with a different value -- hence all three standards listed rather
than only the one that bites today. Back to C++17: that is the standard this
code was written, tested and reviewed under, and moving to C++20 is a decision
taken on its own, with the `native` env aligned in the same breath, not a side
effect of a platform migration. Verified the way it should have been the first
time, by reading the flags the compiler is actually handed.

`check-a222.py` had just been taught to FAIL when objdump is missing instead of
announcing a skip and returning success. It hardcoded `.exe`, which made that
hard failure permanent on Linux and macOS -- the same bug with the sign
flipped. Both spellings and both toolchain layouts are searched now, `PATH`
last. And `check-all.sh` carried a literal backslash-n inside an unquoted line
continuation, so `pio run` received a stray `n` argument and the POSIX runner
could not build at all.

**The framework now logs errors only** (`CORE_DEBUG_LEVEL` 3 -> 1), and that is
not housekeeping. Since the 3.x core `Serial` IS the USB CDC, and
`HWCDC::write` takes its lock with a 100 ms timeout and can then chain several
more of those waits when the host stops draining -- a cable plugged in with the
monitor closed is exactly that state. loop() runs on core 1 beside the
renderer, so one INFO line from the WiFi stack can starve the touch polling the
same way an over-budget frame does (A2.22). Our own `Serial.printf` and
`sce::trace` do not go through those macros and are untouched; what disappears
is the framework's chatter. Worth 17 KB of flash as well. Put it back to 3 for
the length of a network debugging session.

**And lowering it cost a draw call, which is how we learned the gate works.**
A2.22 failed on `flight-radar-fire` the moment the level dropped: `drawMetar`
went from three `drawString` call sites to two. The METAR failure panel drew its
reason and, under it, the HTTP code that names the failure -- two calls in one
body differing only in a string, a colour and eighteen pixels of y, which is
precisely the pair GCC is entitled to thin out. At level 3 the inlining budget
happened to keep both; at level 1 the ESP32 backend emitted one, and the HTTP
number was gone from the binary. Nothing else would have said so: the screen
still draws, still explains itself, and simply omits the number, on the board
whose usual failure IS a bad fetch.

Causality was checked rather than assumed -- the env was rebuilt at level 3
(three call sites) and at level 1 (two) with nothing else moved. The fix is the
one A2.22 prescribes and not a retreat to the old flag: the two rows are a
two-entry table and a loop, so there is no pair left to thin. Verified in the
linked binary on BOTH backends, which now agree at two, and the disassembly
shows the surviving call site loading its string, its colour and its y from the
table rather than from constants. A shape that only holds at one optimisation
level is not a fix; this one survives the flag that exposed it.

The gate's expected count for `drawMetar` therefore goes DOWN, from three to
two, and that is worth stating plainly because a falling expectation usually
means somebody deleted a feature to make a check pass. Here it means two call
sites became one loop -- the same direction the rule always pushes.

**The display, touch and audio stack is pinned to the exact release.** `^0.2.3`
spans the whole 0.2.x line, and the dependency reinstall that came with the
platform switch slid M5Unified 0.2.16 -> 0.2.20 and M5GFX 0.2.22 -> 0.2.28 with
nothing asking for it. 0.2.28 carries a rewritten `Bus_I2C` and a brand-new
`getSpiClockFrequency()` -- the shared G11/G12 bus and the LCD's SPI clock, the
two pieces of contention this project manages by hand (A2.16, rule 15). M5GFX
is now NAMED even though it only ever arrived transitively through M5Unified: a
dependency nobody names is a dependency nobody re-reads, which is the same
lesson ESP32Servo taught one migration earlier. Worth saying plainly: this
particular drift was NOT the cause of the touch failure -- the touch sources are
byte-identical across both bumps, and that was checked before anything was
pinned. It is pinned because a display stack that moves on its own is a
regression waiting for a quiet week.

`-Wno-error=return-type` is also carried by the three `-fire` environments.
They do not need it today -- none of them includes `stackchan-arduino` -- but
`extends` REPLACES `build_flags` rather than merging it, so the parent's copy
never reached them, and the day one of them touches `M5.Log` the error lands in
an environment whose own flags say nothing about the fix applied everywhere
else.

### The band grows a pomodoro that reminds you to drink, and six icons (08-24)

**Hydration.** `pomo_hydra_min` (1 min, 0 = off) slips a drink prompt between a
work block and its break. The placement is the feature: a prompt at the START of
the pause is one you act on while getting up; folded into the break it is a
label nobody reads, and put after it, it interrupts the return to work. It is a
PREFIX of the break and never a slice out of it -- the break that follows is the
full configured break, or enabling the reminder would tax you for drinking. The
last block has none. `0` restores the previous machine exactly, and the test
describing the classic chain now sets it to zero explicitly rather than leaning
on a default: the day that default moves, the test that fails is the one about
hydration and not the one about something else.

**Six phase icons**, 18x24 pixel-art stamps beside the digits: hourglass for a
countdown, brain for work, water drop for hydration, mug for the break, bell for
the ring, flag for done. ALL SIX COME FROM ONE DRAW CALL SITE (A2.22): the
glyphs are data and a single nested loop stamps whichever table the phase picks.
The obvious form -- an if-chain with a little loop each -- would be six similar
fillRect bodies in one function, which GCC 8.4 Xtensa may thin out, and the
symptom would be one phase whose icon silently never appears. `check-a222.py`
now pins `drawBandClock` at exactly two fillRect call sites.

The icon reads a NEW field, `tmr_ph`, published beside `tmr_st` rather than
derived from it: `tmr_st` is a colour code where break, hydration and done all
share green, so deriving the icon from it would give the drink prompt and the
break the same drawing -- the one thing the icons exist to prevent.

**A dance at the bell.** `timer_dance` (0 = none, else the 1-based index in
`/api/dances`) fires on Ring and AllDone only, never on a phase change: those
come round every few minutes and a robot that stands up that often is a robot
you unplug. The console fills its selector from the same fetch that draws the
dance buttons, so the list cannot drift from the robot's own.

**The access point forces the IP on screen.** A robot on its fallback AP is a
robot nobody can reach: no name resolves, the console is the only way to give it
a network, and that address exists in exactly two places -- the serial line,
which needs a cable, and this row of pixels. It OVERRIDES rather than writes, so
`band_debug` is untouched and the display returns to the user's choice the
moment a real network is joined.

**Two fingers held three seconds on the face** toggle that same row. Nothing
else on this robot wants two fingers, so it cannot be reached by accident, which
is the requirement for a control with no on-screen affordance. It suppresses the
one-finger path entirely while down: everything in `TouchGestures` reads finger
zero, which still presses and releases under a two-finger hold, so without that
the gesture would ALSO fire a tap or a swipe.

**A vertical swipe on the sound band cycles its three skins.** Same gesture,
same place, meaning taken from the mode already on screen; it was inert there,
and an inert gesture whose neighbours all answer reads as a fault.

### The platform migration, unblocked (08-24)

The switch to the pioarduino platform (Arduino core 3.x) left the firmware
unbuildable, and this is what it took. `ESP32Servo` is now PINNED at 3.0.x in
the two environments that pull `stackchan-arduino` -- companion and
flight-radar, the only two -- because the transitive 0.13 still calls
`ledcAttachPin`, `ledcSetup` and `dacWrite`, all removed from the new core. The
other guest bins never depended on it and compiled untouched. The companion also
carries `-Wno-error=return-type`: the new core builds with that as an error and
M5Unified 0.2.20 has a path without an explicit return, which is a defect of the
library rather than of this project, so the error is degraded rather than a
dependency file patched that `pio pkg update` would overwrite.

**And A2.22 had gone silent.** The classic platform ships one toolchain per chip
(`toolchain-xtensa-esp32s3`), the new one a single unified toolchain
(`toolchain-xtensa-esp-elf`). The hardcoded objdump path stopped existing, so
the gate printed one line of prose among hundreds and returned SUCCESS -- the
whole A2.22 check inert with `check-all` green, which is exactly the "passes
because it ran nothing" failure the file's own header warns about for an
unlisted env. Both layouts are searched now, and a missing objdump is a FAILURE:
a check that cannot run has not passed.

### /api/rules escaped all the way through (08-23)

Three of the four strings this endpoint emits come from `rules.txt` and were
written with a raw `%s`; only the description went through `jsonSafe()`. The
tokeniser splits on `|` and trims, nothing strips a quote, and `RuleStore`
interns the tokens verbatim — so one `"` in a field name closed the string
early and made the WHOLE document unparseable. The console's rules table then
rendered empty: a typo in one rule silently blanked all of them, with the cause
nowhere near the symptom. That is the exact failure `jsonSafe()`'s own comment
describes for control characters; the guard had simply been applied to one
member of a set it had to cover completely. `act` counts too — it embeds
`setKey`, interned the same way.

Escaped rather than validated, deliberately: field names resolve at RUNTIME
against `FieldStore`, sources register as they come up, and a rule may
legitimately name a field published later. A list of "valid" names checked at
parse time would be a second list drifting from the real sources — the assumed
twin A2.23 forbids. A serialiser cannot lean on an upstream invariant it cannot
see; it can always make its own output well-formed.

Four separate buffers, not one shared scratch: argument evaluation order is
unspecified in C++, so the four calls would clobber each other in whatever order
the compiler chose. The `tmp[448]` sum was re-derived too, since escaping can
double a length and the old arithmetic counted two of these raw — worst case is
now 433 bytes, and every term is a buffer size on the next line.

### The guest AP's captive portal, completed (08-23)

The wildcard `DNSServer` was there and the documentation described the portal as
working; the half that opens it was not. Every operating system probes a URL of
its own — `/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`,
`/canonical.html` — none of them a route in `SceGuest`, so all four fell through
to the WebServer's built-in 404. Android and Windows would eventually raise a
"sign in" notice on that, but Apple DISPLAYS the returned page in its portal
sheet: what a user got was the words "Not found" where the configuration form
should have been. `onNotFound` now answers a 302 to `/config`, which is the
expected body for nobody and therefore reads as a portal to all four.

Registered ONLY in AP mode. On a joined network the same handler would turn
every typo and every stale bookmark into a silent redirect to the settings page,
and a missing route has to stay a missing route.

### led-fluid reaches the Fire, and a review pass over yesterday's work (08-23)

**`led-fluid-fire`** — the same source as a standalone application on an M5Stack
Fire. This bin was described as unportable because its UI is built on sliding;
that was true of the interface, not of the bin. The port states what each gesture
MEANT and maps that onto three buttons: `firmware/led-fluid/input.h` carries the
vocabulary, pure and natively tested (`test_fluidinput`, 7 cases), and
`applyEvent()` is its single consumer. One thing genuinely differs and the
vocabulary says so rather than hiding it — poking the liquid needs a point, so a
tap pokes where the finger landed and a button pokes the centre. `SCE_HAS_PY32`
is a flag and not a probe, unlike the light sensor: probing the ring means
`Wire1.begin(12, 11)` first, and on a classic ESP32 the GPIO 6-11 are the SPI
flash.

**The LED bars were blind to half the turns.** `ledbars::displacement()`
hardcoded `frontIsZero`, so it returned a flat zero for every negative yaw rate.
`EmotionLeds` uses that value, and nothing else about the distribution, to decide
whether the bars are worth re-sending — so the write-suppression held the
previous picture for the whole of every turn in one direction, and the far end
never darkened unless the colour or an eye height happened to change at the same
moment. It is signed now, and the suite that agreed with the bug (it swept the
rate upward from zero and never once passed a negative one) has two tests for the
sign half.

**Boot calibration could never finish.** Counting only QUIET ticks removed the
completion guarantee the unconditional version had for free: `quiet` demands an
acceleration magnitude within 0.05 g of one, and a unit whose scale sits a few
percent off would never satisfy it — `_tilt` then stays zero for the session with nothing
logged. The wait is bounded now (20 s), the fallback is recorded rather than
hidden, and `/api/sensors` publishes `imu_cal` (0 calibrating / 1 done / 2 done
on the deadline).

**Flipping a default is not a migration.** `saveConfig` rewrites every key, so a
card already on a robot carried `tilt_inv_x = 1` — the value users were told to
set when the mapping lived in a switch instead of the baseline. Under the old
names the upgrade would have silently re-inverted X on exactly the robots that
had been set up correctly. The keys are renamed `imu_*` → `tilt_*`, which IS the
migration: `loadConfig` ignores names it does not know and `saveConfig`
truncates.

**The fluid started on the floor.** `reset()` sized its lattice by dividing the
box by a step, so the column count had to be floored and the rows then needed
could overshoot the height — the clamp welded the last row to `y = WORLD_H` in
the first frame of the animation. Integer counts cannot overshoot.

**Three comments described code that had been reverted**, each citing a
measurement that does not hold. Re-measured over a settled 400-grain fluid, with
the closest pair as a fraction of the grain diameter: carrying the previous
position through a contact correction gives 5 % against the shipped 62 % at pitch
8 (and mean speed 49 px/s against 7) — the projection needs the velocity
coupling; sweeping the deepest cells first buys pitch 8 (72 %) and pays for it at
pitch 12 (75 % against 88 %), and is only "deepest" while gravity points down a
screen that an IMU is tilting; visiting each pair from both ends spends the
examination bound twice as fast and halves the settled gap. The code was right
and the prose was not. Also measured, and worth knowing: `MAX_TOUCH` 48 vs 96
changes nothing by a printed digit — it is a guard, not a tuning knob — and the
pass count is non-monotone at pitch 8 (62 / 51 / 69 % for 4 / 8 / 16), so that
packing is an oscillation rather than a shortage and extra passes buy nothing.

### The tilt reflex, corrected on three counts — and the bars gain a depth (08-22)

Measuring the physical direction of the accelerometer's in-plane axes with
`led-fluid` made three faults in `ImuReader` readable that had been invisible
while there was nothing to compare against.

**The baseline was eating real tilts.** `_baseY` tracked at 0.0002 per tick
whenever the robot was "quiet" — and a robot HELD STILL on a slope is quiet:
gyro at zero, one g of acceleration. At the brain's 100 Hz that is a fifty-second
time constant, so a sustained tilt was absorbed into its own baseline, 63 % gone
after fifty seconds and the eyes back at centre inside two minutes.
`VestibularSystem` says the opposite in as many words ("At rest on a slope the
eyes HOLD that target — not zero") and VALIDATION.md's *static tilt held* was
checked over seconds, which is the one timescale on which both were true. The
comment called it thermal drift, and that is the fix: thermal drift wanders
AROUND the rest pose, so the tracking is now gated on staying near it, and slow
enough (~8 min) to be what it claims.

**The two axes disagreed.** `_tilt.x` sent the gaze to the high side —
counter-rotation, what an otolith does — while `_tilt.y` sent it up when the
robot tipped back, which is the opposite convention. Both counter-rotate now.

**X had no baseline at all.** Only Y was calibrated, because at rest upright
`accel.x` is nominally zero — which is exactly why its absence was invisible: any
mounting bias went straight into the gaze as a permanent off-centre offset. Both
axes are calibrated now, and the boot calibration counts QUIET ticks rather than
the first hundred to arrive, so booting in somebody's hand no longer calibrates
the hand.

**The bars gain a second dimension (§10 P1+P2).** The twelve WS2812 are two bars
of six running PERPENDICULAR to the screen, and the firmware had only ever
written one number per bar. The brightness already computed stays the bar's
amplitude; a new pure header (`engine/LedBars.h`, `test_ledbars`) decides only
how that amplitude is DISTRIBUTED along the depth. While the head turns, the far
end darkens. The redistribution subtracts and never adds — a bar can legitimately
sit at 255, so a weight above one would clip and the total would collapse in the
very case the effect matters most. Off by default (`led_depth`), with
`led_depth_front` for the depth wiring nobody has measured yet, and the
write-suppression now watches the distribution too. 360 cases in 31 suites.

### `led-fluid`: the fluid had no gravity and no volume (08-22)

Three faults, reported from the robot as one sentence — "the particles make a
thin line on the edge, and it is the edge opposite the ground".

**Nobody was reading the IMU.** `M5.Imu.getImuData()` only CONVERTS the driver's
raw buffer; `M5.Imu.update()` is what reads the sensor, and the repository's
only caller was the companion's Brain. A guest has no Brain, so the gravity
vector stayed frozen at whatever the struct was born with — no error, no zeroed
values, just a liquid that read exactly like one seen from above. The rule now
has its own section in the guest contract, in both languages.

**The pressure was in the wrong units.** The stiffnesses were the paper's — a
world a few units across — dropped into a world 320 pixels across, where they
were worth about 13 px/s² against a gravity of 900. The fluid had no way to
hold itself up and fell into one row of dots. Every length in the solver is now
a multiple of the dot pitch and every stiffness an acceleration in px/s²,
comparable with the gravity it has to carry. The interaction radius follows the
grain, not the render pitch: tied to the pitch it let the dot-size slider retune
the physics, and it made the neighbourhood grow with the particle slider until
400 grains sat at `MAX_NEIGH` at REST — the cost ceiling then truncated ordinary
fluid and it boiled instead of settling.

**No two grains share a dot.** A grain's diameter is the dot pitch, and a
non-penetration pass holds the centres apart. It runs AFTER the walls, which is
the difference between a constraint and a suggestion: run before them, the wall
clamp put every grain the projection had pushed through the floor straight back
into the neighbour it had just left, once per step for ever. The pass has its
own neighbour grid at its own scale — borrowing the pressure's, it spent its
whole examination bound on grains that were nowhere near touching — and the
count is capped by what the pitch can actually keep apart (400 becomes 153 at
20 px). The splat became bilinear at the same time: a grain shares its light
with the four dots around it, so motion between two dots fades rather than
jumps, and the body of the fluid gets a soft edge.

**The axis mapping is measured, and the switches say so.** `gx = -accel.x`,
`gy = +accel.y`, verified on the robot upright — carried in the baseline that
reads the vector rather than as a default of `imu_inv_x`, so a correct robot no
longer opens its settings announcing that an axis has been inverted. A switch
has to mean *depart from what is right*. The three of them moved from no panel
at all onto the **RENDER tab**: they duplicate no other widget, and the person
who can see which way the fluid runs is holding the robot, not reading `/config`
on a laptop. The hidden tab now holds only what already has a widget — hue,
saturation, brightness.

Four native tests were added and all were shown to fail on the code they pin:
the settled fluid must be a body several dots deep, no pair may sit closer than
three quarters of a diameter, the grain count must answer with what it can hold
apart, and every RENDER row must be a toggle (a slider put there would be drawn
as a switch and flattened to 0 or 1 by the first finger). 353 cases in 30
suites.

### `led-fluid`: a fourth guest bin, and two failures the hardware found (08-18)

**What it is.** Liquid in a box: a particle fluid whose gravity IS the tilt of
the robot, painted as a grid of round dots — density gives a dot its colour,
speed gives it its light. Shake it and it splashes, tap it and it pushes away.
Physics panel on a swipe right (viscosity, gravity, bounce, trail, five
presets), hue/saturation rectangle on a swipe left, and the twelve WS2812 of
the K151 can echo the fluid. Documentation:
[`guests/LED-FLUID.md`](docs/guests/LED-FLUID.md), specification `ROADMAP §9`.

**One table, four consumers.** The panel that draws a setting, the hit-test
that finds it, the yaml that persists it and the `addSetting()` that publishes
it on `/config` all read `ui::params()`. `check-guest-config.py` was written
after a key that `loadConfig` read and `saveConfig` did not write was destroyed
on the first save from the web page; this bin cannot have that bug. The gate
looked for literal `addSetting("key"` calls, so adding this bin to its list
would have made it **pass while checking nothing** — the exact silence it
exists to break. It now reads a table too, and checks what stays fallible: the
two hand-written `strcmp` chains bridging the table to the running state.

**The task watchdog, and why a yield must be unconditional.** The bin boot-
looped on hardware at 20.5 s, `CPU 0: fluid-sim`. `vTaskDelayUntil` blocks only
while its deadline is ahead: the first step that outran its period returned
from it at once and, with the deadline still being advanced into a past already
gone, it never blocked again. The task spun, the idle task of its core was
never scheduled, and the watchdog aborted the chip. An overrun now re-syncs the
deadline and serves a one-tick floor.

**A neighbour grid is not a cost ceiling.** It bounds the pair count only while
the particles are spread out, and gravity spends its whole life doing the
opposite — once a pile fits inside one cell, every pair is a neighbour again
and the step is quadratic after all. That is what made the step overrun its
period in the first place. Neighbours per particle are now capped, and
`test_fluid` compacts 400 particles into a corner to assert the ceiling holds.

**Measured, not estimated.** At the finest pitch (8 px, 1200 cells), gravity
driven in a circle so the fluid never settles: worst paint 16-18 ms, worst loop
26 ms with an occasional 38 ms outlier from the network stack, 50 painted
frames per second. The specification's arithmetic guess of ~26 ms for the paint
was replaced by the number.

**Twelve LEDs stuck white, reported from the robot.** A WS2812 latches its last
colour and keeps it across a reboot and across a reflash, so three separate
moments were leaving the ring lit: switching the echo option **off** merely
stopped writing, handing control **back to the companion** left the last frame
burning on a robot whose companion drives those LEDs only when its own option is
on, and — the white itself — **probing the expander at boot** configures the
data line with a pull-up and announces twelve LEDs, which is enough for a chain
that has never been written to latch noise. All three now black the ring out
through the one function that talks to the colour RAM.

**Five defects an independent review found, and none of them were visible from
the outside.** What writes code and what re-reads it share their blind spots, so
the change was reviewed through four separate lenses before it landed. The
parameters the panel edits were a plain struct that only the other core read —
nothing in the simulation task writes it, so a compiler was entitled to hoist
the reads out of the endless loop and a slider would silently never have reached
the fluid, in optimised builds only; they are published through an atomic
generation now, and the solver says on serial which values it adopted. The tap
coordinates were a torn pair (the task gates on X, so waking between the two
stores splashed at the new X and the old Y). Two frame buffers were not enough:
the producer alternates, so after two flips it writes the one the painter is
still reading — which is not a corner case, since returning from a panel forces
a full repaint that outlasts two periods; the frame is copied now. That full
repaint was also unbounded at ~112 ms, a third of a second of frozen touch and
dark HTTP, and is now spread over frames by a per-frame dot budget. And the
neighbour cap was charged only to the outer particle of a pair, so it was a cap
on LOW INDICES — measured at 24 pairs for particle 0 against 59 for particle 59
in one pile; since the index has nothing to do with position, two identical
piles damped differently depending on who was numbered first. The first test
written for that fairness could not fail (it measured budget spent, which is
bounded by construction); it measures participation now, and it does fail
without the fix.

**Shared rather than duplicated.** The PY32 LED payload — the RGB565 rounding
and the little-endian order of the twelve entries — moved to
`firmware/common/Py32Leds.h`, included by the companion and by the bin; only
the `Wire` transaction stays on each side, with the bus lock where there is a
second task and without it where there is not. The companion gained per-LED
addressing on the way: its colour RAM always carried twelve entries, only the
API pretended otherwise.

Native tests go from 27 suites / 324 cases to **30 / 349**; `check-all.ps1`
builds seven firmwares and checks A2.22 inside all seven binaries.

### Four documents the corpus was missing, and a gate so its lists stop rotting (08-17)

Found by auditing the corpus rather than assuming it.

**[`reference/API.md`](docs/reference/API.md).** The firmware serves **44
method+path pairs** and the only complete description was the OpenAPI the robot
generates from its own code — so learning the API needed a running robot. The
page does not copy that document (a Markdown copy would drift); it gives the
routes by family and, more importantly, the four conventions a list cannot
convey: everything is a query parameter, errors are bilingual and coded,
anything heavy is deferred — **a `202` means accepted, never done** — and route
order is a real constraint the router enforces at boot.

**[`reference/EMOTIONS.md`](docs/reference/EMOTIONS.md).** The 15 dances have had
their table for a long time; the 30 expressions were named in eight documents
and listed in none, so writing a rule meant opening `Emotions.h`. Two
behaviours worth knowing are stated with them: an unknown name is refused with
`404`, never quietly mapped to `Normal`, and an explicit emotion **aborts a
running dance** — otherwise the dance's next keyframe would overwrite it a
fraction of a second later.

**[`reference/SECURITY.md`](docs/reference/SECURITY.md).** The pieces existed in
three files; nothing said in one place what the robot exposes **with no
password**: the SD card in read *and* write, the flash, the camera, the power
switch. It claims no hardening — there is no threat model beyond "a trusted
LAN", and pretending otherwise would be worse than saying so — but it does say
that the fallback access point ships with known credentials, and that `cors=1`
lets any page you visit call your robot.

**[`CONTRIBUTING.md`](CONTRIBUTING.md).** This project is held by eight
gates with strong opinions, and a newcomer discovered them by making them fail.
`CLAUDE.md` filled that role, but it is written for an agent rather than for a
person.

**`check-doc-coverage.py`** holds the documents that ENUMERATE what the code
defines: the 44 routes of `WebApi::route()`, the 30 values of `eEmotions`, and
the 15 dances of `dances::table()` — that last table already existed and was
watched by nothing. **Both directions** are checked, because they fail
differently: a missing line hides a route, an extra one promises an API that
does not exist. Falsified on both directions and on two lists before being
trusted. The three tables turned out to be exact on the first run, which was
not a given.

Each new document is also linked **from where the question arises** rather than
only from the maps: the `api:` block of `CONFIG.md` now points at the security
page, the rule actions of `PLUGINS.md` at the two catalogues of names they
accept, and the `emotion` column of a keyframe at the list it draws from.

---

### The console names what moves the head, and the docs grow a hardware rubric (08-17)

**Two switches, one thing.** `Head-follow` and `Sound tracking (head)` sat side
by side in the console — the two settings that make the head move on its own —
and neither name said what *drove* it. They now name their trigger: **Head
follows the eyes** and **Head turns to noise**. Two smaller faults surfaced
while renaming: `Head-follow` carried no `data-i18n` attribute at all, so it
was the one label in that block that stayed English in the French console; and
the tuning category was the same string as the toggle for a group that holds
the tracker's thresholds.

**[`docs/hardware/`](docs/hardware/README.md), four EN/FR pairs.** Most of the
hard-won knowledge in this project is not about code — it is about a board
where two logical buses share one physical pair of wires, where a servo end
stop destroys the servo, and where the ambient light sensor is behind a wall.
That knowledge was scattered across `hal/` headers, `CLAUDE.md`, and three
sections of `CONVENTIONS.md` that had no business being there. The rubric
covers the full parts inventory with its buses, the boot order and why it *is*
an order, what sharing each bus costs, every part with its calibration and its
**failure mode**, and a table of what was tried on this board and **failed**,
each with the measurement that closed it. The three misplaced sections — the
IMU axis map, the I2C topology, the partition table — were **moved**, not
copied.

It answers a question that kept coming back: **why ±130° of yaw when the head
turns a full circle by hand.** Nothing mechanical stops it. `writeDeg` maps a
0–300° span onto a 10-bit register and clamps there; with the centre at 166 the
reachable envelope is +134/−166, so ±134 symmetric, minus four degrees of
margin. Going further needs the servo's multi-turn mode, not a bigger constant.

**[`reference/EYES.md`](docs/reference/EYES.md)** — the face, from a decision to a
lit pixel: eye geometry, the animation chain and why its transformation stage
carries no ramp, the lid as a channel separate from scale, transitions,
blinking, the roulette (including the night mode, where `Normal` drops to zero
and `Sleepy` takes two thirds of the draws), LED synchronisation, and the
arithmetic behind the dirty-band push — 20.5 ms of pure wire time against
3.5 ms of drawing.

**A map that promised what it did not have.** `docs/README.md` advertised
`WORKFLOWS.md` as covering *flashing*, and no such section existed.
[`WORKFLOWS.md §12`](docs/architecture/WORKFLOWS.md) now draws the chain that swaps
the running firmware — launch, lobby, cooperative stop, return — and names the
trap in its last arrow: coming back reflashes the companion **from the SD
card**, so a stale copy silently overwrites a firmware just flashed over USB.

Also: the choreography editor is announced at the **top** of
`CHOREGRAPHIES.md` rather than buried at §6 — writing a dance by hand was
never the intended path; `SPACE.md` was still describing the four-term lunar
latitude series, with the sign the code carried before it was corrected; and
`sdcard/` gained the missing `space.yaml` listing while losing a phantom
`charge_led` key and a build command naming an environment that no longer
exists.

---

### Two audits, and what they found (08-16)

**The Moon's latitude was wrong enough to misgrade eclipses.** `moonInfo`
carried four terms of Meeus's latitude series, an error of 0.12° measured
against his own worked example (47.a) — and the umbral grader discriminates
total from partial on a quantity whose whole range is about a quarter of a
degree. Graded against the NASA canon, the shipped series called **five of the
eight umbral eclipses of 2023–2028 wrong**: three missed entirely, one partial
announced total, one total announced partial. The `2D − F` term also carried
the sign opposite to table 47.B. With eight terms, all eight are right. The
test that covered this pinned the *wrong* answer — it accepted "at least
partial" for a total eclipse and said so, excusing itself with "against a
truncated series", so the gate stayed green over the bug. It now asserts the
exact grade from both sides, and a new test pins the latitude itself against
the reference, where the defect actually lived.

**One swipe classifier instead of seven.** The same question — was that a
swipe, and which way? — was answered in seven places across four firmwares,
with five thresholds and three tie-break rules. A threshold here is not a
preference but a contract with the gesture above it: SceGuest owns the
downward swipe past 100 px, so a bin's own gestures live in the band below,
and when the two numbers sit in different files nobody compares them. That gap
is where a real drag was read as *a tap at the finger's origin*.
`firmware/common/Gesture.h` holds both distances side by side, one tie-break
rule (ties go to the vertical axis, where the primary navigation lives), and
the press budget; a native suite asserts the band between them is real. One
exception survives and is now expressed rather than endured: swipes starting
on the radar's panel keep 40 px, because the exit is disabled there and the
band does not exist.

**A card inserted after boot was never seen.** On `space` and `ha-remote` the
boot mount stood for the whole session: settings silently stopped persisting
while `/config` went on offering to save them, and a card pulled out was never
noticed. The radar had grown a hot-plug probe; `firmware/common/SdWatch.h` now
holds it for all three, the radar included — extracting a mechanism and leaving
the original behind is how three copies become four.

**Blocking writes off the wrong threads.** The tuning trace was a blocking
`Serial.printf` inside an AsyncTCP callback — forty lines below the comment
forbidding it — and five ha-remote traces ran with the mutex `netTask` waits
on. One deferred queue in `Trace.h`, drained by `loop()`.

**ha-remote joins the other two** on auto-brightness (shipped **off**, because
it is new on a screen held in the hand), on the shared `loadCompanionLang`, and
on the rule that no card is not a transient write failure — its own roster
block eight lines below already made that distinction.

**New gates, each falsified before being trusted.** `check-guest-config.py`
holds a guest setting to the four hand-written lists it must appear in; the
fourth is the dangerous one, since `saveConfig` **truncates** and a key the
yaml can be read from but the save never writes is *destroyed* on the first
save from `/config` — the 08-05 `auto_bright` outage, whose lesson had been
written in a comment and never tooled. `check-mirrors` gained the "Liquid
Glass" palette in its three C++ copies (one of them annotated "synced BY
HAND"), every quoted native-test floor, and the rule that a bin calling
`SD.begin` must hold an `SdWatch`. `check-contrast` now measures that palette
instead of taking three comments' word for it. And `flight-radar-fire`'s A2.22
table, whose comment claimed it was *derived*, was a hand list of three entries
against six — it is derived now, and the gate went from 87 pinned points to 97.

---

### Every firmware can narrate itself (08-15)

A runtime **debug trace** now exists everywhere: `firmware/common/Trace.h`
holds the one implementation (`sce::trace::log("tag", …)`, printf-style,
gated by a single boolean — off, each site costs one test), vendored into
`SceGuest.h` as its fourth held copy. Tags name the subsystem: `net`, `cfg`,
`http`, `sd`, `ui`, `task`; lines read `[dbg][tag] +millis message`; secrets
never appear — URLs are cut at the query string, tokens reported only as
present/absent.

Activation is runtime on purpose: the moments that need a trace — a robot
that will not join, a fetch chain failing in the field — are the moments a
reflash is unavailable or would destroy the evidence. On the companion it is
the `debug` tuning key (persisted, applied within a frame, synced before
`api.begin()` so a persisted flag narrates the very first WiFi join), with
two console switches — one beside the telemetry toggle, one in System. On
the guests it is a framework **Debug** checkbox on `/config`, stored in NVS
like the network override and for the same reason, applied at once. The
contract (`docs/guests/README.md`) documents both the checkbox and the
`sce::trace::log` call every bin can join the narration with.

The three guests are instrumented end to end — each bin's canonical HTTP
helper traces every attempt with host+path, status, duration and size, plus
its silent exits; config loads report recognised and unknown keys; SD writes
report their silent failures and their successes; UI actions name
themselves. The companion narrates its WiFi sequence and every tuning change
a remote client makes.

### The A+C debug overlay reaches space, and the button bank becomes one (08-15)

The full-screen diagnostic overlay held under **A+C** existed only on
flight-radar — investigating "space's debug mode disappeared" showed it had
never existed there, and worse: space's three independent per-button FSMs
would fire BOTH long actions on the chord, and lacked the power-on priming,
so a Fire booted with a button held fired that button's long action
unasked.

The radar's multi-button bank (priming, debounce, one-event-per-call,
`chord()`) moves to **`firmware/common/ButtonFsm.h`** — one implementation,
two bins, no more twins — with the radar's API unchanged (its tests run
untouched). Space adopts the bank, gains the chord, and gets its own
overlay: network, clock sync, TLE age and source, launches diagnostic,
passes, light sensor, heap, uptime and the build identity, released the
moment the chord is. Three new native cases pin the fixes: a button held at
boot stays mute until released, a chord fires no individual action, and the
buttons speak again after it.

---

### The Moon diagram tells the truth, eclipses included (08-15)

Two corrections to the Sun-Earth-Moon figure, both born from reading it
against a textbook plate.

**The Moon on the orbit is now half lit, facing the Sun** — exactly like the
Earth beside it. The previous rendering drew the phase as seen from Earth (a
67 %-lit gibbous disc sitting on the orbit), which matched the percentage
printed beside it but took a liberty with the one thing the figure claims:
the angle, and the lighting that angle causes. The appearance keeps its two
homes, the phase strip below and the big percentage; the strip's own
renderer (`drawMoonDisc`, vertical terminator) already covers them, so the
tilted-terminator generalisation (`drawPhaseBody`) lost its only caller and
is gone.

**During a real umbral eclipse, the Moon goes copper.** The naive version —
darken the Moon whenever it sits behind Earth — would fire at every full
moon, wrong twelve times out of thirteen: the 5° orbital inclination that
makes eclipses rare is exactly what a top-down figure cannot show. The real
test uses the ecliptic latitude `moonInfo` already computed and threw away:
angular separation from the anti-solar point against Meeus' umbra radii,
total when the whole disc fits inside, partial when it merely overlaps. The
figure keeps its call sites (A2.22): only the colours change hands, and an
`eclipse` row leads the fact column while it lasts. Validated natively
against the canon: the total of 2025-09-07 reads total, the barely-total of
2026-03-03 reads umbral, and the very next full moon — 98 % lit and
untouched — reads clear.

Solar eclipses are deliberately not flagged: they are a narrow ground track,
and a geocentric figure claiming one for this observer would usually be
wrong.

---

### The radar's auto-brightness reasserts itself (08-15)

A settings save applied the manual brightness unconditionally, and the
sensor loop only re-corrected when ambient moved more than 6 steps against a
stale hysteresis value — under auto, the screen stayed at the manual level
for minutes to hours. The fix `space` already carried (a reset flag raised
by both apply paths, consumed by the sensor loop) was missing here; manual
brightness now applies only when auto is off, and the ruling source
reasserts itself within a second.

---

### One cooperative stop instead of three hand copies (08-11)

The `netStop`/`netParked` pair that parks a guest's network task before a
reflash was hand-rolled in all three bins, and the copies had drifted four
ways: the ACK window (legitimate, per-bin), the warning when the task never
parks (two of three), the fresh-ACK reset (two of three) — and resume after a
FAILED reflash, which only the radar had, as a 15-second timer: ha-remote and
space left their task parked *forever* if the reflash failed, a half-dead
guest with no network.

`sce::CoopStop` in `SceGuest.h` is now the one implementation: the bin
declares the guard, parks its task with `shouldPark()`, refuses new requests
with `stopping()`, sets its window, and hands the guard to
`guest.netGuard`. `SceGuest` parks it before `updateFromFS` and **releases it
on the failure path** — deterministic, replacing the radar's timer guesswork.
The per-bin ACK window (3 s radar, 20 s ha-remote, 25 s space) stays the one
legitimate variation, each sized on that bin's longest single request.

The recurrence is what the incident was actually made of — a discipline
applied to one bin and silently missing from another — so the rule is now
checked, not remembered: `check-mirrors.py` fails any guest `main.cpp` that
calls `xTaskCreate` without wiring `guest.netGuard`, and the contract section
of `docs/guests/README.md` teaches the three-line version.

The audit behind the question "is everything tied to a bin properly unloaded
on exit?" closes cleanly: exiting a bin ends in `ESP.restart`, which is the
ultimate unload — the only window that matters is stop→reboot, and in it the
guests provably hold one task each (now guarded), no timers and no long-lived
file handles, while the companion side already quiesced systematically before
flashing a guest (camera inhibited and drained, renderer paused, servo rail
cut, and A2.6 keeping AsyncTCP callbacks off the SD by construction).

---

### The road back to the companion no longer crawls (08-11)

`space` created a network task and never parked it — the one guest of three
missing the radar's discipline. During `/api/bins/stop`, that task kept its
TLS fetches and cache writes running while `updateFromFS` read
`/companion.bin` from the SD card and wrote flash: two contenders on one
SD/SPI bus, and the reflash that is the only road back to the companion
crawled from ~9 seconds to **more than ten minutes** — HTTP dark the whole
way, ping alive, indistinguishable from a crash from the network side.

The fix is the radar's cooperative stop, applied identically: a `netStop`
flag the task acknowledges by parking (never `vTaskSuspend`, which can freeze
a mutex holder), a bounded wait sized on the longest single request, and the
same flag refusing new requests inside `httpGet` so a fetch chain cannot
outlive the window. Measured after: tasks parked in **141 ms**, 1.4 MB
reflashed in **8.5 s**, companion answering **14 s** after the stop.

`stopToCompanion` now narrates on serial — tasks stopping, task-stop
duration, file size, explicit "HTTP dark until reboot" — because that window
has no other narrator: the synchronous web server is dead by design while the
flash runs. And `onBeforeStop`, which existed as a hook two bins used and one
forgot, is now a documented **requirement** of the guest contract
(`docs/guests/README.md`) for any bin that creates a task, with the sizing
rules spelled out.

---

### The robot says which build it is running (08-10)

`GET /api/firmware` publishes five values, and the console shows them in
**System › Firmware**, above the flash control — the state of what is running
belongs before the button that replaces it.

| field | what it settles |
|---|---|
| `slot` | the OTA partition actually booted (`app0`/`app1`) |
| `sha` | first 8 hex of the app ELF sha256 — this exact build |
| `console` | digest of `WebConsole.h`, the one `gen_console_gz.py --check` prints |
| `reset` | why it last started (`poweron`, `sw`, `panic`, `task_wdt`, `brownout`…) |

`slot` is the field that matters. A USB flash writes **one** OTA slot and never
touches `otadata`, which is what chooses the slot to boot — so a flash can
report success, verify its own hash, bring the robot back on WiFi, and still
leave a different firmware running. Every outward sign said the update had
landed; only a dump of the flash said otherwise.

Both fingerprints are **reproducible from a working copy**, which is what makes
them useful rather than merely unique: `sha` is `sha256sum firmware.elf` (first 8
hex) and `console` is what `gen_console_gz.py --check` prints. One request
against one command answers "is this robot running my working copy?", with no
ELF surgery and no flash dump.
Re-gzipping the sources to compare instead gives a false negative — the system
Python's zlib and PlatformIO's produce different (both valid) deflate streams
from identical input.

There is deliberately no build date. The obvious source,
`esp_ota_get_app_description()->date`, is the date the *precompiled Arduino
libraries* were built — it answered "Mar 5 2024" on a firmware compiled minutes
earlier. A field that looks authoritative and is wrong is worse than no field,
and it is the exact failure this work exists to end.

A crash-shaped restart is a finding rather than a value, so `panic`, the three
watchdogs and `brownout` are shown in the error colour. All five are fixed for
the whole boot, which is why this is its own route and not five more fields in
the `/api/status` the console polls every two seconds — and why the console
reads it once per page load.

The same block now opens the serial log, which until now printed the reset
reason as a bare enum. Both come from one `FirmwareInfo.h`; nothing here is new
information, the chip knew all of it, it simply had no way out — and the serial
port is not a way out, since opening it on native USB resets the board and
destroys the evidence you came for.

---

### The two CLAUDE.md are held together (08-10)

The file exists at the repo root and in `.claude/`, and nothing made the two
agree — they were kept identical by hand, which is the definition of the twin
that drifts. `check-mirrors.py` now compares them whole and names the first
line that differs.

---

### The header becomes one row (08-10)

Identity on the left, destinations on the right. It was two rows, and since the
header joined a **pinned** block every one of its pixels became permanent: the
brand is read once, the tabs are used all day, and stacking them charged the
whole session for both.

`align-items:flex-end` is what makes it work rather than merely fit — it lands
the active tab's underline exactly on the header's own bottom border instead of
leaving it floating above. Below 720 px the two no longer fit side by side, so
the tabs drop to their own line and take the full width rather than being
squeezed into whatever is left.

---

### The telemetry is pinned, and its graph has room (08-10)

The chart and the status chips scroll away no longer: the header and the
telemetry band are now **one sticky block**. Two stacked `sticky` elements would
have needed the second one's `top` to equal the first one's height — a number
nothing measures, and one that changes with the font, the language, and whether
the tab bar wraps. A single sticky ancestor contains no such number.

The graph goes from 72 to **132 px**. At 72 the four traces spent most of their
travel within two or three pixels of each other and the shape of a memory dip
was guesswork; height is the only dimension a line chart has. It steps back down
on short screens (96 px under 820, 72 under 640) so a pinned block cannot eat a
laptop, and the whole band still folds away when it is in the road.

One defect fixed with it: `drawChart` took only the WIDTH from the CSS box, so
the canvas kept its 56-pixel bitmap and a taller box merely stretched it. Both
dimensions now follow the box — otherwise "bigger" would have meant "blurrier".

---

### Two controls found each other, and the Sound mode can turn its own mic on (08-10)

*"Head-follow is in Pilot and Sound tracking (head) is in Options."* Both are
what makes the head move on its own; sound tracking sat next to the microphone
because it needs one, but `setTrack` already switches the mic on when you enable
it, so the adjacency bought nothing and the split cost a tab. It now lives in
the Head card beside Head-follow and Servos.

*"In Status band → Sound I have no way to turn the microphone back on, or to
know how."* The mode drew *mic idle* and a sentence pointing at another tab — an
instruction to go somewhere else and come back, for the one switch the mode
cannot work without. It now says what the microphone is actually doing (off,
warming up with the countdown, standby because the speaker holds the audio bus,
or listening) and offers the single action that changes it. When the mic is
already listening there is nothing to offer, so the button is gone rather than
greyed.

**And the two rule buttons stopped colliding with the Files tab.** One of them
*was* a duplicate — the file list already offers `↻` on `rules.txt`. The other
was worse than a duplicate: *Refresh the list* existed word for word in Files
for a different list, the card's. Both now name their object: **↻ Re-read
rules.txt** and **↺ Refresh this table**. Keeping them where the table is beats
sending someone to another tab to act on what they are looking at.

The guide's value headings needed to look like headings, too: `field`, `op` and
`action` each open a list and read as stray code chips. They now carry the
guide's own heading vocabulary — a rule above, the translated part in small
caps — while the column NAME stays in code, because that is what you type.

---

### The how-to becomes one annotated example (08-10)

Teaching nine columns and then showing an example asks the reader to hold nine
definitions in their head before anything means something. It now runs the other
way: one real line, taken from the file the robot ships with, read out loud in
one sentence, then each of its nine tokens against the column it fills and what
it means **in that line**. The reader points at the thing being explained.

Then the values, as lists rather than prose: every field the robot itself
publishes with what its number means, the six operators, and each action with
its arguments. That is what "what can I put here" wants — a list to scan, not a
paragraph to parse.

---

### The Rules section, reordered for someone arriving (08-10)

*"C'est un peu le fouillis."* It was: a format line, a worked example, an
eight-row column reference and three paragraphs of caveats — all true, none of
it what you want on the nine visits out of ten where you only came to see
whether your rule is loaded.

So the order now follows what people actually do. One line of context, the two
buttons, **the table**. The how-to is folded underneath for the tenth visit, and
inside it the reference no longer comes first either: three numbered steps in
the order you perform them — pick a field, write the line, put it on the card
and reload — then the columns, then the three things worth knowing. The worked
example sits in step 2 where you need it, with the sentence that reads it out
loud.

The intro paragraph is the user's own wording, and it is better than mine for a
reason worth keeping: three short sentences instead of one compound, and it
opens with WHERE the file is, so the reader knows what they are looking at
before being told why it exists. *Poster* an action became *trigger* one —
"post" is our vocabulary, not theirs.

One defect of my own, caught in the screenshot: the folded guide is a `details`
inside a `details`, so its content inherited the glass card of the outer one.
Glass on glass reads as a rendering bug. I had neutralised it for the summary
and forgotten the body.

---

### The rules table names its own columns (08-10)

The guide talked about `sustainMs`; the table said **TENUE**; nothing on screen
connected the two. And the French guide had translated `field` into *champ*, so
one thing carried three names across the table, the guide and the file the
reader was supposed to type.

The header is two storeys now. The first says what a column is FOR and is
translated; the second is the field's NAME exactly as it appears in
`rules.txt`, in code, and is **never** translated. `Condition` spans its three —
`field`, `op`, `value` — because that is what a condition is made of, and
splitting them is what lets the guide name each one.

The guide was rewritten around that. It is keyed by the column names, so the
reader who wonders what *TENUE* means finds `sustainMs` directly above it and
`sustainMs` again in the guide. It gained a worked example read out in prose —
*while `claude` is on, if `ctx` is at or above 90 for three seconds…* — and a
row for `a1`/`a2`, which were mentioned in the format line and explained
nowhere.

---

### Seven defects the audit found, five of them mine (08-10)

A comparison of the three guest bins against each other, and of every
PowerShell script against its Linux twin. Both reported things that were true
rather than things that looked wrong.

**The two that were bugs on the robot.** `flight-radar`'s config save reported
success unconditionally: `SD.open(FILE_WRITE)` truncates the file, so on a full
or write-protected card `/config` said *"Saved"* over settings that no longer
existed. `ha-remote` paid for exactly that in July and both it and `space` have
checked `getWriteError()` ever since — this bin never got the fix. And `space`
guarded `SCE_WIFI_SSID` and `SCE_WIFI_PASS` behind a **single** `#ifndef`, which
is the trap `SdPins.h` was written to forbid three lines above it: a profile
supplying only the password gets it silently redefined to `""`.

**And five in the Linux scripts, four of them written this session.**

`claude-statusline.sh` took the transcript's last **400 kilobytes** where its
twin takes the last **400 lines**. A single entry carrying a tool result runs
past 100 kB, so the window could hold four lines against the twin's four
hundred; finding no usage in them it pushed `claude=0` and every shipped rule
went dark — on Linux only, which is the worst kind of difference to notice. The
window now grows from 256 kB to 8 MB until it finds one. The same script
squashed spaces to underscores to survive `read`, which turned a directory named
`my_project` into `my project`; it is tab-separated now, and nothing is
rewritten. Its cost gauge truncated where the twin rounds, so $0.126 showed 2
here and 3 there.

`test-mag.sh` required `mag_x`, `mag_y` and `mag_z` to be **adjacent and in
order** in the JSON. Reorder one line of firmware — a change nobody would
connect to this — and it captures zero samples while the PowerShell tester keeps
working, so the two disagree about a sensor that is fine. And its empty-input
path emitted thirteen fields where both readers name twelve.

`endurance-log.sh` reopened the serial device **on every line**: everything
arriving between the close and the next open was dropped, with no marker, from a
capture whose whole purpose is noticing what happened overnight. One open for
the run now, like the twin.

`statusbar-push.ps1` parsed numbers with `[double]::TryParse`, which follows the
current culture. On the fr-FR machine this repository lives on, `"62.5"` is not
a number: every decimal gauge value was being suffixed `_s` and pushed as
**text**, and the gauge stayed at zero without saying so. Invariant culture now
— verified against the live setting rather than assumed.

`check-all.ps1` read the **first** `test cases:` match and required *succeeded*
on the same line, where the bash twin reads the last with no such requirement.
Both feed the same `MIN_TESTS`, which `check-mirrors.py` works to keep in sync —
measuring it differently on the two platforms empties that mirror of its meaning.

---

### Three defects of my own in the rules work (08-10)

Found re-reading what I had just written, and each verified on hardware rather
than argued about.

**The chunked cursor belonged to the server, not to the response.** As a member
of `WebApi` it was shared by every client: two browsers asking at once — a
polling console and a `curl` is enough — advanced the same counter, and each
received half a list interleaved into JSON neither could parse. It is now a
`shared_ptr` captured by the lambda, so it lives and dies with the response.
Six simultaneous requests now return six complete, identical documents.

**A description could carry a control character straight into the JSON.** A tab
inside a comment survives the parser that captured it, and a raw control
character is not valid JSON — the whole document becomes unparseable and the
symptom is an EMPTY rules table, which lands nowhere near its cause. Folded to a
space, alongside the quote and backslash that were already escaped. Verified
with a file containing all three.

**A rule that failed to parse left its comment on the next one.** The pending
comment was cleared only on success, so a typo produced a working rule
described by someone else's sentence — worse than no description at all. It is
now consumed by any line that is neither blank nor a comment, whether it parsed
or not.

---

### Every rule column shrinks to its content, description takes the rest (08-10)

Six columns of short, fixed vocabulary — `INTEGREE`, `ctx ge 75`, `10s`, `600s`,
`SetEmotion Worried 4000` — were sharing the width evenly with the one column
made of sentences, which left the description three words per line. They now
shrink to their content and the description absorbs everything left.

Written as "all cells, then the last one back" rather than as a list of
`nth-child`, so a seventh column added later cannot silently opt out. And the
table has to declare `table-layout:auto`: the global `table{}` rule sets `fixed`
for the tuning tables, and under `fixed` a `width:1px` is obeyed literally —
every column collapsed to a pixel and the rows overlapped into a smear.

---

### A rule carries the comment written above it (08-10)

The format has no description field, and adding one would break every
`rules.txt` already written. But a human puts the explanation on the line above
anyway — the shipped template does it for all four of its rules — so **that line
is the description**. It costs the file nothing and the console gains a column.

Consecutive comment lines accumulate into one description, because a sentence
wrapped over two lines is one sentence: keeping only the last gave rules
described as *"mid-answer does not read as celebration"*, which reads like a
broken parser rather than a wrap. A bare `#` is the paragraph break, which is
what keeps the file's fifty-line format header from attaching itself to the
first rule below it. Over ninety-six characters it is cut **on a word** and
elided, and it is interned **last** — the arena is finite, and a rule that lost
its description still works where a rule that lost its field name does not.

**The section's help was rewritten to be written FROM.** It was a paragraph
summarising the format; it is now the format itself: the line shape as code,
then every column with what it is for, then the two things that actually catch
people — an unknown field reads **0**, so `ctx lt 20` is true when nothing
publishes `ctx` (which is what the `enable` gate exists for), and a line over
127 bytes is discarded whole and in silence.

Two defects of my own on the way, both caught by something rather than by luck.
The chunked `/api/rules` writer copied `snprintf`'s **return value** — the
length it *would* have written — out of a 224-byte buffer the description had
outgrown; the read ran off the stack and the server stopped answering at all.
Sized for the worst case and clamped to what is actually in the buffer, first
and always. And regenerating the mirror, a file was opened for writing before
being read, so the read returned nothing and nothing was written: the mirror
gate reported a 0-byte copy on the next run, and the copy is now regenerated
from the firmware literal rather than retyped beside it.

---

### The rules explain themselves, ship live, and can be looked at (08-10)

Three things were true of the rules engine at once: the file did not say what a
rule was, the shipped examples were all commented out, and nothing anywhere let
you see what had actually loaded.

**`rules.txt` now teaches its own format.** The template the firmware writes to
a card that has none carries the field list, the operators, the held-edge
semantics, and the two properties that make a rule safe to hand to a stranger —
only whitelisted commands, and the reflexes outrank it. It also states the
127-byte line limit, because `RuleStore::load` reads into `char line[128]` and
**discards** an over-long line whole: a comment that overflows vanishes from the
very file you are reading to learn the format.

**Four LIVE rules for the Claude context gauge**, not commented examples: a
warning at 75 %, alarm at 90 %, a head-shake at 97 %, and contentment under
20 %. All four are gated on `claude`, which only the statusline bridge
publishes, so they are inert until it is installed and cannot fire on a robot
that has never heard of Claude.

**The bridge now publishes what they watch.** `claude-statusline.ps1` and its
`.sh` twin derive `ctx` — the share of the context window in use — from the
session transcript, since the statusline JSON carries no such field and its
schema is undocumented. The denominator resolves `$CLAUDE_CTX_WINDOW`, then
`autoCompactWindow`, then 200 000, and is stated rather than assumed: on a
1 M-window model the default reads 100 % for ever, which is a variable to set
and not a bug to chase. It sets `claude` to 1 **only** when `ctx` could be
computed — a rule like `ctx lt 20` is TRUE on an absent field, so a bridge
announcing itself without the number would cheerfully fire the empty-context
rule.

**`GET /api/rules` says what is loaded**, which is not what the file contains: a
line that fails to parse is simply absent, with no error and no log, and the
only way to find out used to be watching the robot fail to react. Per rule it
reports the gate, the condition, the timings, the rendered action, whether it
came from the card, and whether its gate is open right now — answered by the
engine itself, because a console computing it from its own copy of the field
would eventually disagree about which rules are live.

**The console renders it as a table, in a section of its own.** Rules had been
the third block of the status-band panel, which is where they least belong: a
rule watches a field and posts a command, and the band is one of the things it
may end up changing, not its home. Closed gates are greyed and struck through
rather than hidden — "my rule does nothing" is answered far faster by seeing it
there, greyed, than by its absence.

And the two copies of the template — the firmware literal and
`sdcard/stackchan-companion/rules.txt.example` — are now held by `check-mirrors.py`.
The old copy had already drifted: it documented `touch_head`, `approval` and
`decision`, three fields nothing in this repository publishes.

---

### The rules block says what a rule is (08-10)

`↻ Reload SD rules` and the word `rules.txt` beside it told you how to reload
something the panel never explained. A short note now says what one is — *a
field, a comparison, an action*, with `batt lt 15 → SetEmotion Worried` as the
whole shape — where the fields come from (the robot itself, or anything pushing
one through `/api/field`), and the two facts that make them safe to hand to a
stranger: a rule can only post whitelisted commands, and the reflexes stay above
it.

It also points at where the file is edited, which the panel had never mentioned:
**the Files tab**, then reload here, no restart.

---

### The notification gets a block of its own (08-10)

It was sitting under **Mode**, with the text size and the scroll speed beside
it, and that said the wrong thing twice. None of the three depends on the mode:
a notification INTERRUPTS whatever the band was showing, whichever mode that is,
and it scrolls at its own size and its own speed. Read as a mode's settings,
they are what makes someone switch mode to change the size of a message.

The status-band panel is now three blocks, separated the way System's are:

- **Notification** — the field, the *Say* button, the two sliders on one row
  because they read as a pair, and a line saying what they actually govern
  (every notification, alert or `say`, whatever the band shows underneath — and
  an alert is drawn at size 2 minimum whatever the slider says, because it is
  meant to be read).
- **Mode** — the selector and, under it, only what the CURRENT mode can use.
- **Rules · SD card** — neither the notification nor a mode: the file that turns
  a *field* into an emotion or a dance, whatever the band is showing.

Going full width had one cost worth catching: the Gauges mode's three
percentage fields and its *Push* button stretched across the whole panel, and a
button as wide as its container reads as the container's own action rather than
that mode's. Capped at 280 px.

---

### Four tabs, and the icon pills join the telemetry row (08-10)

**Pilot and Band become one.** They had been split by SUBJECT — the robot's
body on one side, its status strip on the other — and that is not how the page
is used: driving the robot and choosing what it displays are the same sitting.
Four destinations now, and the one you land on holds everything you touch to
make something happen. A remembered `band` falls back to `pilot` rather than
opening an empty page, because `tab()` validates against the list before
touching anything.

**The icon pills move onto the telemetry row, pinned right.** They choose what
the robot's own status row shows — the same question the band answers about the
robot's state — and they had been the tail of the status-band panel, three
scrolls away from the chips they mirror. Pinned with `margin-left:auto`, which
is released below 760 px: on a narrow window the pills drop to their own line
and being pushed right there would read as a third column that is not present.

A stale string surfaced in the screenshot rather than in any test: the French
pomodoro note still sent the reader to "the Tuning tab" for three sliders
sitting immediately above it — a layout from two designs ago. Corrected, along
with the code comment that repeated it.

---

### The robot becomes the input device of the choreography editor (08-10)

Two sliders are a poor way to say "like this". **Released, the robot's head is
posable by hand and it reports the angles it has been put into** — and the
firmware already had the whole mechanism: `GET /api/servo/pos` reports a
measured pose, and reports it only while the servos are off, because the SCS0009
bus is write-only in operation and a read slipped between two `WritePos` leaves
the servos mute. Releasing them is not a convenience there; it is the
precondition of measuring them.

What was missing was one thing on each side.

**On the robot: `cors`, off by default.** The editor is a local `file://` page,
so every request it makes is cross-origin and the browser discards the answer
unless the robot allows it. `Access-Control-Allow-Origin: *` is a real widening
— while it is on, any page the browser happens to show can talk to the robot on
the local network, and with Basic Auth off that includes making it move — so it
is a switch you throw, not a default you inherit. Read once at boot, because the
header list is global to the server and add-only: there is no removing it
afterwards, so the decision belongs to the boot that read the config. Verified
on target: absent by default, present after `cors=1` and a restart.

**In the editor: a Capture panel.** IP, a switch that releases the servos and
polls the pose at 5 Hz, a live readout, ⤓ Capture into the keyframe, and a
*follow live* tick that writes every movement into the selected keyframe as you
pose it. Turning it off puts the servos back **the way they were**, not the way
the tool assumes they were — a robot whose owner runs with servos off would
otherwise have been handed a servo task it never had. Closing the tab restores
them too, through `sendBeacon`.

Three details decide whether it is usable. Raw servo degrees come back and the
editor speaks offsets: both conversions are a subtraction (yaw − 166,
pitch − 93) and both directions already agreed with the sliders. What is
captured is clamped to the editor's limits — the head poses a little past what a
choreography may ask for, and a CSV the robot silently trims is the exact
failure this tool exists to prevent. And a blocked cross-origin fetch surfaces
as a bare `TypeError` with no status and no body: reported raw it reads as "the
robot is off", and you power-cycle a robot that was answering fine. It is named
for what it is, with the switch to throw and the restart it needs.

---

### The telemetry becomes a jumbo band, and Options becomes three cards (08-10)

Three notes on the new console, and each named something that was still wrong.

**The live telemetry runs edge to edge**, directly under the header, as its
continuation rather than as the first panel of whatever tab you are on — which
is exactly what it is not: it belongs to none of them and is true for all. That
meant neutralising the generic `details` rules, which paint a glass card on
every summary and every direct child, and which had put a 1020 px rounded box
back inside a full-width background — the worst of both, a card that had lost
its border. Content included: the graph IS the band now, minus the 20 px that
keep text off the bezel.

**The two switches that are ABOUT telemetry moved into it**: *Debug info
(emotion·IP)*, which publishes it on the robot's own band, and *Serial
telemetry*, which publishes it on the wire. Neither was a property of the status
band or of the options they sat in.

**The Options tab is three cards, laid out like Pilot's row.** The auto-fit
grid packed its groups by available width, so a four-group set wrapped one under
another and the column headings stopped lining up — which is the one thing a
column heading is for. With Diagnostics gone to the telemetry band there are
exactly three, and `.cgrid` is the row the Emotions/Dances/Head cards already
use: one layout fewer to keep in agreement with itself. Its own "Options"
heading is gone too; the tab is already called that, and a panel inside it
repeating the word said nothing twice.

A tab also stopped being a card: `.tab` is a `section` because that is the
honest element for it, and `section{}` was drawing a second frame around panels
that already had their own.

---

### The console gets a shape: five tabs over one live block (08-10)

Same palette, same glass, same aurora — not a colour moved. What changed is
that the page stopped being one scroll.

**Five tabs.** Eight panels stacked head to tail meant that reaching the tuning
tables involved scrolling past the camera and the file manager every single
time, and that the System section — the longest of them — was the furthest
away. Pilot (emotions, dances, head), Band, Options, Files, System. The tab
lives in the URL hash, so `/#sys` is a link and a reload lands where you left;
it falls back to `localStorage`, because the hash is lost the moment anything
navigates. Neither alone is enough: the hash is shareable but fragile, the
storage durable but private to one browser.

**Telemetry left the sticky header.** The chart and the status chips made about
180 px of permanent chrome — a fifth of a laptop viewport, pinned while you drag
a slider. They now sit in the page, above the tabs and shared by all of them,
in a block that collapses and remembers. The header is brand plus tabs, and it
finally lines up with the content: it used to pad 20 px from the viewport while
`main` centred at 960, so on a wide screen the logo sat far left of everything
it introduced. One `--wrap` variable now, both sides.

**Options are grouped by what they act on** — Screen, LEDs, Sound,
Diagnostics — instead of nine switches in one column you read to the end every
time. "Is the microphone on" is now answered by looking at one place. Fine
tuning joins them, closed: it is the same question asked of seventy numbers
instead of ten switches, not a chapter of its own.

**ISO-FUNCTIONAL.** Not one control was added, removed or rewired — every id,
every handler, every `data-i18n` survives, and `check-console.py` still reaches
all 71 tuning keys. Verified by rendering the five tabs headless and reading
them: that pass caught two real defects, a band panel left at two thirds width
by the grid its neighbour had vacated, and a `Clock · NTP` header whose badge
made it read "NTP NTP". The `.rowflex` rules that grid used are gone rather than
left behind.

---

### The scripts get Linux twins, and the two facts they share are held (08-10)

Seven `.sh` beside their `.ps1`, taking the same arguments: `check-all.sh`,
`gates/test-native.sh`, and in `dev/` `find-port.sh`, `test-mag.sh`,
`endurance-log.sh`, `statusbar-push.sh`, `claude-statusline.sh`. The gates
themselves are Python and were always portable; what needed porting is the
runner around them.

**The twins that matter are registered.** `check-all.sh` restates the two
thresholds that make the native-test gate mean anything (26 suites, 306 cases)
and `find-port.sh` restates each board's USB identity. Both are hand copies, so
both drift the same way — a suite added on Windows and not raised on Linux gives
a Linux run that passes with *fewer tests than it should*, and a board added to
one table stays unknown to the other while the script keeps answering "ABSENT"
as though that were true. Six new entries in `check-mirrors.py` hold them, which
is what this project does with a copy it decided to keep.

Three things the port could not copy:

- **`find-port.sh` reads sysfs, not WMI.** `/dev/ttyACM0` is no more an identity
  than `COM6` is, so the board is resolved by walking up from the tty to the USB
  device that owns it and comparing VID:PID. No `udevadm`, no Python — one fewer
  thing the machine has to have, and it works in a container. The Windows
  "driver not installed" case becomes the Linux one: the node exists and you
  cannot open it, so it names the `dialout` group instead of letting a
  permission error surface from inside esptool.
- **No hand-rolled percent-encoding.** `statusbar-push.sh` builds its query with
  curl's own `--data-urlencode` (`--get` puts it in the URL, `-X POST` keeps the
  method), because a label with a space, an accent or an `&` is exactly what a
  home-made escaper gets wrong, and the failure is a truncated field on the
  robot with nothing in any log. Verified against the robot: a `say` with an
  ampersand, three gauges, a mode change and back.
- **`.gitattributes` now pins `*.sh` to LF.** `core.autocrlf` is true here, so
  without it a Linux clone gets `#!/usr/bin/env bash\r` and the kernel refuses
  with *"bad interpreter: no such file or directory"* — a message that names the
  interpreter, not the carriage return, so it reads as a missing bash. Files
  that exist so a Linux user has something that runs would have shipped
  unrunnable.

---

### `scripts/` and `tools/` get a shape, and it states a rule (08-10)

Fifteen files flat in `scripts/`, six in `tools/`, with gates, hand-run session
chores and a build step all in the same heap. Sorted by **who runs it**, which
is the question you actually have when you open the directory:

```
scripts/  check-all.ps1   the one entry point
          gates/          what check-all runs, and NOTHING else
          dev/            what you run by hand, against a board
          build/          what PlatformIO runs on its own (a pre: hook)
tools/    choregraphies/  an application: the dance editor
          generators/     write a file the firmware or the SD then consumes
          probes/         query an external service, to parse what it really
                          answers rather than what its documentation says
```

The `gates/` rule is the point: a file in there is a file the gate runs. Two
residents are named as exceptions in `scripts/README.md` rather than left to
look like oversights — `check-comments-only.py` (a sweep tool that exits 1 on
any real commit; it was in the gate list once and made the gate fail on its own
first run) and `test-native.ps1` (which runs the suites alone, where `check-all`
runs `pio test` itself so it can assert the suite and case COUNTS).

**The trap was the root computation.** Every checker found the repo with
`dirname(dirname(__file__))`, an assumption about its own depth; one level
deeper, all six resolved to `scripts/` and would have reported a repository
full of missing files. They now walk one more level AND assert that
`platformio.ini` is there, because that failure does not crash — it makes every
path miss, and the run reads like a broken repository instead of a moved script.

Sixty-two files carried a reference: `platformio.ini`'s build hook, the gate
runner, both `CLAUDE.md`, both `README`, ten documents, and the "how to run me"
header of every native test. One of them was **already wrong** —
`docs/ROADMAP` had been pointing at `scripts/extract-presets.py` since the dance
editor was written, and the file has always lived in `tools/choregraphies/`.

Both directories gain a README. `tools/`'s table listed **two of its six**
tools, which is how three of them went unmentioned for months: a tool nobody can
find is a tool that gets written a second time.

---

### Forcing the clock to resync, and a plural where there were several files (08-10)

The wall clock could only ever be set once, at the end of the wake-up sequence,
and only if the robot already had an IP by then. A network that arrived later —
credentials typed into the console, an access point that came back — left the
companion on whatever the RTC had drifted to, with no way to fix it short of a
reboot.

**`POST /api/clock/sync`** restarts the SNTP client and, crucially, **re-arms
the RTC write**. Clearing the "a packet really landed" flag is the whole point:
left set, the next loop pass would have rewritten the chip from the old drifted
`time()` and called it a sync — the exact self-deception the SNTP callback was
introduced to end. It is deferred to `loop()` like every other write (A2.6:
`configTime` tears the client down and back up, and the RTC is I2C on the shared
11/12 bus), and it answers **409 in AP mode** rather than pretending: the robot
IS the network there, so there is no route to a time server and a pending state
would be a lie that never resolves.

**`GET /api/clock`** publishes two facts the console had been conflating.
`plausible` means the epoch looks like a real date — which proves nothing, since
`M5.begin()` restores the system clock from the RTC at every boot. `ntp` means a
packet really landed. A clock that looks right and has never seen NTP is exactly
what the old sync guard believed for weeks.

In the console, a **Clock · NTP** group opens the System section with the button
and both badges; the time is shown with *"from the RTC, unverified"* next to it
until NTP confirms. Registered before `/api/clock` so the prefix cannot swallow
it (A2.19).

And in the SD file section, the group holding `config.yaml`, `rules.txt` and the
guest configs is now **Configurations** — there was never only one.

---

### The peak marker is coloured by its own height (08-10)

One fixed colour made every transient look alike: a clap that reached full scale
and a door closing three blocks up were the same mark in two places, and the eye
had to read the *position* to learn which. Colour is the faster channel, so it
now carries the same fact the height does — the ramp, dimmed, near the axis;
white at the edge of the zone — and the two agree by construction.

The whitening is **quadratic**. Linear, everything above the middle read as
roughly white and the top of the scale stopped being special; squared, the
marker stays on the palette for most of its travel and only peaks that really
reach the edge go white. That also answers the earlier complaint from the other
direction: pure white everywhere out-shouted the stack it annotates, and a
single dim colour told the eye nothing the position was not already telling it.
Now the loud ones shout and the rest do not.

`sndGrad` splits into an RGB888 core and a 565 wrapper, so a caller that needs
to keep mixing — the marker walks on towards white — is not made to re-derive
the ramp from scratch.

---

### Matrix stops swallowing quiet sounds (08-10)

*"columns feels more sensitive."* It does, and measuring it separated two
causes that looked like one.

A scratch harness fed the same block to the three skins and reported how tall
each draws. **The peaks agree exactly** — a 625 Hz tone reaches 13 px in `wave`
and 13 px in `columns` — so there is no scaling bug and no drifting gain. What
differs is the mean: 8.2 px of ink for the curve against 12.5 for the bars,
because `wave` plots the instantaneous waveform while the bar skins plot its
ENVELOPE. Above a few hundred hertz the bars sit near the peak while the curve
spends most of its time below it. That is what an envelope is; making the curve
match would mean drawing an envelope and calling it a scope.

The second cause was a real defect. Matrix computed its block count as `h / 4`,
so anything under a sixth of full scale drew **nothing at all** while `columns`
— the very same numbers, the other skin — drew it. Two skins of one picture
disagreeing about whether there is any sound, which is the one thing they may
never disagree about. A non-zero level now always lights at least one block; the
quantisation still governs the LEVEL, it no longer governs PRESENCE. The ceiling
is untouched, so the peak marker still lands one block beyond the tallest stack
block and the three `static_assert`s still hold.

---

### Ten rows of air over the icons, and the prose catches up (08-10)

The band's zone loses six rows from the bottom: **163..221 → 163..215**, the
axis moves to 189, the half-height 27 → 23. The margins are now deliberately
unequal — three rows under the eyes, ten over the icon row — because a
full-scale bar stopping two rows under a line of small glyphs read as touching
it, two unrelated things fused into one crowded strip. Above, the eye zone is
mostly black at the bottom, so the trace has air there whether the constant
says so or not. 23 and not 26 for the same 4m+3 reason as before, and the three
`static_assert`s caught the arithmetic rather than the screen doing it.

**And the prose caught up with the code.** The documentation still described
the visualiser as a scope PLUS a spectrum laid out in two halves — which
stopped being true the day the styles became skins of one waveform, but prose
does not fail a build. Corrected in `STATUSBAR.md` §6 (the opening claim, the
no-forced-symmetry paragraph, the analysis table, which now names the envelope
and both peak holds and says which one is drawn), `CONFIG.md` (the gain scales
the trace, the envelope and the bands), `CLAUDE.md`, and the header comments of
`Renderer.h`, `SoundFrame.h` and `SoundViz.h` — the last still opened by saying
it produced "the two things the status band draws".

The measured-cost row was replaced rather than refreshed: the old "4.3 to
8.8 ms" was read from `frameAvgUs`, which is the WHOLE frame, eyes included. Re-
measured across the three styles it lands between 4 and 10 ms, and moves more
with whatever the eyes are doing than with the choice of style. Saying so is
worth more than a number that looks like a measurement of the band.

**`flight-radar` stops being "the demo bin".** It was written up that way when
it was the only guest; there are four now, plus two standalone Fire ports.
`README` drops the label, the quick-start builds `space` and says the same two
lines work for any of them, and `docs/guests/SPACE.md` — which existed but was
listed nowhere — joins the index in `README` and `CLAUDE.md`.

---

### The sound band takes the height it has (08-10)

Two more notes on the styles, and the second was a question worth asking: no,
the visualiser was not using the space it had. Its zone was rows 166..205 —
forty rows inherited from the TEXT layout, which is where a scrolling line of
8-pixel type wants to sit. But a visualiser is not type. Between the eye zone's
last row and the icon row's own wipe there are sixty-three rows, and it was
using forty; the trace was a thin ribbon in a wide black strip.

The zone is now rows **163..221** (three of air under the eyes, two above the
icons), the axis moves to 192, and the half-height goes **19 → 27** for all
three styles at once — three skins of one waveform have to agree about how tall
full scale is, so the scope's separate 18 is gone with them. Matrix gains two
block rows per half.

27 and not 29, which the zone would allow: matrix's outermost mark is the peak
block at `k = HALF/4`, whose top row is `CY − 4·(HALF/4) − 3`, and that row is
inside the zone only when the half-height is 4m+3. At 29 the peak block of a
full-scale column would have sat one row outside and `mSeg` would have trimmed
it in silence — the same quiet clipping the axis was once moved to cure, back
by another door. Three `static_assert`s now check it at build time, plus two on
the segment table, because the failure mode here is never a crash: it is paint
that stops appearing at exactly the moment the display is looked at.

Two consequences handled. Text over the band (an alert, a `say`) wipes the
visualiser's own zone on the transition, since the per-frame text wipe no
longer covers it and bars would survive above and below the line. And matrix's
peak marker steps back from pure white to the console's text colour at 70 %:
at full brightness it out-shouted the stack it is there to annotate.

---

### The sound band: three looks, and a peak marker that is legitimate (08-10)

Three requests on the styles, and the first one reopened a decision.

**Matrix keeps a white peak marker, and its blocks are squares.** The marker
had been dropped the day before for a real reason — a peak-hold computed on
the display column is smoothing born in the renderer, which A2.15 forbids. The
answer is not to give it up but to put it where memories belong: the envelope
AND its peak-hold now live in `SoundViz` and are published in `SoundFrame`
(`env`, `envPeak`), and the renderer only draws them. It rises instantly, falls
at a fixed rate — about three seconds from full scale — and a return to *mic
idle* clears it, so a transient from a minute ago is never the first thing the
band says. The blocks go from 6 × 3 to **3 × 3**: they were called square and
were not. Drawn in white rather than a pale tint of the ramp, so the marker
reads as a different kind of mark: not a level, but where the level has been.

**Wave's ink follows the agitation.** A calm trace is nearly black — present,
barely — and whitens along the palette as the curve moves. A line of constant
brightness makes silence and speech look equally eventful; now the band is dark
when the room is, and the eye is only called when something happened.

**Columns is monochrome.** The ramp across the width made a row of thin strokes
read as a gradient chart — a second thing said by the colour while the height
was already saying the only thing that style has to say.

`sndGrad`'s `hot` becomes an amount rather than a flag, since wave whitens
progressively and a boolean can only snap; `true` still converts to 1.0, so
every earlier call site keeps its meaning. Five native cases pin the new
producer-side contract: the envelope follows the trace, takes the louder mic,
the peak holds then falls, never sits below the envelope, and idle forgets it.

---

### The sound band: full width, and three skins of one waveform (08-09)

Two user asks, and the second corrected a design mistake. First, all three
styles now span the panel **edge to edge** — wave grows to 160 columns × 2 px
= 320, the bar styles move to a 10 px pitch × 32 = 320, and the segment table
follows (560 → 704, the drift its own comment predicted).

Then the real one: **columns and matrix are now skins of wave**, not
spectrums. The reference images never showed analysers — they showed the same
waveform silhouette dressed as thin strokes (viz2) and as pixel blocks (viz3).
All three styles draw the same triggered 256-sample window, time across the
width: wave as continuous curves, columns as the **envelope** of each five
samples (the louder of the two mics — a silhouette has one height), matrix as
the same envelope quantised into blocks. Switching styles changes the
clothing, never the statement.

This also answers the symmetry question: the mirrored left/right look came
from the spectrum layout (one channel per half, bass outward) over two
microphones that hear nearly the same room. A waveform skin has no halves.

No peak marker anywhere — a peak-hold computed on the display column would be
smoothing born in the renderer (A2.15). The band spectrum stays computed and
published in `SoundFrame`: nothing draws it today; it is the assistant mouth's
food (ROADMAP §8). Measured on target: columns 9.9 ms, matrix 8.0 ms of 33.

---

### The max review lands: fifteen fixes, four of them invisible on screen (08-09)

The interrupted review finally completed: 24 verified findings, 15 above the
severity bar, all fixed. The invisible ones are the ones worth reading:

- **`space`'s auto-brightness turned itself off on every boot.** The
  "hand-set brightness disables auto" coupling also fired while the yaml was
  being REPLAYED, where `bright` still holds the compiled default — so the
  card's own `bright:` line killed the `auto_bright: 1` loaded two lines
  earlier. A replayed file is not a hand.
- **The NOTAM token reopened the two-task race its own comment declared
  fixed** — the 401 path still saved from netTask, and pre-NTP it persisted a
  deadline the loader discards.
- **An inherited timezone sealed itself** into the bin's yaml on the first
  unrelated save, making "the bin's key wins" undefeatable.
- **The scope trace kept the DC the spectrum removes**: a mic riding an offset
  never crossed zero, so the trigger never fired — the exact artifact it
  exists to prevent.

Also: `band_mouth` carried the key but not the renumbered VALUES (a card that
chose Wave got Columns); two "bounded" readers had no overlong-line drain
(phantom airport cached forever, phantom HA entity); the HA roster rewrote
identical bytes ~2900 times a day; the band flush paid ~512 unbatched SPI
transactions per loud frame; WAVE traces punched holes in each other at
crossings; the mic register lied on legacy cards; the analyser ran ~31 FFTs/s
for an invisible band; a one-step brightness nudge never applied; and the FR
console labelled an HOURS slider "(min)".

The steering documents joined the as-it-is policy the same day — ROADMAP
1256 → 1079 lines, closed backlog rows gone, incident dates out of the 24
rules, and real contradictions fixed against the source (`presets/` claimed
"GENERATED — do not edit", the exact inverse of rule A2.11).

---

### The MOON column answers the question it is asked (08-09)

The right-hand column used two thirds of its 124 × 162 frame; it now fills it,
from identity to fact: the phase name, the lit percentage at **triple** size, a
cyan→indigo identity bar, then **the next milestone** — *full moon ~ 3 d*, *new
moon ~ 12 d*, *tonight* under a day. "When is the full moon" is the question a
moon page is asked, and the column never answered it: the phase strip shows the
order of what comes next, not how long it takes. `moonDaysToElong` is pure, at
the mean synodic rate (exact would need a root search for a figure read in whole
days), natively tested.

The geometry work surfaced three defects: the longest phase name ("Gibbeuse
decroissante", 126 px) had **always overflowed the panel by 2 px** — the column
moves to x=190, where the whole vocabulary fits; GCC 8.4 **unrolls a
two-iteration constant-bound loop** back into the two similar draw calls A2.22
forbids, and `#pragma GCC unroll 1` does not stop it (verified in the
disassembly — an empty `asm volatile` on the bound does); and the first
"tonight" wording fit **neither its buffer nor the panel** — 24 characters in a
`buf[24]` and 144 px on a 130 px column. "ce soir": 21 glyphs, the exact
capacity.

Follow-up review: five `15000000` literals had survived the SdPins
factorisation (two in `hal/Board.h` — the companion, the one target whose
silent drift costs most — three in the radar); all now read `SCE_SD_HZ`, no
literal left outside `SdPins.h`. And the full-moon boundary was strict: at
exactly 180° the countdown announced the next *new* moon, a fortnight out, at
the very instant of the event.

---

### The last two duplications, and a doc that contradicted itself (08-08)

**The SD wiring was written four times** — three guest bins and `hal/Board.h` —
and the copies agreed on the numbers while disagreeing on the discipline, which
is the more dangerous half. `flight-radar` guarded each pin separately and wrote
down why; `space` guarded all four behind the first, which is **exactly the trap
that comment describes**: a board profile overriding only `SCE_SD_CS` leaves
`SCE_SD_SCK` undefined, the block puts CS silently back to 4, the redefinition
warning scrolls past, the mount fails, and there are no WiFi credentials and no
STA. `ha-remote` had no macros at all — `SPI.begin(36, 35, 37, 4)` as literals,
twice, unreachable by any board profile. One header now, four guards, one per
pin; both Fire profiles verified to still override it.

**The guest contract contradicted itself about WiFi.** The integration section
still described the pre-NVS chain — card, then build arguments, then AP — while
a later section described the new one. The precedence is now a table where a
reader meets it, with NVS at rank 1 and the reason it outranks the card.

**And the settings-page verdict is now written down.** The three bins answer
differently when an SD write fails: `space` returns the real synchronous result,
so a full card genuinely shows an error; the other two return success and defer
the write, because theirs must release a lock and must not block an HTTP handler.
Neither is wrong, both are deliberate, and neither was documented — so a fourth
bin would have picked one by accident. The contract now says what `false`
promises, and that a deferred retry needs a backoff.

---

### Review: one wrong answer out of three, and eight false statements (08-08)

**`off` meant true.** Three bins each had their own idea of what a configuration
value means. Two compared exactly against `"1" | "on" | "true"`; `space` tested
the FIRST CHARACTER, so `off` read as **true** — it starts like `on`. Nothing
sent `off` from the form, so it never fired; a hand-edited yaml would have. One
definition now, in `firmware/common/CfgBool.h`, **pure and natively tested**
(`test_cfgbool`, 6 cases, the first of which is `off`).

**A fix that reached one twin and not the other.** `ha-remote` grew a 5 s
backoff on a failed SD write and wrote down why — "without it, loop() hammered
the SPI2 bus shared with the display". `flight-radar` had the same shape,
unthrottled: on a full or write-protected card it reopened the file every ten
milliseconds, for ever, on the bus the renderer needs.

**The matrix gradient stopped short.** Its span was the literal `8` — the block
count of the old full-height bar. When the bars became half-height the top block
stalled at 0.69 lightness instead of 0.94, dull beside a peak marker still drawn
at full brightness, at exactly the moment the display is looked at. The span is
now derived from the constant that determines it.

**And eight documented statements were false**, the worst of them actively
dangerous: `CONFIG.md` printed `soundtrack_sign: 1` as "validated on K151" while
the code holds `-1` and warns, twice, that the inversion has already been made
twice — the doc was telling a reader to make it a third time. Also corrected:
`head_follow` documented as off when it is on by default, the guest settings cap
(20 → 24), the no-card line budget (6 → 5), two live tuning keys never
documented at all (`led_swap`, `cam_colorbar`), the launches section still
describing the text queue the horizon replaced, and the rocket-family table
missing the `not announced` profile.

---

### The space bin's palette had never been checked (08-08)

`scripts/gates/check-contrast.py` has gated the radar's four themes since it was
written, and it read exactly one file. The `space` bin carries a second
`THEMES[]` with a different field list, so it was never parsed — and `hint`,
which that bin uses for **information** (the "waiting for the clock" splash at
double size on every boot, the PASSES radio chip, every sky-dome caption, the
TBC/TBD launch chip), measured **2.78:1 on Deep and 1.66:1 on Night** against a
4.5:1 threshold. A gate that covers one of two palettes reports CONFORME about
half the product.

The gate now parses both, with a **per-field background** — the coastlines are
drawn over the map's own day and night fills, and judging them against black
would have flattered them. Seven failures came out; the palette was corrected
rather than the thresholds relaxed, each colour raised to the minimum that
passes while keeping its hue.

Also from the same audit:

- **The ISS caption could eat the longitude.** A 24-character satellite name
  plus `"  -90.00  -180.000"` is 42 characters into a 39-character buffer:
  `snprintf` cut the *coordinate*, silently, leaving a truncated longitude
  reading as a different place. The name is fitted first, so the loss lands
  where it is visible and costs least.
- **The launch status chip could overlap the vehicle name.** The comment
  asserted the widest status was "In Flight" and could not collide; the field
  holds eleven characters and Launch Library sends "Partial Failure". A fact
  about a feed belongs in a clamp, not in a comment.
- **The sky dome's "E" and its daylight caption shared their pixels**, with not
  one column between them, every daylit hour — which is most of the hours that
  screen is opened.
- **Elevation now carries its unit** wherever it sits beside an azimuth that
  already did: the same panel showed two degree values four pixels apart, one
  labelled and one not.
- **The launches strip says its gesture.** PASSES tells you its rows are
  tappable; this one did not, and its gesture is the less guessable of the two.

---

### The no-card screen belongs to SceGuest (08-08)

Two bins had written it by hand, the second by copying the first — rule 17's
exact failure mode, and the twins had already begun to differ. It now lives in
`SceGuest`, where every guest bin gets it, including the ones nobody has written
yet.

- **SceGuest owns the mechanism**: the layout, the retry loop, the input
  routing, the single `drawString` call site (A2.22, now pinned ONCE instead of
  per bin), and every row that is true of any bin.
- **The bin owns its consequences**, and they are **optional** —
  `guest.noSdNotice(remount)` alone gives a complete screen. A notice that only
  works once you have written five strings is a notice new bins will not have.
- **The bin owns the remount**: the header must never learn anybody's SD pins.
- **The input asks the panel, not a build flag** (`M5.Touch.isEnabled()`): touch
  halves on a CoreS3, A/B/C on a Fire, from the same binary.

`ha-remote` gained the screen it never had, which is what factoring is for.
Without a card it has no Home Assistant at all — the host and the long-lived
token live in the yaml with no compiled default — so it used to come up, discover
nothing, and show an empty home screen indistinguishable from a working one.

The generic rows also state what a card-less board **keeps**: the network, which
now lives in NVS. Without that line the screen reads as "you are stuck on the
access point for ever".

---

### `space` says when there is no card (08-08)

The card was optional and its absence silent — the worst combination. Without
it the observer falls back to the **compiled position** (Paris), and a sky drawn
for the wrong place looks exactly like a sky drawn for the right one: the passes
are wrong by hours, and south of the equator the Moon is lit on the wrong side.

The boot screen now says so, and states the position it actually fell back to —
**formatted from the live config**, never written out as text, because a
hard-coded "Paris" would keep claiming Paris the day the compiled default
changes. Retry after inserting a card, or continue without one; a card found on
the retry makes the configuration be read again, since everything read before it
ran against no card.

Same idiom as the radar's notice, including the single `drawString` call site
(A2.22) — eleven rows differing only by datum and colour. That one is pinned in
the gate: a row thinned out of the binary would take the warning with it.

---

### A WiFi passphrase may hold any printable character (08-08)

It could not, and nothing said so. `"` and `\` were **silently deleted** in
three places — the `/api/wifi` filter, the API-credential cleaner and the YAML
writer — on the stated grounds that quotes had to be kept out of the JSON and
the card. Those two characters are legal in WPA-PSK, which is any 8 to 63
**printable** ASCII characters.

The failure mode was the nastiest kind. The value accepted over the API was the
one used for `WiFi.begin()`, so **the robot joined the network** and reported
success. The value written to the card was the amputated one, so after the next
reboot it could not join at all — a fault that appears hours later, on a reboot
nobody connects to the setting, with no message anywhere.

- `"` and `\` are now **escaped** in the card's quoted scalars, and the shared
  decoder reads them back (`\"` and `\\`). Single quotes take no escapes,
  which is YAML's own rule.
- The credential filter now takes a `quotable` flag. WiFi credentials use it;
  **API credentials still do not**, and that difference is real rather than an
  oversight: the API user name is emitted in `/api/status` with a raw `%s`, so
  an unescaped quote there breaks the whole JSON document and empties the
  console.
- Six native cases pin the round trip, including a backslash immediately before
  the closing quote — the one that makes a naive parser swallow the rest of the
  line. They fail against the previous code.

Everything else already worked and stays covered: `#`, `:`, spaces, `&`, `%`,
`+`, `<`, `>`, `'` and accented UTF-8.

---

### `space` runs on a Fire too (08-08)

The same instrument as a standalone application on an M5Stack Fire: no
StackChan, no companion, no K151. Not a fork — the same source, with the
hardware differences declared as capability flags (`SCE_INPUT_BUTTONS`,
`SCE_COMPANION`, `SCE_SD_*`), each named after what the board **has**.

- **The button mapping had a hole, and it was the interesting part.** Short C
  meant *Select* on the two list views, which left those views with **no
  forward step**: the cursor could only be walked backwards, on precisely the
  screens built around a cursor. The mapping is now uniform and no longer
  depends on the view — A/B/C short are previous item / next view / next item,
  long are settings / refresh / open detail. A control whose meaning changes
  with the screen is a control you have to think about, and leaving a modal
  needs no button of its own because B already closes it.
- **No `SCE_HAS_LTR553`, unlike the radar**, and that is a decision: the light
  sensor is probed at boot and the result published in `/config`. The probe
  already covers a board with no sensor — and also one whose sensor has died,
  which a build flag never would.
- The exit gesture is switched off where there is no companion: the swipe would
  open a confirmation whose "yes" leads nowhere.
- **The A2.22 table for `space-fire` is derived from `space`**, not copied, so a
  point added to one covers the other. The two radar tables were hand-kept and
  had already drifted. One exclusion is named rather than filtered silently: on
  the ESP32 backend GCC inlines `drawSkyNames` away, so no symbol remains to
  count.

The gate builds six firmwares now, and checks A2.22 on both backends — 69
pinned call sites.

---

### The status band gets a real analyser, and the screen its own brightness (08-08)

- **Sound mode is measured, not generated.** The band used to draw a texture and
  say so honestly in a comment; it now draws an **oscilloscope trace** and a
  **band spectrum** computed from a real transform. `engine/Fft.h` is pure and
  natively tested — a sine lands in its bin and nowhere else, DC disappears
  everywhere, one channel stays silent while the other saturates. Both
  microphones come out of **one** 512-point transform: a real signal has a
  conjugate-symmetric spectrum, so left goes in the real part and right in the
  imaginary, and they separate exactly afterwards. The scope is **triggered** on
  a rising zero crossing, without which a steady tone starts at a different
  phase every block and the picture slides sideways for ever. Sixteen
  logarithmic bands, each worth the peak of its bins, on a decibel scale with a
  fast attack and a slow release. Measured on target: **4.3 to 8.8 ms** of the
  33 ms frame — cheaper than the texture it replaced.
- **The mean is removed before the window, not after.** A window has a spectrum
  of its own, so a constant multiplied by a Hann window lands in bins 1 and 2 as
  well as bin 0: zeroing bin 0 leaves two phantom bars standing under
  everything. The native suite caught the ordering before any hardware run.
- **`screen_bright` is the panel; `eye_color_dim` is the ink.** The only
  brightness control was the eye palette, which left the status band, the
  launcher and every guest bin at full blast. The two sources — sensor and
  manual — now compute **one** target in one place, and the manual value is
  applied the moment it changes rather than waiting for the sensor's two-second
  step. Moving the slider turns automatic brightness off, since the sensor would
  otherwise overwrite the value within two seconds. Floor of 10: the one control
  that could hide itself.
- **The spectrum had its bands the wrong way round.** Bass sat at the centre gap
  and treble at the edges, the opposite of the design and of the code's own
  comment. Bass dominates almost any signal, so the display grew a permanent
  central lump that said nothing about the sound.
- **The peak marker punched a hole through its own bar.** On a transient the bar
  and the peak rise together, so the row the marker used to occupy is now inside
  the bar — and erasing it to black carved a notch across, one frame per clap.
- **"mic idle" could never appear.** The producer published only on a live→idle
  transition; at boot nothing has ever been live, and the microphone is off by
  default. The band stayed blank instead of saying why.

### A guest bin can be told about a network (08-08)

- **The settings page always carries a Network block**, whether or not the bin
  declares a setting of its own — that is precisely the bin most likely to be
  stranded. Credentials go to **NVS**, not to the card: NVS survives a reflash of
  the application, an SD card does not; a card is written once and cloned to ten
  robots, NVS is not.
- **What was typed on the device outranks the card.** The other way round, the
  field silently does nothing on any robot whose `config.yaml` still names the
  old network — which is the robot somebody is standing in front of. *Forget it*
  hands control back to the card.
- **The access point serves a captive portal.** Every name resolves to the bin,
  so joining the network opens the page by itself instead of requiring an
  address nothing has displayed. `isAp()`/`apSsid()` let a bin name the network
  to join on screen; the radar's info screen does.
- The network is saved **before and outside** the application's lock: a user who
  has just entered a network on a stranded robot must not lose it because an
  unrelated write to a missing card returned false.

### The launches screen becomes graphical (08-08)

- **A horizon replaces the queue of five text rows.** A logarithmic time axis
  (NOW / 1d / 1w / 1M) carries the date by position, a drawn launcher carries
  what is flying, and a pip carries whether it is confirmed. Logarithmic because
  on a linear axis four markers out of five fall inside the first centimetre.
- The status chip is **filled** when the date is held and **outlined** when it is
  TBD/TBC: the shape carries the distinction, so the dim grey can stay dim
  without cheating on contrast.
- The tap picks the **nearest** marker rather than a hit box — a 10 px
  silhouette is not a finger target.

### The documentation describes the system as it is (08-08)

The project is not published: the narrative of its evolutions taught nothing to
a reader who came to use it, and it aged badly. Fourteen files rewritten in both
languages, every "it used to / now it does" turned into a rule or into the
consequence of breaking it. That history lives here, which is what a changelog
is for.

Stale facts corrected against the source rather than guessed: yaw range,
`cfg_version`, the dance yaw clamp, the timer's `MM:SS`, and a convention page
that demanded French comments beside headers written in English throughout.
`docs/guests/SPACE.md` and `FLIGHT-RADAR.md` now document their mechanisms and
calculations in full, with mermaid where a diagram earns its place.

---

### Caches: what does not change is not asked twice (08-04)

Four lookups were being re-earned on a schedule that had nothing to do with how
often their answers change.

- **flight-radar — aerodromes.** Resolving a route is one request plus one per
  waypoint, up to six TLS sessions for a single tap; the two-hour route ring
  spared the repeat within a session, and every reboot paid the whole price
  again. `/stackchan-companion/radar-airports.csv` now holds position, city, IATA,
  country, **and the tower/ATIS frequencies and field elevation** that used to be
  a second endpoint fetched once per boot — one entry per aerodrome, not one
  cache per question. No expiry: an aerodrome does not move. Negative answers are
  remembered too, but only in RAM and only for codes the database actually
  ANSWERED about; an outage must not be cached as fact.
- **flight-radar — the METAR poll is driven by the observation.** A fixed
  ten-minute timer asked six times an hour for something a station publishes
  twice, and still showed a report up to ten minutes after it was issued. The
  cycle is now LEARNED from consecutive `obsTime` values (clamped 10–60 min) and
  the next poll falls due at `obsTime + cycle + 150 s`, clamped to 5–15 minutes.
- **flight-radar — a NOTAM token minted before NTP is kept.** Its deadline could
  not be computed, so it was never written to the card — the worst case to drop,
  since a bin minting before NTP is a bin that just rebooted, and the account
  allows twenty tokens a week. The remaining life is now measured on the
  monotonic clock and stamped the moment NTP lands.
- **ha-remote — the entity roster survives the reboot.** Discovery costs fifteen
  seconds during which the home screen counts zero of everything. Identifiers,
  names, categories and capabilities are written to
  `/stackchan-companion/ha-entities.tsv` and read back at boot, so the table is up in
  a second — with the **states deliberately blank**, because a remembered `on`
  presented as current is the one lie a remote must not tell.

## Architecture

- **FreeRTOS**: Brain at 100 Hz (core 1, prio 4, sole writer of the face
  state), Renderer at 30 Hz (core 1, prio 3, sole owner of `M5.Display`),
  ServoMotion at 50 Hz (core 0, prio 3), **camera task** (core 0, prio 1 —
  capture + software JPEG encoding outside the loop, preempted by the servos
  and by the network), Arduino loop on core 0 (10 ms).
  Lock-free `TripleBuffer<FaceState>` + `CommandQueue` (xQueue) + `Tuning`
  registry (~58 hot parameters, persisted to SD, `GET/POST /api/tuning` — SD
  save only on a **real change** of value).
  `Renderer::pause()` is **refcounted under a spinlock** (concurrent pausers:
  SD from loop() + camera DMA, on different cores — atomic counter/request
  pair, clamped counter, ack re-checked before drawing).
- **Shared I2C bus lock** (`hal/I2cBus.h`): the CoreS3 internal bus
  (`M5.In_I2C` — IMU, AXP, RTC, screen touch, camera SCCB, audio codecs) and
  the body bus (`Wire1` — PY32 LEDs, Si12T head touch, INA226 gauge) sit on
  **the same physical pins G11/G12**. Since `m5gfx::i2c` is not thread-safe,
  every transaction (IMU, SCCB, battery, charge LED, touch, body LEDs) is
  taken under a short FreeRTOS mutex with priority inheritance.
  The Brain **never skips** an IMU read: at worst it blocks for the duration
  of one transaction. The ONLY exception: camera init holds the lock
  EXCLUSIVELY for the SCCB config burst (~0.5 s — a non-exclusive burst
  corrupts the sensor); the ALDO3 power cycle (~650 ms) happens OUTSIDE the
  lock, and the VOR stays alive throughout.

## Rendering, eyes, expressions
- **The robot WAKES UP instead of snapping to attention** (08-03, user's own
  design). The head used to slam into the lower stop on every companion boot:
  the idle release drops torque wherever the head is, nothing holds it, it sags
  onto the stop, and the next boot re-engaged torque against that stop and
  pushed. Never on a guest bin, which writes nothing to the servo bus.
  Two mechanisms fix it, and the choreography is what makes it read as intent.
  `ServoMotion::begin()` now **releases the torque the instant the bus exists**
  — a limp servo cannot force anything — and **seeds the pose from a
  MEASUREMENT** rather than assuming it equals the start values, which is what
  made every later trajectory interpolate from a fiction and jump the difference
  on the first move. `readDeg` is legal exactly there: the motion task has not
  started, so the half-duplex bus is idle and there is nothing to corrupt.
  `homeSlowly()` then re-engages the torque **at the measured pose**, so nothing
  is forced at that instant, and walks the target up to HOME over 1.8 s against
  the ~400 ms of an ordinary move.
  Then the sequence: eyes shut, a blink, the head rises, eyes open, normal life.
  **The ORDER is the fix, and the first attempt had it wrong** (user: "I see
  Normal before Sleepy, and the servos engage before I even see Normal"): the
  Brain's first tick publishes the default face AND commands the neck through
  head-follow, which re-engages the torque. The emotion is now QUEUED BEFORE the
  Brain runs — the CommandQueue is drained on the first tick, so frame one is
  already Sleepy — and head-follow is held at zero until the head has arrived,
  then restored to the user's own value.
  Three more faults surfaced on target and in review (08-03/04), each hiding
  behind the previous one. The **measurement itself lied**: at 120 ms after
  power the SCS0009 does not answer yet, the read came back empty and fell
  back SILENTLY to the assumed pose — the exact fiction the change exists to
  remove, reinstated by a timeout; three retries 150 ms apart produced the
  decisive capture, `pose mesuree 167/71` where the code assumed 93 — a 22°
  gap every first trajectory jumped. The **library attach was the residual
  mover**: it writes its start position onto a live, torque-on servo before
  our `torque(false)` can run, so the rail is now CUT across the attach and
  the release+measurement moved to `settleAfterPower()`, first thing on the
  live bus. And **`homeSlowly()` never actually started its ramp** (review):
  it stored the targets without bumping the sequence counter the trajectory
  task watches, so the 1.8 s rise did not exist — the head only moved because
  the Brain's next emotion change issued its own 700 ms move. It now goes
  through `moveTo()`, and the choreography waits the ramp out before posting
  Normal, whose own move then commands the pose the head is already at.
  **The eyes open on the IP, not on a timer** (user 08-04: "keep sleepy until
  StackChan acquires an IP"). Sleepy now holds through the WHOLE boot — head
  rise, SD loads, WiFi association — and Normal is posted right after
  `webApi->begin()` returns an address (STA or fallback AP): eyes that open
  before the robot can be talked to promise a readiness that is not there,
  and a sleepy face over a long association makes "no network" visible from
  across the room. The 120 s Sleepy hold is the CAP: a robot whose radio
  failed still wakes, because staying asleep forever reads as a dead robot.
  This also settled the review's own debt of the wake PRECEDING the STA
  connect: the sleepy seconds now are the association seconds.
  Renderer side, the LAST Normal flash was the renderer's own: the eye rigs
  are BORN in the Normal preset, so the first published emotion arrived as a
  Normal→Sleepy *morph* — replaying a face that was never published. The
  first consumed frame now applies INSTANTLY, shape and colour both: there is
  no previous on-screen state to morph from, and interpolating from the
  constructor's default is a fiction of the same family.
- **The NOTAM identity line stopped labelling its headline `Q)`** (08-03, user:
  "we print `Q)...` and then `Q)` again just below — isn't that misleading?").
  It was: the row below IS item Q, in full, and it *contains* those four
  letters. Two things labelled `Q)`, one of them a fragment, invites the reader
  to believe they are different fields. The code stays as the headline it
  actually is, unlabelled.
- *(Superseded the same day: an earlier entry here recorded the slam as
  "diagnosed, not yet fixed", with parking at HOME before release as the
  planned fix. The user chose the wake-up sequence above instead — the head
  is allowed to sag, and the boot stands it up slowly from wherever it lies.)*
- **The selected ADS-B source became the PREFERRED one, not the only one**
  (08-03). Its outage used to be the radar's outage: an empty screen, with no
  way to tell "no traffic" from "no server". The preferred feed is still asked
  first, every cycle; on a TRANSPORT failure the others are tried in order and
  whichever answers serves the cycle - and the substitution is STATED on screen
  for as long as it lasts, because a radar showing traffic from a source you did
  not choose, silently, is worse than an empty one. A 200 with an empty sky is
  NOT a failure and does not trigger it, or every quiet hour would be chased
  through all three servers. SafeSky stays out of the rotation as a TARGET: it
  is account-bound, and falling back onto something the user has not configured
  is a surprise, not a rescue. It does fall back FROM (review 08-04) — the
  guard used to exclude SafeSky as the preferred source too, denying its users
  the exact rescue this mechanism exists for — except on a missing key, which
  is a setting error with its own on-screen diagnosis, not an outage.
- **"Unknown airport" was often "the database said nothing"** (08-03, user
  searching `CDG`). `CDG` is a perfectly good IATA code; hexdb.io had stopped
  answering - DNS resolving, TCP 443 accepting, the request hanging to the
  timeout while another aeronautical API answered 200 from the same machine.
  The lookup reported that as *unknown*, i.e. it told the user something false
  about their airport rather than something true about the database. The two are
  now separate: an HTTP 200 without a latitude means the code is unknown,
  anything else means the database did not speak, and the keyboard says which.
- **The METAR flight-category chip is always drawn** (08-03, user: "sometimes
  there is nothing where VFR should be"). Half normal - the category is NOT
  computed here, it arrives as `fltCat` from NOAA, and deriving it ourselves
  would mean contradicting the official source on a safety-relevant call; NOAA
  omits it often, and a comment in `fetchMetar` already noted that FMEE does so
  regularly. The other half was a defect: the row was SKIPPED, so 34 px vanished
  and everything below moved up. The reader saw a different screen with no way
  to tell "the source said nothing" from "the display forgot" - the same rule
  this project applies to a truncation. `fltCatColor` already had a grey for the
  unknown; the chip could say "I do not know" and was never asked to.
- **The NOTAM screen was re-laid out to carry more** (08-03), and the field map
  is documented in `docs/guests/FLIGHT-RADAR.md`. A **deck strip** under the
  header gives three facts in four pixels: how many notices are in force, which
  one is open, and - by colour - how many URGENT ones are still behind. The
  chevrons left the middle of the text block for a **page strip** at its foot,
  which gave the text the full width of the rules (47 -> 50 glyphs, ~40 more
  characters per page) and let the page number leave the identity line. The
  validity line gained the **time left**, coarse on purpose - hours under two
  days, days beyond - because a minute count on a notice issued to the hour
  would be false precision.
- **Review of 08-03 — ten defects, most of them in the code committed that same
  day.** The ones that mattered: the new per-gauge change detection was armed by
  ONE of the three paths that black those rows out, so an alert or a `/api/say`
  left the band permanently black (its own comment stated the principle the code
  broke); `servo_idle_release_ms` went 15 s -> 4 s without a `CFG_VERSION` bump,
  so the whole servo-release fix never reached a robot that already had a
  `config.yaml` — i.e. every robot that had the bug (schema **v5** added);
  `/api/sd/put` and `/api/sd/delete` never invalidated the `/bins` name cache,
  so a guest uploaded from the console was listed with a Launch button that
  answered 404; the deferred-listing job used one shared `dead` flag with a
  per-request callback, so a disconnect belonging to an ALREADY ANSWERED listing
  killed the next one silently (a generation counter now); the servo rail was
  cut on the API path only, so the touch launcher — the path users actually take
  — handed a locked neck to the guest, and a failed flash left the neck
  unpowered for the session; `enableServoPower()` was called under the I2C guard
  and ends in `delay(300)`, the one thing A2.21 forbids outright; `danceStore
  .reload()` ran outside its `pause()` bracket; and the ghost buffer was
  invalidated on EVERY resume, so the camera's ~10 fps pauses gave back the
  dirty-band gain on one frame in three (`pause(willPaint)` now).
  Two gates were not biting either: the palette separation test skipped every
  pair TOUCHING the neutral cyan rather than cyan-vs-cyan, and the A2.22 binary
  check did not cover the companion at all — including `drawBlush`, rewritten
  for that very rule the same day.
  And `intensify(rgb, t)`'s weight did nothing: routed through `blendRgb888` it
  inherited the rescale-to-brightest-parent, and the brighter parent is white,
  so every result was pinned at maximum luminance whatever `t` said and `t = 0`
  was not the identity. Three emotions were tuned through a knob that did not
  exist. Made a plain lerp; `Suspicious` then had to be re-found against the
  gate (0.20 -> 0.45).
- **The renderer pushes only what CHANGED, and the frame went from 26 ms to
  4-11 ms** (08-03, measured on target). The diagnosis was that `pushSprite`
  moves 320x160 px to a 16 bpp panel = 102 400 bytes on SPI2 at 40 MHz = 20,5 ms
  of pure wire time, while ALL the drawing code together is ~3,5 ms - so the only
  lever of the right order was the number of pixels put on the wire, not the
  geometry. A ghost buffer in PSRAM is compared band by band after the draw and
  only the runs that differ are pushed (`engine/DirtyBands.h`, tested natively).
  Measured on the robot: **1 600 to 14 400 pixels out of 51 200**, frame average
  3 900 to 10 800 us against 26 062 before. The comparison is on PIXELS ALREADY
  RENDERED, never on channel values - that is what keeps it clear of A2.15,
  which reserves every smoothing decision to the Brain. The ghost is invalidated
  wherever a third party paints into the eye zone (launcher, SD-Updater, busy
  screen), the same shape as the existing band invalidation, and falls back to
  the full push when PSRAM is unavailable.
- **The status band stopped flickering** (08-03, user: "it shimmers, sometimes
  blinks"). Two defects, and the comment sitting there claimed neither existed -
  it read "redraw throttled + only when a value has changed", and there was NO
  change detection at all. A comment asserting a guard nobody wrote is worse
  than no comment: it stops the next reader from looking. Each gauge row was
  WIPED TO BLACK and redrawn straight onto the panel five times a second, so
  unlike the eye zone - which renders into a canvas and pushes once - the black
  frame was really on screen. Rows now render into a 320x15 sprite pushed in one
  go, and a row whose value, label and right-hand text are unchanged is not
  touched. Band stage: **2 369 us -> 248-716 us** per frame.
- **The three listing routes left the AsyncTCP callback** (08-03). `/api/bins`,
  `/api/sd/list` and `/api/sd/get` read the card inside the callback - blocking
  I/O where A2.6 allows only `post()` and Tuning writes, on the SPI2 bus the LCD
  shares. They could not be deferred like a write, because the card's content IS
  their response body, and they could not borrow `renderer.pause()` either: it
  waits for the end-of-frame ack, up to 500 ms of `vTaskDelay`, and blocking the
  AsyncTCP task for a third of a second is worse than the contention. So the
  request is PARKED, `loop()` builds the body into PSRAM with the renderer
  paused, and the response is filled by a memcpy on the AsyncTCP side. One job
  at a time: a second concurrent listing is refused with a clean 503 rather than
  corrupting the first. A client that disconnects mid-flight is caught by a weak
  request pointer plus a disconnect flag, and the PSRAM block is owned by the
  response so it is freed exactly once either way.
  One bug found on target rather than by reading: `sendPsBody(req, buf.release(),
  buf.len)` - the two arguments are INDETERMINATELY SEQUENCED in C++, GCC
  evaluated `release()` first, which zeroes `len`, and the endpoint answered
  HTTP 200 with correct headers, no Content-Length and an EMPTY body. It looked
  exactly like a card fault and was a sequencing rule.
- **The emotion palette is a SYSTEM, not thirty choices** (08-03, reference
  images in `docs/assets/insideout - *.png`). It was twelve hand-picked hex
  values grouped by intuition, and the grouping showed: Happy, Glee, Excited and
  Smug were the SAME yellow, so four very different faces lit the LEDs
  identically, while Surprised and Awe were plain white.
  Disney/Pixar's **Inside Out** already solved this shape - a small set of base
  feelings with one unmistakable hue each, and a published chart of what every
  PAIR adds up to. Nine bases (five from the first film, four from the second)
  and blends that are COMPUTED, so a pair that looks wrong is fixed by moving
  ONE base and every blend follows. The DIRECTIONAL chart is the one used: the
  row leads, the column sits underneath, so Joy+Disgust is "disdain" when joy
  leads and "ironic" when disgust does - exactly what the blend weight
  expresses, where a symmetric chart would have wasted it.
  **The black background drove the engineering.** Mixing two saturated hues in
  sRGB does two damages, not one: it darkens AND washes out. Restoring only the
  brightness left Frustrated, Scary and Blush as three neighbouring pinks. The
  blend now restores the parents' SATURATION before recovering the light - hue
  from the mix, bite and energy from the parents.
  Two native gates lock it (`test/test_emocolor`) and both bit during the
  writing: **contrast >= 3:1 on black** after the default dim caught Sleepy at
  2.99:1 - the SECOND time that exact emotion has had to be lightened for that
  exact reason, the old palette already carrying the note "a darker violet
  becomes unreadable once dimmed" - and **no two colours closer than 32** caught
  Doubt at 0x8889FF against Dead at 0x8899FF, sixteen units apart on one
  channel. The weights were SEARCHED, not picked: hand-tuning does not converge
  (every fix pushes a colour into its neighbour), so the free part was chosen by
  maximising the smallest distance between any two of the thirty under the
  contrast floor. It settles at ~34, and that is a ceiling - thirty colours in a
  finite gamut, half of them lifted towards white to survive black.
  Four DECLARED exemptions, each with its reason, because colour is not the only
  channel: Happy/Glee/Excited share one amber (a plain smile, a squeezed grin
  and wide eyes with a star are already three unmistakable faces - which
  deliberately reverses the finding that opened the rewrite), Surprised and Awe
  are both white (being startled is not a kind of joy), Angry and Furious are
  the same red vivified rather than whitened (fury is anger with the volume up,
  and blending towards white would have drifted it to pink), and **Dead is let
  through at 2.3:1** - a dark GREY, and grey is the point: the other
  twenty-nine all carry a hue, so the one face that is not a feeling is the one
  with none, and cannot be read as a quiet version of anything. A "dead" that
  shines as brightly as Happy would contradict itself.
  The robot KEEPS ITS CYAN on Normal, Focused and Squint: Inside Out has no
  character for "nothing in particular", and this robot spends most of its life
  there. Borrowing a system is not surrendering the face.
- **Dead is immobile, Sleepy is pinned** (08-03). Gaze, VOR, breathing and
  squash are frozen for Dead at the ONE place the face state is published - in
  the Brain, not the Renderer: rules 3 and A2.15 put every continuous channel in
  the Brain's hands, and a Renderer deciding on its own to ignore a channel for
  one emotion is the bug those rules exist to prevent. The head keeps its own
  raised, agonising choreography; what stops is the eyes. Sleepy rests 20 px
  above the floor of the eye zone and freezes its VERTICAL channels only - gaze
  y, breathing and squashY, that last one because a vertical squash moves both
  edges even when the centre does not. Looking sideways still works: a sleepy
  face that cannot move at all is a dead one, and the table already has that.
  Both REVERSE earlier recorded requests (`VALIDATION.md`: "Dead: the cross
  follows the gaze"; the 2026-07-12 verdict on Sleepy's bottom alignment). Both
  are written down as superseded rather than quietly dropped - a preset value
  with a user's name on it is not a free parameter.

- **VOR** (vestibulo-ocular reflex): direct gyro counter-rotation, bias
  learned while at rest, dead zone, gated complementary drift, catch-up
  saccades towards the tilt target, servo efference copy; sensor→screen
  mapping tunable at runtime (`gyro_*`, HW-validated); shake (3 axes) →
  Scared; PickupDetector → Curious ("dangling feet", torque released).
  Reflexes are PREEMPTIVE (they cut everything). **Manual interactions take
  priority** (A2.5): a screen swipe, a head stroke (Si12T press/forward/back)
  and a console/API emotion all cut the running dance (AbortDance before
  SetEmotion) — otherwise its keyframes re-apply their own emotion and mask
  the reaction.
- **Living idle**: fixation → crisp saccade → micro-overshoot + procedural
  squash & stretch; BlinkController with per-expression policies + a Sleepy
  "fighting off sleep" state machine; "Cozmo" blink (both eyes closed = a
  1 px line laid on the bottom edge).
- **Bounded frame budget** (33 ms / 30 Hz): the special renders (the Dead "X",
  the Excited star, and **the blush since 08-03** — this claim was FALSE for it
  until then, `drawBlush` still drew eight anti-aliased strokes on every frame
  of Blush, Glee and Smug; found by the frame-budget audit, which is what an
  audit is for) use CHEAP primitives (`fillTriangle`/`fillCircle`,
  scanline) and not `drawWideLine` (anti-aliased, expensive) — a frame over
  33 ms would starve `loop()`'s touch polling (lower priority, same core 1)
  and freeze swipes. Hardened anti-starvation guard: an over-budget frame
  grants loop() 3 guaranteed ticks of respite. The Dead X is stamped with
  `fillCircle` in **a single loop with a single call site** — a constraint
  VERIFIED ON TARGET: GCC 8.4 Xtensa drops from the binary the second of two
  similar drawing calls inside the same body (the / arm was never drawn,
  proven by reading back the canvas buffer + a serial call counter).
- **The frame is MEASURED PER STAGE** (08-01). The heartbeat used to publish
  one number (`frame avg 27536us max 42066us`) — over the 33 ms budget at the
  peaks, and silent about where the time went. It now splits the two stages the
  frame is made of, `drawFrame` (the eyes) and `drawStatusBand`:
  `frame avg 26062us max 40231us (eyes 24097us band 1956us)`. Measured on
  target: **the eyes are ~92 % of the frame and the status band ~7 %**, so any
  work on the budget belongs in `drawFrame` and nowhere else. Two
  `esp_timer_get_time()` calls per frame, kept permanently — the cost is
  ~1 µs and the alternative is optimising blind.
- **30 expressions** (+ overlay effects in `engine/EyeEffects.h`: blushing
  cheeks — anime style: 4 thin parallel strokes of different lengths, nearly
  vertical, mirrored on each cheek; **anchored under the REAL bottom edge of
  the eyes and following the gaze** (gaze + squash), spread apart so they
  extend slightly past the eye edges —, sparkles, sweat drop); random
  mirroring of asymmetries decided by the Brain and aligned with the
  direction of the dances; dynamic shapes (Sad "sky gaze", Curious "edge
  eye"); equidistant eye centres (`eye_spacing`).
- **15 dances** (Sequencer, unified keyframes covering servo + expression +
  eyelids + gaze, reflex abort) + **CSV choreographies on the SD card**
  (`DanceStore`).
- **CRT effect** (LUT) switchable at runtime; **EmotionLeds** (PY32) +
  **SoundFx** valence-based chirps, off by default.

## Head / servos

- **Head posture per emotion**: home pitch 93°, `Brain::pitchBiasFor`
  (Sad/Sleepy/Blush lower it, Smug/Excited raise it), bias preserved by
  head-follow (on by default). **Dead** raises the head to a raw pitch of
  **55** (agony, throat exposed) — without holding the end stop (servo
  stall); a SHARP 400 ms rise (other emotions keep the gentle 700 ms nudge),
  ~2 s held up.
- **Y servo limits = OFFICIAL M5Stack SPEC**: the safe range 5–85° official =
  raw 19–99 (`PITCH_MIN`/`PITCH_MAX`, `raw ≈ 104 − official`). Holding the
  end stops stalls the servo (permanent damage) — they are never reached. The
  maximum reachable head-down bias is derived (`PITCH_DOWN_MAX = PITCH_MAX −
  PITCH_NEUTRAL`), never hard-coded, so that any recalibration propagates.
- **Generalized return to home** (`head_home_ms`): after a pause with no head
  activity and no sound, the head returns home. The target is clamped to the
  reachable servo range → the rest condition always converges (no looping
  servo re-commands).
- **`GET /api/servo/pos` — the MEASURED pose** (08-02), as opposed to
  `/api/status`, which reports what was COMMANDED and can never confirm the
  servo arrived. The SCS0009 bus is **write-only in operation** — reading it
  between two `WritePos` corrupts them and the servos go mute (tried and
  reverted 2026-07-21) — so the read is guarded on `servos = 0`, where the
  motion task writes nothing and the bus is idle. It answers `valid:false`
  otherwise rather than a stale number. Sampling lives in `loop()` at 5 Hz, not
  in the AsyncTCP callback: a blocking serial read has no business there
  (A2.6). Registered BEFORE `/api/servo` (A2.19).
  What it established, on hardware: commanded 166.0 → **measured 166.9**, and a
  full turn by hand reads **0.3 → 300.0 then decouples** — the potentiometer
  has no track over the remaining ~60°, which is why a position is ambiguous
  after several revolutions and why `YAW_RANGE = 130` keeps `[36, 296]` inside
  the readable arc.
- **Console header**: `clock` (NTP-synced UTC, `n/a` until the first sync) and
  `night` (what SoundFx decides from it and from `lat`/`lon`) join the chips,
  and **uptime is written in units a human reads** — `4j 18h`, `5h 12min`,
  `42min`, seconds only below a minute. The two largest units only: `4j 18h
  32min 43s` makes you hunt for the part that matters.
- **Servo remote control** `POST /api/servo` (absolute/relative) + automatic
  torque release after inactivity.
- **`servos` option** (0 = servos disabled): torque RELEASED (limp head,
  movable by hand, zero consumption) + no bus writes at all — VOR/dances/
  head-follow are physically inert, the eyes keep going; the shake→Scared
  reflex stays ACTIVE (isMoving does not publish a frozen trajectory).
  Re-enabling is a **soft resume**: the PHYSICAL position is read back
  (`ReadPos`) and the trajectory is rebased onto it — a head moved by hand
  does not snap when torque comes back.

## Camera (GC0308 → Home Assistant / Frigate)

- `camera` option (off by default): **live** view `GET /api/camera/still.jpg`
  (compressed by `cam_stream_quality`, **320×240** if `cam_stream_qvga=1` —
  ~4× fewer bytes over the air, the default), **full-quality VGA** snapshot
  `?full=1` (capture on demand, 503 until it is fresh), **MJPEG stream**
  `GET /api/camera/stream` + state in `/api/status` (`cam`, `camErr`) + live
  view in the console + sensor settings at runtime (`cam_fps`, `cam_quality`,
  `cam_brightness`, `cam_contrast`, `cam_saturation`, `cam_vflip`,
  `cam_hmirror`, `cam_lowlight`, `cam_colorbar`).
- HAL `hal/Camera.h`: capture + JPEG encoding on a **dedicated task** (core 0,
  prio 1 — preempted by servos/network: dances stay fluid and HTTP stays
  responsive during the stream), JPEG kept in **PSRAM** (the internal heap no
  longer runs dry under streaming), init on demand, **auto shutdown after 60 s
  with no consumer**, graceful failure (sensor rail cut), capture **inhibited
  during flashing** (OTA, `.bin` upload/launch). Frame rate slaved to actual
  consumption; `camera=0` cuts capture, the console view AND any open streams.
- **Reliable init**: robust ALDO3 power cycle (the GC0308 has neither RESET
  nor PWDN wired), exclusive SCCB burst (bus lock), frame-gate reset on
  deinit — `camera=0→1` cycles deliver frames again without a reboot.
- Hardware obstacles solved (details in
  `docs/integrations/HOMEASSISTANT.md §Caméra`): SCCB routed through
  `M5.In_I2C` (`SCCB_*` override, `sccb_m5.cpp` — under the bus lock),
  partial frames (renderer paused during DMA), YUV422 + `frame2jpg` (no
  hardware JPEG — `cam_quality` mapped correctly onto the software encoder),
  official Espressif `esp_cam_sensor` init table (settings applied as direct
  register writes).

## Status band & plugins (contract `sources → fields → {widgets, rules}`)

- **Blackboard** (`engine/FieldStore.h`): a store of named fields (float +
  string), the SINGLE source both for the bottom-of-screen display AND for
  reactions. Every source (`POST /api/field`, sensors, scripts, future BLE)
  writes fields; nobody wires a dedicated path.
- **Status band** (bottom of screen, `Renderer::drawStatusBand`, drawn by the
  renderer — rule 1): modes for the dynamic zone (`POST /api/statusbar` —
  none / stereo VU meter / gauges), **persisted** (`band_mode`, survives a
  reboot) + a row of **maskable** icons (`icon_mask` — battery, WiFi,
  camera = red dot, mic, night; colours **independent of the eyes**, battery
  green while charging) + **optional `emotion·ip` debug info** (`band_debug`,
  centred within the icon row, no longer a mode) + `POST /api/say`
  notifications (long text → **marquee scrolling**, tunable via
  `band_scroll_speed`, size via `band_text_size`, 160-char buffer, region
  cleared before/after) and the battery alert (both take priority). Gauges
  have their own per-row colour. **A horizontal swipe inside the band area**
  (y ≥ 160) cycles the modes (left = next, right = previous, persisted —
  coalesced: a burst of swipes = a single SD write); above it, an L/R swipe
  cycles the emotions; **vertical swipes inside the band are inert** (no
  dance/launcher on a diagonal gesture). `band_mode` is **validated** by
  loop() ({0,2,3} — an inherited invalid value, e.g. the former debug mode 1,
  is brought back to 0). Per-region change detection. Dedicated reference:
  **`docs/reference/STATUSBAR.md`**.
- **Rule engine** (`behavior/RuleEngine.h`, PURE, tested): declarative
  `field → whitelisted Command` rules (or `→ field`), with "held edge"
  semantics (sustain + cooldown + gate). **`dark_sleepy` is a declarative
  rule** (not an ad-hoc state machine). Reflexes still take priority (A2.5).
- **`POST /api/bins/launch` no longer fails silently** (08-01): the size test
  `sz > 0 && sz <= partition` had no `else`, so a size of 0 did nothing at all —
  no flash, no message — while the API had already answered "flash + reboot in
  ~2 s". The caller was told the OPPOSITE of what happened, which is the worst
  of the three possible answers. Both refusals now say why, and the open is
  RETRIED three times: right after a reboot or a large upload the card really
  does answer 0 for a file that is plainly there (observed three times while
  deploying the guest bin), recovering a few hundred ms later. The read is
  paused/resumed around the renderer (A2.16).
- **The console is served GZIPPED** (08-01): 65 563 B on the wire become
  22 857 B, and the firmware SHRINKS by 51 468 B because the compressed bytes
  replace the plain ones in flash rather than sitting beside them —
  `WebApi.h` no longer includes `WebConsole.h`, so the plain literals are never
  emitted (no reliance on `--gc-sections`). Generated by a PlatformIO `pre:`
  script so it cannot be forgotten, byte-stable (`mtime=0`) so an unchanged
  console triggers no rebuild, and the array is gitignored. Compile time went
  DOWN (15.6 s -> 10.6 s: a 22 KB byte array parses faster than 77 KB of raw
  string literals). The language trailer could not survive a gzip stream, so
  the language now travels as a `Set-Cookie` on the very response carrying the
  page — stored while the headers are processed, i.e. BEFORE the parser reaches
  the inline script: no extra round trip, no flash of the wrong language.
- **Night follows the SUN, not a clock window** (08-01,
  `firmware/common/SunClock.h`, 15 native tests): the flight-radar's Night
  theme used a fixed 21 h -> 7 h, and at Reunion sunset moves 1 h 13 across the
  year, so in June the screen stayed bright more than three hours after dark.
  NOAA solar position from the radar's OWN latitude and longitude — right
  wherever the bin runs, not just here. One departure from the published NOAA
  form, and it is worth 4 minutes at Paris: measuring the solar phase in
  TROPICAL years since 2000 instead of re-zeroing it every 1 January, which
  drifts a quarter-day per year and resets on leap years.
- **Reactive SD plugins** (`app/RuleStore.h`, `/stackchan-companion/rules.txt`,
  hot reload via `POST /api/rules/reload`): the community adds behaviours
  (Claude-buddy, automations) by editing a file — same conventions as the CSV
  dances. Details: `docs/reference/PLUGINS.md`.
- **`ha-remote` guest bin** (`firmware/ha-remote/`, dedicated env): a Home
  Assistant remote — here the robot is a CLIENT of your home automation (the
  reverse of `docs/integrations/HOMEASSISTANT.md`). Home screen by category
  (covers, lights, plugs, cameras) with counts, single-entity list screen
  (swipe ←/→), per-domain actions, camera thumbnails via `/api/camera_proxy`.
  **Continuous settings**: a cover's position (including mid-travel), a
  lamp's brightness / white temperature / colour — the controls follow the
  CAPABILITIES declared by `supported_color_modes`, never the presence of a
  value (a lamp that is off stops publishing its brightness). Sliders follow
  the finger locally and only call the service **on release**. **Near
  real-time feedback** at two rates: the displayed entity is polled on its own
  (`/api/states/<id>`) at 1 Hz, and at 400 ms during the 8 s that follow an
  action — the full catalogue stays on `poll_s`. No home-made "close
  everything" action: a Home Assistant GROUP is already an ordinary `cover.*`
  entity in the list. JSON documents are filtered and allocated in PSRAM (the
  internal heap is shared with WiFi/TLS), 64 entities max. Long-lived token on
  SD, entered from the bin's web page in a **`Secret`** field.
  **Screens modelled on actual use**: a four-card home screen with an icon and
  an *active/total* counter (`3/6`) plus link status; a cover commanded the way
  it moves (draggable vertical curtain, `OUVRIR`/`STOP`/`FERMER` — open/stop/
  close — butted together over the full height); a lamp with an **adaptive**
  layout depending on its capabilities, 34×30 pads and a **single power
  button** that offers the only useful action; a **full-screen** camera view
  refreshed automatically, JPEG **and PNG**, scaled from the dimensions read
  in the header. Settings panel on a **right swipe**, the same gesture as
  `flight-radar`. Details: `docs/guests/HA-REMOTE.md`.
- **`ha-remote` — WHICH 64 entities survive** (08-03). The table holds 64 and a
  real installation serves several hundred; it used to be filled in the order
  `/api/states` served them, so what went missing was the *last* entities
  served, not the least useful ones. An installation serving four hundred
  `light.*` before its first `cover.*` showed **zero covers** — and the home
  screen announced that zero with the confidence of a real one. The choice is
  now explicit and PURE (`firmware/ha-remote/entsel.h`): entities pinned by the
  user (`entities:` key, matched as a PREFIX, so `cover.` pins a whole domain)
  and **the entity currently on screen**, then those that answer before the
  `unavailable` ones, then an equal share between the four categories with the
  leftovers redistributed, Home Assistant's own order breaking ties. Tested
  natively — `test/test_entsel`, 22 cases (17 suites / 266 tests in all).
  The other half of the fix: **the truncation stopped being silent.** The home
  header reads `64 of 213`, each card carries its `+n`, so does the list header,
  an emptied category says `9 not loaded - list too long` instead of "no entity
  in this category", and the settings panel states the shortfall next to the
  REFRESH button. A cap that truncates in silence reads as "everything is here".
  One bug found on the way: `curId` is a 47-character TRUNCATION of the entity
  id, compared with `strcmp` against the full id from the JSON — so any entity
  whose id ran to 48 characters never matched itself, and the one entity the pin
  exists to protect was exactly the one it could not protect.
- **The neck is released for real when a guest bin takes over** (08-02), and it
  took THREE layers because each covers a window the others cannot:
  1. **Idle release at 4 s** (was 15) while the companion runs. Fifteen was
     right for a *commanded* move and wrong for the autonomous posture nudges
     that arrive in a loop: every emotion carries a pitch bias, the home return
     re-engages the torque to follow it, and the timer restarts — with the
     roulette running the neck was held permanently. The idle is now counted
     from the END of the trajectory, not its start.
  2. **An acknowledged release at both doors** to a guest (`POST
     /api/bins/launch` and the touch launcher). `releaseTorque()` only posts a
     request; both paths happened to have seconds of work before the flash, so
     it landed by accident of timing, not by guarantee. It is now waited for,
     bounded at 400 ms, and proceeds anyway on timeout — the same shape as the
     netTask parking ACK.
  3. **The VM_EN rail is cut.** The decisive one: `EnableTorque(id,0)` writes a
     register INSIDE the SCS0009, and those servos power up ENABLED. The reboot
     that starts the guest re-asserts VM_EN and erases the release we had just
     confirmed — limp under the companion, locked again under the guest.
     The PY32 keeps its GPIO across our reset, so cutting the rail is the only
     release a reboot cannot undo. Torque first, rail second: cutting power
     under a driven servo is the jolt this project spends its time removing.
     Nothing is stranded — the companion re-enables the rail at boot, and a
     guest that drives the neck does it in its own init.
- **`POST /api/bins/launch` releases the servo torque before handing the
  machine over** (08-02). The INTERACTIVE launcher already did ("servos torque
  released = mechanical silence"); the API path did not, and both end in the
  same place — a guest firmware running on this board. A guest that does not
  use the servos never touches the bus, so it inherited what we left: the head
  stayed locked, warm and unmovable by hand, with nothing on the robot able to
  explain why. Torque is a physical state that outlives our process (the SCS
  registers keep it across a reboot), so releasing it belongs to "we are
  leaving", not to "the user swiped".
- **TAF: the "in force" bar now covers the BULLETIN, not only its change
  groups** (08-02). The prevailing-conditions line carries no `DDHH/DDHH` of
  its own — the validity is in the header, which the body skips — so it could
  never be marked, and as soon as a TEMPO ended the screen showed no bar at all
  and read as "nothing is in force". Range comparison factored into
  `fr::ddhhRangeCovers`, shared rather than copied, with 5 native tests
  including the live FMEE bulletin. Also: `metar_icao` now DEFAULTS to `FMEE`
  to match the built-in Réunion coordinates (empty + empty `airport` meant no
  station at all, hence no METAR and no TAF, on any board without an SD card);
  a failed METAR is LOGGED (only success printed, so a failing fetch and one
  never attempted looked identical); and the heartbeat carries `clock:` because
  a board with no RTC — the Fire — cannot otherwise be asked whether SNTP has
  answered, which silently disables the in-force bar, the Night theme and local
  times.
- **A notification banner is now a FILLED BAR** (08-03): the message's own
  colour as the ground, **black text, centred** — instead of coloured text on
  black like everything around it. On screens where every value is already
  colour-coded, one more coloured line among forty read as *data* rather than as
  an *event*, and inverting the ground is the one move that cannot be mistaken
  for a caption. On the reading screens it covers the raw band WHOLE — all
  24 px, text centred on both axes. A first attempt made it one 8 px text line,
  which looked like a bug and was one: the METAR writes up to THREE raw lines
  there, so the other two stood beside the banner and read as pixels it had
  failed to clear. And when a banner's window closes the band comes back **on
  the millisecond**: the sticky messages deliberately keep the frame clean (a
  5-minute message redrawn at loop rate is the 25 % of `loop()` that the old
  8 fps redraw cost), so nothing was watching for their end — the bar sat on the
  raw text until the 1 s counter tick happened to redraw. Each windowed message
  now publishes its expiry date and `loop()` arms exactly one redraw on it.
  The radar's **hints** stay plain text —
  a hint is permanent furniture, and a permanent colour bar is a standing alarm.
  Black on the eight possible grounds (`accent` and `alert` × four themes) is
  gated by `scripts/gates/check-contrast.py` at the WCAG AA text threshold; tightest
  is Scope night's pure red at 5.25:1. A2.22 holds: the shared helper paints the
  ground and picks the ink, it does **not** take the `drawString` with it — that
  would have emptied the two symbols `check-a222.py` watches and silenced the
  gate rather than satisfying it.
- **The SD card's presence is a MEASUREMENT again, not a boot-time verdict**
  (08-03). `board.hasSD()` was decided once at startup and believed for the rest
  of the run, so pulling the card left `/api/status` answering `sd:1`, the
  console offering files that were gone, and a 1.7 MB upload writing into a
  stale mount — a 3.4 s renderer freeze on the timeout, and an `import failed`
  that reads like a card fault rather than an absent card. Found by doing it:
  the card was moved to the other board mid-session. `loop()` now re-probes
  every 3 s (one directory open, renderer paused around it and ONLY around it),
  and while the card is absent the same tick attempts a full `end()` + `begin()`
  remount — a flag that only ever goes false would make the first removal
  permanent until the next boot, and a re-probe that merely looked would report
  a card that still cannot be written to. A state change resynchronises what
  depended on it: the `sd` field, the `/bins/` name cache, the dances.
- **A settings save no longer costs an autorouter token** (08-03). `settingSet`
  is called for every field the form POSTS, not for the ones that moved, and
  `notam_user` is a Text field the browser echoes back on every save — so
  changing the BRIGHTNESS dropped the cached bearer, the next NOTAM query
  re-authenticated, and that mints one out of an allowance of twenty a week.
  The token survived reboots and power cuts, as designed, and died of an
  unrelated save. Both credential fields now act on a REAL change only. Its
  twin `notam_pass` never showed the bug because a Secret field left empty
  means "unchanged" and never reaches the handler — which is exactly how one
  of two identical branches stays invisible.
- **A board with no card now SAYS so, and the NVS token store is gone** (08-03).
  The fallback looked right — internal flash, so it survived a reboot and a
  power cycle, the answer to the boot-loop that can burn twenty autorouter
  tokens in minutes. **It was unreachable.** `notam_user`/`notam_pass` are
  settings, settings live in the yaml, there is no yaml without a card: after
  any reboot they are empty and `fetchNotam` returns `HTTP_NO_KEY` *before* it
  looks at the bearer. A stored token nothing can present is not a cache, it is
  a credential sitting in flash for no one. What a missing card costs is not one
  token, it is the whole configuration — including the WiFi credentials, which
  come from build flags and cannot be changed without a card either.
  So booting with no card raises a full-screen notice, and **it waits to be
  dismissed rather than counting down**: a six-second notice is one that gets
  missed, while the condition does not expire. Two ways out — `A` / left half
  remounts the card you have just inserted (and then re-reads the configuration
  for real: the yaml was parsed against no card and loaded nothing, so skipping
  that would make the retry a lie), `B`/`C` / right half runs without one. A
  retry that finds nothing says so and stays put. The trade-off is stated, not
  hidden: an unattended power cycle waits for a press. What keeps the warning
  alive afterwards is the radar footer, which reads `no microSD: settings are
  not saved` in place of the tracking hint for as long as there is no card —
  the hint is learnt once, "nothing you type is saved" never stops being true.
- **`flight-radar` also runs standalone on an M5Stack Fire**
  (`pio run -e flight-radar-fire`, 08-02) — same source, no fork. The board
  differences are *capability flags* declared in one `BOARD PROFILE` block
  (`SCE_INPUT_BUTTONS`, `SCE_HAS_SERVO`, `SCE_HAS_LTR553`, `SCE_COMPANION`,
  `SCE_SD_*`), named after what the board **has** rather than after a board.
  The real obstacle was never the K151 hardware — it was that the Fire has
  **three buttons and no touchscreen** while the whole navigation was
  gestures. So the actions got names: `firmware/flight-radar/input.h` holds a
  pure `UiEvent` vocabulary and a natively-tested button state machine
  (`test/test_input`, 14 cases, including that a long press must not also
  emit a short one on release); touch and buttons are two **producers**,
  `applyEvent()` the single **consumer**. The two touch modals (8×5 keyboard,
  slider panel) are not ported but **replaced** by `http://<ip>/config`, which
  gains the one field it lacked — `track` — sharing the keyboard's exact
  commit path instead of a second copy. Not ported to the Core Basic: no
  PSRAM, which the 150 KB canvas and rule 18 both depend on.
  ⚠ With both boards plugged in, `scripts/dev/find-port.ps1` resolves the upload
  port by **VID/PID** and refuses when ambiguous — a bare `-t upload` would
  overwrite the StackChan's companion.
- **`flight-radar` demo guest bin** (`firmware/flight-radar/`, dedicated
  PlatformIO env): real-time ADS-B aircraft radar (airplanes.live by default,
  adsb.lol/adsb.fi compatible, no key needed) — radar on the left, **info
  panel for the tracked flight on the right**, blips oriented to heading,
  colour by altitude. **Tapping an aircraft (blip OR callsign) = TRACK IT**
  (automatic re-lock by callsign, local trail, route via hexdb.io with
  FALLBACK to adsbdb.com — different databases, cities + airports in one
  call —, estimated `DÉPART~`/`ARRIVÉE~` (departure/arrival) in NTP local
  time — a network failure is RETRIED up to 5× with an 8 s backoff, and the
  route status is SHOWN on the panel instead of a mute "?"; a blip targeted
  before it has a callsign is armed on the next poll, diag 07-27). The three
  screens are a vertical **STACK** — radar, METAR, NOTAM — where a vertical
  swipe moves ONE step and never toggles: the state is a `viewLevel`, not one
  boolean per view. A fourth step, **TAF**, slots between the METAR and the
  NOTAM (07-31) and costs NO extra connection — `taf=1` on the request the
  METAR already makes returns the forecast in the same object. It is shown
  RAW, deliberately: a TAF is a sequence of conditional groups whose validity
  periods overlap, so glossing it means forecasting on the pilot's behalf —
  instead the groups are LAID OUT, a new line at each BECMG/TEMPO/PROB/FM
  (a PROBnn stays glued to the change it qualifies) and the TEXT SIZE is
  DERIVED: size 2 when the whole forecast fits at 25 glyphs x 11 lines, size 1
  otherwise — nothing is ever cut to keep the letters large. The
  order of the stack IS the meaning: what is happening, what is measured, what
  is forecast, what is out of service. The three READING screens share their
  chrome (07-31): the header (ICAO · station name · optional **screen label**)
  comes from ONE implementation, so they cannot drift apart — each had quietly grown its own
  first line, pitch and margins, invisible while separate gestures reached them
  and obvious once one swipe made them a cycle. Font rule: verbatim wire text
  in Font0 (fixed pitch — a code must never re-flow), our prose in efontJA_12.
  There is NO navigation footer: it cost a 12 px band on every screen to state
  what one swipe teaches once, and those pixels went into the rose instead
  (radius 78 -> 82). The METAR carries no screen label either — the rose IS its
  name — so the station name goes back to right-aligned. The wind figures left
  the rose for the top of the RIGHT margin, still in the accent so the colour
  ties them to the arrow — that row breaking the column rule twice, on purpose:
  its label is in the accent because it CARRIES a figure instead of naming one,
  and its value keeps size 2 up to 7 glyphs so the wind is not the small number
  on a card about the wind. On the rim they punched two opaque holes through the
  graduations and moved at every observation, and the rose could never be read
  as one object. The runway lost its end caps (a closed rectangle is a box, not
  a runway) and its centreline became DASHED and dim, drawn OVER the fill —
  road markings, running the length of the runway instead of stopping where the
  runway began. The rose is now an explicit STACK of five layers, each
  answering the one under it: runway fill+edges (the ground), dashed
  centreline, THEN the graduations and labels, then the runway NUMBERS (over the lines — a number cut by its own marking
  is the one ambiguity this card cannot afford), then the wind LAST over every
  bit of runway, because the wind is the measurement being compared TO the
  runway and must never be what gets hidden.
  The four screens are a **RING travelled by the swipe UP alone**; the swipe DOWN is **not ours at any step** and always means
  "back to the companion" (07-31). An earlier design armed that exit at the
  bottom step only and used down as "one step back" above it: it worked, but it
  made our navigation and the guest contract share an input, and the contract
  is not ours to bend. The companion's exit
  is armed on the **bottom step only** (a long swipe down is the very gesture
  that means "one step down" higher up, so left armed it would open the reflash
  dialog instead of going back — which is also why the cycle must not close
  downwards). Above the radar any tap returns straight to it. Swipe up from the
  radar = **METAR of the station** (aviationweather.gov, free, no key), where the
  **WIND ROSE IS the display**: centred on (160,114), radius 78 — the arrow
  ring, not the disc, is what fits the 180 px left between the title and the
  raw report. 36 graduations and twelve labels in tens of degrees, the runway
  drawn across it with both numbers — SURFACE AND SIDES running the whole way and
  a few pixels PAST the rim (07-31) the way the wind arrow leaves the disc, with
  no end caps because a closed rectangle is a box, not a runway (and the strip moved UNDER
  the rose so the graduations and labels draw on top of it — a filled strip on
  top swallowed whichever degree labels it crossed, which by construction it
  does whenever the runway points at a thirty); the numbers lost their bright plate the same day (the plate's
  fill became the glyph colour) — PLUS its axis extended rim to rim (read
  the runway heading off the graduations, and compare it with the wind in one
  glance — the crosswind component), and a hollow arrow on the rim at the
  azimuth the wind comes from, its speed and bearing written beside it rather
  than in a pill parked elsewhere — **plus, since 07-31, the wind's own trail**:
  its path across the field, DOTTED, rim to rim through the centre. The arrow
  alone marked a POINT on the rim and left the eye to carry that azimuth across
  the disc; dotted rather than solid because the shape carries the meaning (the
  runway is a real object and is drawn solid, the wind is a measurement and
  gets a construction line — two solid lines would read as two runways). The
  title line is **justified** the same day: code left, station name right,
  spanning exactly what the rule below it spans, width MEASURED and never
  computed from a glyph pitch. The runway is **looked up on the SD card**,
  `/stackchan-companion/runways.csv` — the OurAirports base (PUBLIC DOMAIN)
  filtered by `tools/generators/make-runways.py` down to ~14 200 fixed-width records of
  17 bytes, sorted by ICAO then by DECREASING length, which lets the bin
  **binary-search on offsets** (14 probes plus a confirming read, once per
  station change, from `loop()` alone — SD and LCD share SPI2) and land on the
  aerodrome's MAIN runway. A runway NUMBER is the MAGNETIC bearing rounded to
  the ten (FMEE's 12/30 really lies 102/282°, 18° off), which is why the TRUE
  heading is fetched rather than deduced. The `metar_rwy` setting (form
  `12/30@102`) still WINS when filled in — to force a secondary runway or to
  cover an aerodrome the base ignores; with neither, the rose is drawn alone.
  The data folds into the two 68 px margins the disc leaves free:
  `fltCat` colour-coded VFR/MVFR/IFR/LIFR by day (only when published) but
  encoded by WEIGHT under a night theme — outline / dim fill / solid / solid +
  inner frame — because Scope night's accent IS the IFR red and Gundam night
  holds blue at zero, so the international code either vanishes into the
  interface or breaks the dark-adaptation rule the palette is built on; the
  cost (no colour code at night) is stated on screen and in the docs,
  temperature/dew point and clouds on the left, QNH, visibility, **relative
  humidity DERIVED by the Magnus formula** (the report carries none) and the
  observation age on the right — **one shared column width of 60 px on both
  sides**, size 2 when the value fits it, size 1 otherwise, and every string
  clamped so none can run into the rose. Cloud codes carry a **plain-French
  gloss** under them (`CAVOK` → degage, `BKN` → fragmente, `///TCU` → non
  mesure + bourgeonne): the code is what a pilot reads, the gloss is what
  everyone else reads. Title = ICAO + station name on one line, and the raw `rawOb` a
  pilot reads stays verbatim at the bottom. Own 10-minute period, fetched only while the view is open;
  every row is built only when its datum exists (absent temperature, dew
  point, QNH or category simply do not show). Metric visibility in
  KILOMETRES, and a trailing `+` is read as the METAR group 9999 —
  "10 km+", never a converted 9600 m; left = keyboard
  (callsign to track OR IATA/ICAO airport code to recentre, persisted);
  right = settings (radius 10-500 nm with tiling above 250, poll, source —
  all persisted); long swipe down = back to the companion. Top of the stack,
  the **NOTAM view** reads **autorouter.aero**, i.e. the EUROCONTROL EAD
  database (08-01). It stayed deliberately EMPTY for two days — no free
  key-less service answered for this station (FAA public search 403, `notamapi`
  401 without a key) and parsing a response nobody has ever seen is the one
  failure mode that matters on an aeronautical display; the parser was written
  against a REAL captured response (`tools/probes/autorouter-notam.py`), the method
  that produced the METAR view. Free but not anonymous: OAuth 2.0
  client_credentials reusing the ACCOUNT e-mail and password (no API key
  exists), and API access is a permission granted on a support ticket — an
  activated account alone returns 403 "privileges", which looks exactly like a
  wrong password and is not one. FMEE answered 27 NOTAMs of which 13 were in
  force, with E fields from 74 to 1673 characters: the view keeps the ones IN
  FORCE (only when NTP makes the clock trustworthy), sorts them by PIB purpose
  (NBO > BO > B > M, the aeronautical order and not ours) and shows ONE PER
  SCREEN, paged by HORIZONTAL swipes — the free axis, the vertical one being
  spoken for twice. A truncated text SAYS SO.
  **The bug that hid behind that feature** (08-01): the view answered a bare
  "-1001" while the very same request worked from a PC. `HTTPClient` only
  DECHUNKS through `writeToStream()` — `getStream()` hands back the raw socket,
  chunk-size lines and all, so ArduinoJson read the chunk framing and gave up.
  Every service the bin already talked to sends `Content-Length`, which is
  exactly why this stayed invisible until autorouter (Apache + mod_wsgi, no
  Content-Length) became its first chunked source. `fetchJson` now drains a
  chunked body through a bounded PSRAM sink (rule 18) and parses from there.
  **A long press** (700 ms, finger still) on any reading screen FORCES a
  refresh of the METAR, the TAF and the NOTAM: every other gesture was taken,
  and a double tap cannot work because the first tap already leaves for the
  radar. It clears the 60 s failure spacing too, otherwise the guard that stops
  a failing service being hammered would swallow the retry you just asked for.
  **Multi-leg routes** resolved correctly (the leg that
  brackets the aircraft, `fr::pickLeg` — tested natively), **airline** shown,
  **automatic brightness and Night theme** (LTR-553 + RTC, on by default),
  **pinch zoom** (the radius no longer has a slider), ground-traffic filter,
  automatic tracking of the nearest aircraft, **head pointed at the tracked
  flight** and **chirps**, whose `volume` is a LEVEL and not a switch (0..100 %,
  0 = silent and the only "off" there is; silent by default; HALVED at night on
  the same rule that swaps the theme, and the level scales the measured envelope
  instead of replacing it, so a quiet alarm is still the same alarm; set on the
  device by a SLIDER on the RADAR tab, in 5 % steps, previewing AT the level
  being set). Adaptive backoff on 429/5xx.
  **Switchable aeronautical ⇄ metric units** (distances, speeds, altitudes,
  vario — storage and API queries stay in aeronautical units), settings panel
  built on a **single geometry table** (drawing and hit testing read the same
  line description),
  WORLDWIDE tracking by callsign (/v2/callsign), flight progress (distances,
  %, bar with a marker), vario, emergency squawk, ICAO categories
  (helicopter/glider/microlight/light/military symbols, magenta + type),
  **4 paired day/night themes**: 0 Gundam, 1 Gundam night (saturated ORANGE,
  blue at zero: blue was what pulled it towards yellow), 2 ATC Scope, 3 Scope
  night (**ASTRO mode**: black background + PURE monochrome red, in the style
  of Stellarium/SkySafari — only three compliant text levels exist in pure
  red, so the hierarchy is carried by size instead; military is told apart by
  its SHAPE; only the altitude ramp slides towards amber, for want of any way
  to separate four bands between 3:1 and 5.25:1). Even =
  day, odd = its night: `auto_night` switches to `theme | 1` and comes back in
  the morning, while a night theme chosen by hand stays put. A SINGLE night
  theme made the theme you had just picked lose its identity.
  ⚠ The numbering has changed: `theme: 1` used to mean Scope.
  **Settings panel in THREE tabs** (Affichage / Radar / Reseau): eight groups
  inside 240 px forced 48×20 px targets and truncated labels; every target is
  now at least 97×26 px (tab strip 3 × 100×26 on the whole width — the
  "Reglages" title gave way, three named tabs already say what the screen is),
  labels are complete, and a SINGLE geometry table
  (`Grid`/`gHit`) is read by both the drawing code AND the hit test —
  hard-coded bands on both sides would have drifted the moment the theme went
  from three to four entries. The RESEAU tab holds the **source chips** and
  the diagnostics that used to hide behind the up swipe (IP, signal, memory,
  PSRAM, stacks, SD/HTTP, refreshed every second).
  **Fourth source: `safesky`** (api.safesky.app) — FLARM/advisory traffic
  (gliders, UAVs) that no ADS-B mirror sees. It serves **metres and m/s**,
  converted **at the entry point** so no second unit system travels through
  the code; its `rad` parameter caps at 20 km, so beyond 10.8 nm the request
  switches to the `viewport` bounding box (a user asking for 200 nm gets
  200 nm); it needs an **API key** (`Secret`, never echoed back, sentinel `-`
  revokes it) without which the chip refuses to be selected and the radar
  names the missing setting instead of showing "0 aircraft". Its `x-api-key`
  authentication is deprecated in favour of an HMAC-SHA256 signature — a
  known debt, documented in the source and in `docs/guests/FLIGHT-RADAR.md`.
  **RGAA/WCAG 2.1 AA contrasts verified by script**
  (`scripts/gates/check-contrast.py`, which reads the table out of the source and
  exits with an error if a threshold breaks): text ≥ 4.5:1, meaning-bearing
  graphical object ≥ 3:1, decorative elements exempt under 1.4.11. The
  measurement is taken **after RGB565 quantisation** and with the piecewise
  sRGB curve — a palette tuned in RGB888 with an approximated gamma gave a
  false verdict, and all three themes were in fact failing before this pass.
  Full legend always shown in the summary (the diagnostics moved to the
  RESEAU settings tab),
  48 aircraft ("48+" when saturated), efontJA accents.
  **Dedicated doc: `docs/guests/FLIGHT-RADAR.md`** (mermaid).
  Concurrency: netTask = sole TLS owner (requests by generation). SD config
  specific to the bin (`/stackchan-companion/flightradar.yaml`, editable from the
  console); the companion's WiFi is reused; remote stop via SceGuest.
  The bin's networking runs on a dedicated TASK (blocking TLS inside loop()
  made the SceGuest WebServer unreachable); CoreS3 SD init fixed
  (`SPI.begin` 36/35/37/4 is mandatory); SceGuest parser made QUOTE-AWARE
  (credentials serialised between quotes by the companion broke the guest STA).
  **Swipe down = back to the companion on EVERY guest bin** (detected in the
  SceGuest stub: on-screen confirmation then reflash — on by default,
  disableable by community devs via `guest.setSwipeExit(false)`; symmetric
  with the companion's swipe down → launcher). It demonstrates the whole guest
  chain from `docs/guests/README.md`. SD whitelist extended: every `.yaml`
  under `/stackchan-companion/` (guest bin configs, category "guest" in the file
  manager) **and `/companion.bin`** (the restore binary used by stop can be
  deployed remotely).
- **Hardening of the recovery paths** (max review 07-27): the boot cleanup
  RESTORES instead of deleting (`/companion.old`, `config.yaml.tmp`,
  `rules.txt.tmp` were the only surviving copies after a power cut between
  `remove` and `rename` — the safety net for getting back and the WiFi
  credentials went straight in the bin); `/api/bins` writes to `.tmp` and
  validates (bytes received, 0xE9 magic, 256 KB minimum) before `rename` — a
  truncated `.bin` passed the "size ≤ partition" guard ALL THE MORE EASILY and
  flashed a non-bootable partition; the launcher REFUSES to launch without
  `/companion.bin` (its confirmation screen promises the way back) and without
  an OTA partition; the band's touch zones are the buttons as DRAWN (an
  imprecise tap used to trigger a 1.5 MB flash dump); the debounced save is
  FORCED before any reboot/power-off (a setting acknowledged with a 200 was
  thrown away); the launcher's self-heal RELEASES its pauses (`pause()` is
  refcounted, `isPaused()` returns the ACK — the screen used to stay frozen
  forever).
- **Boot lobby for guest bins** (`SceGuest::applyLobbyTheme`): a 2.5 s window
  BEFORE the guest's `setup()` — `[Companion]` / `[Continuer]` (continue) /
  countdown — in the launcher's theme. It is the only exit that depends
  neither on the guest's code nor on the network: a `.bin` that crashes,
  loops or breaks touch stays recoverable without USB. On CoreS3 the library
  takes the TOUCH path, which ignores the button drawing callback — so the
  wait screen is replaced wholesale (`setWaitForActionCb`). No "save" button:
  the action is inert inside a guest, and saving would overwrite
  `/companion.bin`, the one and only safety net.
- **Web configuration for guest bins** (`SceGuest`): a guest DECLARES its
  settings (`addSetting` — Num/Bool/Text/Choice/**Secret**) and provides two
  accessors; SceGuest renders the form in the console's theme at `/config`,
  applies it and renders a verdict. HTTP Basic auth is **taken from the
  companion's console** (the `api:` section of the same `config.yaml` — one
  password for the whole robot; `setAuth` to impose something else), escaped
  values, hidden field guarding against an empty POST, explicit `value='1'`
  on checkboxes (the browser sends `on`, which a naive `toInt()` turns
  into 0). The app keeps ownership of its storage.
  **Anti-CSRF token drawn at boot** into the hidden field (a public constant
  could be reproduced in a cross-origin POST: by rewriting `host`, a
  third-party page made the robot send the home-automation token to a server
  of its choosing, without even knowing the token). `onSettingsBegin`
  declared **without** `onSettingsSaved` is refused — the lock taken by the
  former has only one release point, and the incomplete pair froze the robot.
  A **`Secret`** setting (API token, password) is **never redisplayed**: the
  field comes back empty, submitting it empty means "unchanged", and a lone
  dash (`-`) **revokes** the value.
  A `Text` setting redisplays its value in the HTML attribute — acceptable
  for a radius in nautical miles, not for a Home Assistant token, which opens
  your entire installation to anyone who loads the page. If a `Secret` is
  declared while the page has no password, the page says so at the top rather
  than locking itself: it is also the only convenient path for entering the
  secret in the first place.
- **Claude Code bridge** (`scripts/dev/claude-statusline.ps1`): the robot lives
  Claude's activity (gauges, reactions) **without Claude Desktop and without
  BLE** — just WiFi + the API. Generic pusher: `scripts/dev/statusbar-push.ps1`.

## API, console, network

- **Embedded web console** (`/`, zero CDN, French-language UI, "Liquid Glass"
  design with a consistent radius/colour system): header with **status chips**
  + heap/frame graph; an **always-visible control area** (emotions, dances,
  head); panels grouped logically (Status band + Options side by side with
  linked expand/collapse, Camera, unified **Fichiers · carte SD** (files · SD
  card), Fine tuning, System = network/VOR/security/OTA/power); band as a
  *segmented control* + icon *pills*; servo remote, VOR calibration;
  **Swagger** (`/swagger`) + OpenAPI; **mDNS** + AP **captive portal**.
- **Basic Auth protection** (`config.yaml`, `api:` section): an empty password
  = open API (the default), a non-empty one protects the console and `/api/*`
  GLOBALLY through `AsyncAuthenticationMiddleware`, changeable at runtime via
  `POST /api/security`, persisted to SD. **Uploads (OTA/bins/CSV) check auth
  THEMSELVES** at the top of the callback (the middleware only runs after the
  body).
- **OTA** `POST /api/update` (multipart) + **`/api/reboot`** +
  **`/api/poweroff`** (deferred, HTTP response sent before the action;
  poweroff is one-shot — no loop while on USB); **[Bins] API + Launcher**
  (SD `/bins/` menu); `guest/SceGuest.h`.
  **Launcher UI redone** (aligned with the console: rounded cards,
  cyan/indigo accent, filled/outlined buttons) + a **dedicated flash screen**:
  the "…" dots ON TOP, title + binary name BELOW, a real progress bar + %,
  SD-Updater messages relegated to a bottom strip — the library's default UIs
  (DisplayUpdateUI/DisplayErrorUI) used to write over the dots; ALL callbacks
  are replaced (flash + `SauvFW`).
- **Hardening from the max review 07-26**: `sd/put` uploads are ATOMIC for ALL
  paths (`.tmp` + validation of bytes written + rename — an interrupted upload
  no longer truncates config.yaml; `/companion.bin`: the `.old` dance, so the
  old safety net survives a failed rename; `.tmp` files purged at boot); SD
  file names JSON-escaped in ALL emitters (sd/list, bins, dances); EXCLUSIVE
  launcher mode (Brain suspended, servo torque released, LEDs off, camera
  inhibited, screen-pause self-heal); a flash failure returns to the face (no
  more defensive reboot); a long touch contact ≥ 700 ms is swallowed (a
  resting thumb ≠ a wink); guest radar: airport recentring by CAS +
  generations (no more phantom recentring), a single SD writer (loop), route
  cache, right panel inert to taps, tracking preserved outside the dial;
  SceGuest: `onBeforeStop` hook (suspends the app's tasks before a reflash) +
  BtnA `checkSDUpdater` lobby in the demo bin.
- **SD import / export** (`GET /api/sd/list` · `GET /api/sd/get` ·
  `POST /api/sd/put` · `DELETE /api/sd/delete`): download/replace/delete
  `config.yaml`, `rules.txt`, choreographies (`/dances/*.csv`) and binaries
  (`/bins/*.bin`). Paths are **whitelisted** (anti-traversal); importing
  config/rules triggers an **automatic reload**. Console panel *"Import /
  Export"* (download links + import + delete). ⚠ `config.yaml` may contain
  the WiFi password (protect the API with Basic Auth).
- **`GET /api/sensors`**: telemetry for the extra sensors — INA226 gauge (bus
  and shunt voltage), LTR-553 ambient light (`light_pct` + `light_raw`),
  BMM150 magnetic heading.
- **Network latency under control** (measured: commands ~40-60 ms even with
  the stream running): WiFi modem-sleep OFF + max TX (power save was adding
  100-300 ms per request), console commands **parallelised** (3 in flight)
  with self-healing timeouts, camera view and status poll that **yield the
  radio** to commands, no duplicated request (no refresh after every POST),
  serialised uploads (only one active, auto recovery after >10 s).

## Extra K151 sensors

- **Battery** (AXP2101 PMIC, `Board::batteryPercent/isCharging/vbusPresent`)
  → `/api/status` + console cell (red if ≤15 % and not charging) + status band
  "BATTERIE FAIBLE" (low battery).
- **INA226 gauge** (I2C 0x41, Wire1): bus voltage (battery) and shunt
  (∝ current) exposed as telemetry in `/api/sensors`.
- **BMM150 magnetometer** (via `M5.Imu`, 9-axis): magnetic heading 0–360° in
  telemetry (a drift-free yaw reference; VOR fusion still to be calibrated on
  hardware).
- **LTR-553 ambient light** (I2C 0x23, **96x gain + 400 ms integration** — the
  sensor is nearly occluded by the K151 case, calibrated against HW
  measurements: lit desk ≈ 50 %): **automatic screen brightness** via the
  `auto_brightness` option (read every ~2 s, applied by the renderer on change
  only — never `setBrightness` per frame; read-failure guard: the last valid
  value is kept); **`dark_sleepy`** (ON by default): NIGHT MODE through the
  ROULETTE — full darkness sustained for ~6 s → Sleepy replaces Normal in the
  draw (dominant weight ~66 %, the robot dozes with the occasional other
  emotion), reflexes/dances/API still take priority; light back → day mode +
  wake-up blink. Anti-flap hysteresis.
- **Night volume**: `sound_volume_night`, from REAL sunset to REAL sunrise
  (`sce::isNight` over the `lat`/`lon` tuning keys — the same solar code the
  radar bin uses, so the two binaries cannot disagree about what night is).
  The companion gained a wall clock in the process: **NTP** at boot in STA
  mode, and the **BM8563 RTC is now SET** from it once — the chip used to be
  read by this feature and written by nothing at all, which is why the old
  fixed 22h-6h window fired on an arbitrary hour.
- **Extended IMU**: software double-tap (peak in `|accel|`) → Happy + wink;
  face up/down orientation (`accel.z`) → held Sleepy.
- **Head towards the noise** (`sound_track` option, ES7210 stereo): ambient
  gate, adjustable threshold/direction, I2S1 bus arbitration with the chirps.
  **Muted during movement**: sound events are ignored during a servo
  trajectory + 350 ms (the mics pick up servo/gear noise — without this gate
  the head chased itself, and the false event delayed the return home).

## Robustness / security (hardening)

- **Quote-aware config.yaml**: `password: ""` means an empty string (open API)
  and not the two literal characters; credentials serialised between quotes
  (round-trip safe for `#`, spaces, empty); API inputs sanitised (`"`, `\`,
  control characters removed) → no JSON injection into `/api/status` and no
  YAML injection.
- **`/api/security` and `/api/wifi` are deferred**: credentials are staged and
  then applied by `loop()` — no more mutation of a shared `String` from an
  AsyncTCP callback concurrently with `SdConfig::save()`.
- **Schema migration** (`cfg_version`): an older yaml has its recalibrated
  keys discarded again at load time.
- **Concurrency & resource hardening**: renderer pause refcount under a
  spinlock; upload ownership under a spinlock (timestamp before publication,
  auto-heal outside the critical window); camera — deinit performs a full
  purge (`_initReq`, snapshot state), `_failed` un-latched by `camera=0`,
  constant 300 ms MJPEG re-serve (AsyncTCP credits), `waitCaptureIdle`
  rendezvous before any flash, unified JPEG pipeline streamed to PSRAM
  (`fmt2jpg_cb`, zero internal allocation); `?full=0` = live frame; LTR-553
  read-failure guard; console — generation token against late callbacks,
  snapshot without a false "failure", blobs revoked, toggles resynced.

## Tests / build

- **159 native tests** (16 suites), `scripts/gates/test-native.ps1`, FakeClock; the
  `engine/`+`behavior/` modules stay pure (Clock/Rng injected). The 13th suite
  is `test_sunclock` — solar position checked against published sunrise and
  sunset times, which is what lets the radar's night theme follow the real sun
  instead of a fixed clock window.
- **Endurance**: long run with no reboot, stable heap, healthy stacks;
  instrumented heartbeat + `scripts/dev/endurance-log.ps1`.
- PlatformIO envs: **`companion`** (firmware), `flight-radar` and `ha-remote`
  (guest bins, dropped onto the SD card — never flashed over USB, they would
  overwrite the companion), `flight-radar-fire` (the same radar source as a
  standalone M5Stack Fire application), `native` (PC tests).
  `build_unflags = -std=gnu++11`; the LCD bus is reconfigured to 40 MHz BEFORE
  `SD.begin()`.

## Shared code (design review 2026-07-29)

- **One single YAML parser** (`SceGuest::yamlForEach`) instead of four. The
  card's file format was re-implemented in `SdConfig.h`, in `SceGuest.h` and
  in every guest bin, with four different behaviours — and two failures came
  out of it: quotes kept inside the SSID (STA failing, silent AP fallback,
  07-25), then an `api: "adsb.fi"` that `flight-radar` no longer recognised,
  silently switching to another source. The parser lives in `SceGuest.h`
  because that header is the only one already embedded in all three binaries:
  sharing it there costs not one byte of flash. Whether the file has sections
  is **declared** by the caller, never guessed. On the companion side,
  `SdConfig` keeps an acknowledged and documented twin — `parseScalar` **and**
  the read loop of `load()`: making the companion depend on a `guest/` header,
  which is designed to be copied into third-party projects, would cost more
  than the duplication. The two loops must stay identical line for line
  (rule A2.23); they had already diverged — see the review below.
- **Shared, bounded PSRAM allocator** (`firmware/common/PsJson.h`). It was
  duplicated in both bins and, worse, fell back **silently** onto internal
  `malloc()` when PSRAM ran out — taking the very heap it existed to spare, at
  the worst possible moment, with a dead network stack much later as the only
  symptom and no visible link to the cause. From now on: a large block is
  REFUSED (a failed parse can be replayed, an exhausted internal heap cannot),
  small fallbacks are capped per document, and any PSRAM exhaustion leaves a
  serial trace.
- **A2.19 route ordering verified at startup.** Every route goes through
  `WebApi::route()`; `checkRouteOrder()` re-reads the list before
  `_server.begin()` and calls out any badly ordered pair. The criterion is the
  router's own (`^{uri}(/.*)?$`) and not an approximation — otherwise the guard
  would have screamed about `/swagger` and `/api/openapi.json`, which work
  fine. The rule has already made three endpoints unreachable; until now it
  rested on nothing but the position of 38 calls inside 1400 lines.
- **`flight-radar`: one single HTTPS+JSON path** (`fetchJson`). The four calls
  each repeated the TLS open, the timeout, the GET, the parse and the `end()`
  — and they had diverged: three of them parsed into the INTERNAL heap instead
  of PSRAM. Net result: −1.6 KB of flash.

## flight-radar: dock mode and phase of flight (2026-08-04)

Three ideas borrowed from the Aerospace Tracker interaction guide (roadmap
review of the same day), the third redesigned by the user before it was an
hour old:

- **The 6-to-12 panel** (user design): the flight panel's own separator
  line BECOMES the route — DEPARTURE at the bottom, ARRIVAL at the top,
  you fly from 6 o'clock to 12; the travelled segment is solid up to the
  aircraft triangle, the remainder dotted; the blocks swapped to match,
  each distance figure sits on its airport's side, and the row the old
  horizontal bar occupied now carries the trip rows. No complete route =
  an all-dotted rail with no aircraft. Trip rows (user 08-04): `%` and
  remaining distance share a row, the phase code carries its readable
  label (`CRZ cruise`), and the remaining time reads `ETA 0h28` —
  language-free instead of the translated `left`/`reste` label.

- **Dock mode**: parked on a desk, the views cycle by themselves — `dock_s`
  seconds per view (`/config`, 0-120, 0 = off by default). Every manual
  action restarts the clock at its producer; the touch modals suspend the
  cycle by blocking the loop; the advance goes through `applyEvent()` as a
  third input vocabulary after touch and buttons.
- **Phase of flight**: a `CLB`/`CRZ`/`DES`/`GND` chip on the tracked
  panel, aviation abbreviations, same thresholds as the reversed-leg
  corrector — the phase existed in the maths and was invisible on screen.

## Timer and pomodoro in the status band (2026-08-04)

Two interactive band modes (user request): **4 = countdown timer**, **5 =
pomodoro**. The state machines are one pure module (`engine/BandTimer.h`,
Clock-injected, `test_bandtimer` 8 native cases); loop() owns the instance,
publishes its display through the FieldStore blackboard (`tmr`/`tmr_st`) and
turns its events into emotions through the CommandQueue — the renderer only
maps a state to a colour and hands the composed text to `drawDynText`, whose
existing change detection even provides the ring's blink for free (loop
alternates the text at 2 Hz).

- **Timer**: vertical swipe on the band's left half = hours ±1 (wraps 0↔23),
  right half = minutes ±1 (0↔59) — while idle or paused only, a running
  countdown is deliberately not editable. Band tap = start / pause (the
  remainder freezes, no wall-clock re-anchor) / resume / acknowledge. At
  zero: `00:00` flashes red and the robot fires **Excited** — the star-eyed
  alarm face — for 10 s, until acknowledged.
- **Pomodoro**: `cycle/total MM:SS`; work → **Focused**, break → **Happy**,
  end of the last work block → **Glee** (no trailing break). Lengths are
  tuning keys (`pomo_work_min`/`pomo_break_min`/`pomo_cycles`, clamped in
  the machine, sliders in the console).
- The band-mode ring becomes `0→2→3→4→5`, `/api/statusbar` and the console
  segmented control follow; a new `TouchGestures::onTapBand` callback keeps
  band taps and eye taps two separate vocabularies. Details: STATUSBAR §2b.
- **Revised the same day** (user: "the swipes are hard to use"): the timer
  setting adopted the ALARM-CLOCK tap pattern — while editable, the band is
  a keypad (left third = hours, right third = minutes, tap above the digits
  = +1, below = −1, centre = start/pause), with small `+`/`−` affordance
  rows drawn around the digits (which shrank one size to make room). The
  swipes still work; while running or ringing the WHOLE band stays the
  primary action — an alarm must stop on the first tap, not a well-aimed
  one.
- **`band_clock` option** (user 08-04): mode 0 (None) can carry the wall
  clock instead of black — discreet gray HH:MM through the same
  blackboard pipe, empty until NTP has spoken (never 1970). The robot only
  knows UTC (the night is sun-driven by design), so the display gets its
  own `tz_offset_h` (console slider, ±14 h in quarter-hour steps — decimal hours, the same key and unit the guest bins use) — display-only.

## Magnetometer/VOR fusion, and the neck hold done right (2026-08-04)

> **HW verdict — impractical on the K151, reached the honest way: wrong
> twice first, and the record keeps all three acts.** Session 1 (897
> samples): norm 296-589 µT vs Earth's ~50, heading pinned ±3° through hand
> rotations — closed as impractical, blaming servo currents. Session 2
> seemed to REVERSE that: servo regimes identical, an averaged head-yaw
> sweep recovering an Earth-sized signal — **but it ran against `servos=0`,
> where the servo task writes nothing: the whole choreography was inert,
> the sweep never moved, its 50.9 µT "signal" was drift. The user caught
> it.** Session 3, servos genuinely on, alternating sweep + drift control:
> the head-yaw dependence is 370 µT per 80° — six times Earth, the BODY's
> servo magnets seen through a steep gradient — with ±100-175 µT of
> irreproducibility at identical commanded pose and ~50 µT torque-state
> steps. A pose-map calibration dies on that irreproducibility. Only path
> left: an external magnetometer away from the motors (Grove). The fusion
> code and its three native tests stay; `vor_mag_alpha` stays 0.

- **The BMM150 heading now corrects the VOR's yaw drift** — the observable
  the accelerometer does not have: gravity says nothing about rotation
  around itself, so yaw drift was only ever caught by the quiet-gated pull
  and the catch-up saccades. A complementary filter anchors a heading
  reference and pulls `offset.x` toward `target − (heading − ref)·K` DURING
  MOTION — exactly where the accelerometer lies — while the quiet regime
  stays owned by the accel corrector (which re-anchors the reference).
  Gated off during self-motion (servo currents distort the field being
  read), re-anchored on saccades and reading gaps. **`vor_mag_alpha`
  defaults to 0 (off)**: the heading is not tilt-compensated and its sign
  against the gyro yaw is unvalidated on hardware — with the wrong sign the
  loop doubles the drift instead of removing it. The calibration recipe is
  in CONFIG (`telemetry=1`, turn the robot by hand, check heading and gyro
  yaw agree, then 0.02-0.10). Three native tests pin the physics: a fake
  rotation (gyro says turn, heading says still) is corrected with zero
  parasitic saccades, a TRUE rotation is not fought, an absent heading is
  inert.
- **The wake's neck hold is the Brain's own bit** (`setHeadFollowHold`),
  no longer a zeroed-then-restored `tuning.head_follow`: the restore used
  to silently overwrite a `POST /api/tuning?head_follow` landing inside
  the wake window. The user's setting is never touched; both debts of the
  08-04 review row are settled.

## Eight-angle review — defects found and fixed (2026-08-04)

Eight parallel review angles over the 08-03 batch (line-by-line, removed
behaviours, cross-file tracing, reuse, simplification, efficiency, altitude,
conventions), then fixes. The heavy ones:

- **The boot bang survived on every RETURN path.** The wake sequence fixed the
  power-on, but dismissing the touch launcher and a refused/failed API flash
  both re-engaged torque on the pre-handover COMMANDED pose while the head had
  sagged for minutes — and the three API-launch refusal branches re-engaged
  NOTHING: the neck stayed limp for the session (`releaseTorque()` sets no
  `_autoReleased`, so the auto re-engage can never fire for a deliberate
  release). New `ServoMotion::rebaseAndEngage()`: the motion task — the bus's
  only owner — re-MEASURES the pose, re-aims any in-flight move from the true
  origin, then honours the torque request; every return path calls it.
- **`homeSlowly()` never actually ramped**: it stored targets without bumping
  the sequence counter the task watches. Now through `moveTo()`; and when the
  wake measurement fails three times, the ramp is delegated to the SERVO
  (WritePos-with-time interpolates from its real position — the one thing the
  software ramp cannot know blind). `isMoving()` reports the servo-side ramp,
  and the wake waits on that signal instead of restating the duration.
- **The first-frame gate moved to where it cannot lose**: `TripleBuffer`
  gained `hasEverPublished()` and the renderer draws NOTHING until the first
  real publish — the 60 ms head start only made the default-Normal flash
  unlikely; scheduling jitter could still spend the instant-apply shot on a
  face nobody produced.
- **Failover now serves the WHOLE poll cycle.** The satellite tiles and the
  worldwide-track request kept hammering the dead preferred server after the
  centre fetch had fallen back — up to 7 extra timeouts per cycle and an outer
  ring that silently emptied under a banner naming the living source. One
  `serving` decision per cycle, honoured everywhere; tiling keys on the source
  that ANSWERED (a SafeSky→v2 fallback now gets its >250 nm tiles). SafeSky
  auth failures (401/403, bad key) no longer fail over silently: a working
  radar would hide the misconfiguration forever.
- **Renderer resume() published its wipe flag after releasing the pause**: the
  renderer could wake, pass its consumption point, and miss the invalidation —
  menu leftovers frozen under the eyes until the next pause, minutes away. The
  two real flags are now set under the pause lock, and the relay flag is gone.
- **NOTAM: blank lines live again** (the CRLF collapse ate every consecutive
  newline, gluing the blocks the wrapper had just learned to keep apart);
  the expiry countdown is measured against item D's schedule instead of
  overprinting it at a fixed x, on exactly the urgent notices.
- **ButtonFsm: a release sampled inside its debounce window no longer fires
  the long action it was released to avoid** (the raw level now guards the
  threshold check; native test added).
- **SD hot-swap hardened**: no probe/remount under an active upload
  (`SD.end()` beneath an open `_uploadFile` is FAT corruption); a replaced
  card reloads the RULES and cancels the pending tuning autosave (it was
  about to overwrite the fresh card's config.yaml with the old card's); both
  bins back off 10→60 s while NO card has ever been seen.
- Smaller: gauge rows quantise what they DRAW so the change detection's
  invariant is true (bars could sit ~1.6 px stale); one call site for the
  gauge row (A2.22 shape); shared `/bins/` limits for the touch launcher and
  the API cache (24/40 vs 48/50 behaved differently per entry point); the
  radar's debug overlay redraws at 250 ms instead of ~100 Hz; `upAscii()` and
  `zuluHhmm()` replace five and two hand-rolled copies that had already
  diverged; the sRGB threshold in test_emocolor aligned on
  check-contrast.py's 0.04045; French comments translated; the `/api/sd/get`
  A2.6 exception recorded in the rule's audit trail (ROADMAP §A2.6).

Declined with reasons, recorded rather than silently dropped: drawBlush's
half-pixel double stamping stays (it is the 08-03 fix for a user-visible cut,
cost bounded and documented); the SD listing keeps its String building (the
paused window is dominated by the FAT walk, and rewriting the JSON escaping
replays the 07-26 jesc lesson); intensify stays separate from lerpRgb888
(different rounding, LSB-tuned gates).

## Max review — defects found and fixed (2026-07-29)

A multi-agent review of the batch above (six independent angles, each finding
subjected to an adversarial rebuttal before being retained).

- **Unbounded SD file reads** (`SceGuest::yamlForEach`, `SdConfig::load`,
  `DanceStore::parseCsv`). The 512-byte ceiling was tested AFTER a
  `readStringUntil`, hence after the String had already grown: it protected
  nothing that its own comment claimed. A corrupted file — or a `.bin` renamed
  to `.yaml`/`.csv`, without a single `'\n'` in megabytes — exhausted the heap
  BEFORE the test, and these files arrive by API upload. From now on:
  `readBytesUntil` into a stack buffer, and the remainder of an over-long line
  is THROWN AWAY up to the newline (otherwise the leftover came back as a bogus
  key/value pair). `SdConfig` did not even have a ceiling: the twins had
  already diverged.
- **`PsJson`: the refusal was not safe where it was applied.** `reallocate`
  refused any block over 4 KB — including a SHRINK. Yet ArduinoJson holds a
  shrinking realloc to be infallible (`ARDUINOJSON_ASSERT`, erased in release)
  and has already freed the block by the time the allocator returns
  `nullptr`: so we crashed in exactly the case we claimed to degrade
  gracefully. The refusal is now reserved for `allocate()`; whatever
  `reallocate` concedes is counted and traced. Along the way the cumulative
  budget, which it never consulted, is actually enforced.
- **The A2.19 guard was blind to combined masks.** It compared methods with
  `==` while the router decides by intersection: a route declared
  `HTTP_GET | HTTP_POST` made it mute on that path — precisely the case where
  ordering matters most. It also calls out path+method duplicates, whose
  second handler is dead without a word.
- **`flight-radar`: a timeout that did not exist.** `tls.setTimeout(ms)`
  expects SECONDS (we were passing it 6000 s), touches nothing until the
  socket is open — which is the case before `GET()` — and its effect was
  overwritten right afterwards by `HTTPClient::connect()`. The line was inert
  and the comment promised a read bound that was never armed. The only useful
  setting is `http.setTimeout()`, which carries the budget over to the socket.
  The local failure codes go from −1/−2 to −1000/−1001/−1002: they used to
  show up on screen under the same number as
  `HTTPC_ERROR_CONNECTION_REFUSED`, the only trace available without a probe.
- **Special renders and gaze** (`EyeRig`). The `Excited` star applied `MoveY`
  with the sign inverted relative to the rest of the face: it went DOWN when
  the gaze went up. The `Dead` cross ignored the gaze entirely, the only
  render left nailed to the centre while the eyes next to it moved. To be
  re-validated on target (`docs/validation/VALIDATION.md`).

## Screen frozen during uploads (2026-07-30)

- **The "…" waiting screen no longer animates.** The three squares each
  breathed on their own period — it looked nice, and that was the problem:
  animating forces the renderer back onto SPI2 on every frame, and during an
  upload those frames fight the SD card for the bus (A2.16). The indicator
  meant to hide the stutter was producing it. Motionless, it is drawn **once**
  (with the status band frozen alongside it) and then the bus is released.
- **So every upload freezes the screen**, not just OTA: the freeze is armed in
  `WebApi::uploadOwns` and lifted in `uploadRelease`, the single entry/exit
  point for all four routes (bins, sd/put, dances, OTA). `update()`'s
  self-healing lifts it too — without which a connection dropped mid-upload
  left the robot frozen until the next reboot. A successful OTA, on the other
  hand, keeps the freeze until the restart.
- Corollary: `setBusy` becomes usable during an SD write, which its own
  documentation ruled out until now. `pause()` keeps its own role — it alone
  waits for the renderer's acknowledgement, so it alone allows the screen to
  be HANDED OVER to a third party (Launcher, SD-Updater).
- The launcher now lays the dots down once in `drawFlashScreen` instead of
  repainting them on every progress callback, where they slotted in between
  the flash's SD reads for an identical result.

## PC tools (`tools/`, `scripts/`)

- **Choreography editor** (`tools/choregraphies/`, HTML+JS+CSS with no
  dependency and no server — a double-click is enough): a keyframe timeline
  (expression, yaw, pitch, `gazeY`, eyelids, `servoMs`, `holdMs`), an **eye
  simulator** that replays the firmware's own chain
  (`EyeRig::setEmotion` → `mirrored` → `eyegeom::normalize` →
  `EyeDrawer::Draw`) on the **real presets**, a head orientation cube,
  playback at real durations, import/export of the CSV format used
  by `/dances/`. The input bounds are the robot's own (yaw ±130°,
  pitch −74..+6°, `gazeY` saturated at ±0.20, 23 keyframes).
- `tools/choregraphies/extract-presets.py` **generates** `presets.js` from
  `src/engine/presets/*.h`, `Emotions.h` (names **and** `emotionToRgb`),
  `EyeRig.h` (left/right/`lidCenter` mapping) and `Tuning.h`
  (`eye_color_dim`) — copying that data by hand would have made it diverge
  from the firmware at the very first tweak. Re-run it after any change to a
  preset, a colour or the mapping.
- **Panel rendered without resampling** (review 07-29). The canvas drew at
  320×160 and was then scaled down to ~160 px by the browser, with
  `image-rendering:pixelated` — that is, nearest neighbour. Since the factor
  is not an integer (0.4988), the sampling grid drifted along the image: the
  two arms of the `Dead` cross advance in x in OPPOSITE directions, so they
  accumulated opposite phases and one of the two ended up shaved down to a
  one-pixel light line — even though the canvas content itself was perfectly
  symmetric (26 stamps per arm, same radius, same colour). The buffer is now
  sized to the DISPLAYED size: nothing left to resample, at any panel size,
  and the preview becomes sharp on a HiDPI screen (where it was being
  downscaled TWICE).
- **Layout heights** (user 07-30): the top row sizes itself on the LARGER of
  Keyframe and Preview — on their real content — instead of an arbitrary
  `72vh` ceiling, too tall on a big screen and too short on a small one. The
  timeline no longer takes part in that computation: its list has a zero flex
  basis, so it adapts to the height found and scrolls instead of imposing that
  height with its seventeen keyframes. The CSV area takes all the rest.
- **Seam in the `Excited` star** (user report 07-30): its two triangles shared
  the `y = cy` edge exactly, and canvas antialiasing makes each of them cover
  half of that pixel row — two translucent halves do not recombine into a
  solid pixel, so a dark hairline ran across the star. The top triangle now
  lowers its base by one pixel: the two overlap and the seam disappears. The
  firmware does not need this, since `fillTriangle` there draws solid pixels.
- Other editor defects fixed by the same review: any input STOPS playback
  (otherwise the sliders wrote into the keyframe currently playing, and the
  result was overwritten on the next frame); the "last keyframe" warning tests
  the LITERAL column just like `DanceStore`, so it finally sees the most
  common case — a last line with no emotion, for which the robot appends an
  exit keyframe; the eyelid event has its own duration (`BlinkController`
  envelopes) instead of a fraction of `servoMs`, so that a blink with no servo
  travel is at last visible; dragging on the head no longer stays "stuck" when
  the gesture is cancelled (`releasePointerCapture` throws on `pointercancel`
  and used to skip the whole cleanup); the list is no longer rebuilt on every
  slider pixel nor on every keyframe played.
- `scripts/gates/check-contrast.py`: reads the `THEMES[]` table **out of the
  `flight-radar` source** and fails if a text/background pair drops below the
  WCAG AA threshold, computed on the **RGB565-quantised** colours actually
  displayed.

## The "space" guest bin (2026-08-04)

A third guest bin, `firmware/space/` — and the first that COMPUTES its subject
instead of receiving it. One TLE fetched at most once a day (Celestrak, cached
on the card) is enough to place the ISS to the kilometre and predict its passes
for 48 hours, on the microcontroller: cut the network and everything but the
launch list keeps working.

- **Five views on one ring**: ISS world map (day/night terminator computed per
  column, ground track, sunlit/eclipse), PASSES (with a `VISIBLE` chip that
  means sunlit satellite AND dark sky — two different events), MOON (real
  terminator ellipse, rise/set, next new/full), SKY (Moon + 5 naked-eye
  planets, three-valued state chip so nothing reads "up" at noon), LAUNCHES
  (T- ticking locally between polls, over **two sources with failover**).
- **The correctness lives in two PURE headers with native tests**: `sgp4.h`
  (TLE + near-Earth propagator) is pinned against the CANONICAL Spacetrack
  Report No. 3 verification vector to within 1 km; `astro.h` (Sun, Moon,
  planets, geodesy, look angles, Earth shadow, pass search) against real dated
  events. 27 cases; the gate rose to 23 suites / 266 tests and builds FIVE
  firmwares.
- **Two launch sources, and the failover is a design choice, not a safety
  net**: RocketLaunch.Live is primary — no key, no published limit, and every
  datum in a field of its own, so the vehicle the silhouette is chosen from is
  never recovered by splitting a display string; it alone publishes **pad
  weather**, now shown beside the date. Launch Library 2 stays as the fallback
  and keeps the poll floor at its published 15 requests/hour. The two answers
  are told apart by SHAPE (`results` vs `result`), which is what lets the SD
  cache remain a verbatim copy of whichever body worked. RocketLaunch.Live's
  terms ask for a visible credit: the view carries it, and it names whichever
  source actually served.
- **Three API etiquettes encoded as code, not as intentions**: Celestrak at
  most daily behind an SD cache, the launch poll floored at the slowest
  source's throttle, and every view gated on a synchronised clock.
- **What it refuses to do**: a deep-space `norad` is rejected rather than
  approximated (SGP4 alone is not valid there), a TLE older than 14 days greys
  the view out with its age, and a launch cache older than an hour says "as of
  hh:mm" instead of presenting a slipped T-0 as live.
- Spec: `docs/ROADMAP.md §5`. Guide: `docs/guests/SPACE.md`.
