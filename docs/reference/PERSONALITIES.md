> **English** · [Français](PERSONALITIES.fr.md)

# Personalities — which character the robot is

A personality is not a theme. It is a set of dispositions: **which rules the
robot obeys, which expressions it rests in, and what colour it is.** Switching
one changes how the robot behaves when nothing in particular is happening,
which is most of the time.

```
POST /api/tuning?personality=1      # 0 = default, 1 = Haro
```

| | `0` — default | `1` — Haro |
|---|---|---|
| rules | `/stackchan-companion/rules.txt` | `/stackchan-companion/rules.haro.txt` |
| resting expressions | the full 17-weight table | six, all cheerful |
| draw interval | 6–12 s | 8–20 s |
| colour | the emotion palette | one green (`#22FF66`) |

## The boundary — what a personality may and may not touch

> A personality may change **what the robot feels and how long it shows it**.
> It may never change **what the hardware tolerates**.

It owns its rule file, its roulette (weights and cadence), its identity colour,
and two FEEL dials — how fast it moves between moods and how much its posture
leans into one. It owns **nothing else**: switching never rewrites your other settings.
Brightness, servo options, sound, thresholds — all survive a change of
character in both directions, so there is never a question of which layer last
wrote a value.

Servo end stops, the frame budget, bus timings and the touch vocabulary are
therefore absent from the personality table by construction. `test_personalities`
asserts it: a "personality" able to park a servo against its stop would be a
damage mechanism wearing a costume.

## The default personality is not "the absence of a costume"

It is a character in its own right, and it has an index because of that. The key
is `personality` rather than a `haro_mode` boolean for one concrete reason: a
boolean would have to be renamed the day a third character appears, and on this
project renaming a key *is* a migration — the old value comes back silently
through the config rewrite. The name is paid for now, while it costs nothing.

Personality 0 also declines to declare any weights. It does not restate the
historical table, it leaves `EmotionRoulette`'s own alone — which is what makes
"a card with no personality data behaves exactly as before" true by
construction rather than by careful copying. That equivalence is the acceptance
criterion of the whole feature and it is tested.

**The index is not stable across a reload — the active character's NAME is.**
`PersonalityStore` rebuilds the table from the card's directory order on every
reload, so creating or deleting a character can shift what index an unrelated
one now sits at. The console's create/edit/delete flow carries the active
character's name across that reload and re-resolves its new index before
re-applying it, so switching characters through `/api/tuning?personality=`
between two writes is the only way to land on the wrong one — a
create/edit/delete from the console itself always keeps the character that
was actually running.

## The random moods switch

```
POST /api/tuning?roulette=0
```

With the roulette on, the robot draws an emotion every few seconds. That is what
makes it feel alive when nothing is happening — and it is also what stops any
expression from **meaning** something, because the next draw overwrites whatever
a rule just said. Turn it off and only rules and reflexes speak: a Worried face
can then only have come from something that happened.

**It is not a freeze.** Timed emotions still expire back to rest, and blinking,
glancing around, breathing and the gyroscopic gaze are driven elsewhere and keep
running. What stops is the random *choosing*, nothing else.

A narrow personality achieves the same thing more gently. Haro rests in six
cheerful expressions and never draws anything dark, so a Worried face is still
informative without the switch.

## Two feel dials, not a hundred per-emotion values

```
POST /api/tuning?transition_scale=1.6&pitch_bias_scale=0.4
```

`transitionFor()` and `pitchBiasFor()` hold roughly a hundred hand-tuned
numbers — a duration and a head-tilt for every one of the 30 emotions. Making
each of those individually editable per character would be a huge surface for
very little feel, so a personality gets two dials over the whole table instead
of a hundred keys into it:

| Dial | Range | 1.0 means | 0 means |
|---|---|---|---|
| `transition_scale` | 0.3 – 3.0 | unscaled — the compiled durations, unchanged | *(not reachable — 0.3 is the floor: instant transitions would look broken, not snappy)* |
| `pitch_bias_scale` | 0.0 – 2.0 | unscaled — the compiled head-tilt per emotion | stoic — the head never leans with a mood, however it moved to get there |

Both stay **scales**, never replacement values: they multiply what the
compiled table already says, so a snappier character is still recognisably the
same character, only paced differently. The clamp lives once, in
`Personalities.h`, as a pure function — `Brain` itself cannot be natively
tested (its constructor opens a FreeRTOS queue and pins a task), so the bound
that keeps a transition inside 40–3000 ms has to be provable somewhere a PC
binary can reach it, and `test_personality_feel` sweeps it, including NaN and
infinity, rather than trusting a few sample points.

## Writing a personality's rules

The format is the one in [PLUGINS.md](PLUGINS.md), with two traps worth
restating because a rule file has to be readable on its own.

**An absent field reads as zero.** `RuleEngine` reads `getF(field, 0.0f)` with no
existence test, so any `lt`/`le` rule with a threshold above 0 is **true while
the field does not exist**. On an optional field (`build`, `ctx`, anything a PC
script publishes), compare against a non-zero code with `eq`:

```
| build | eq | 2 | 0 | 10000 | PlayDance | happy     # 2 = success
```

**The ceiling is silent.** The engine holds 24 rules, 3 of them built in, and
refuses further ones without a word. Each personality gets the remaining 21 to
itself — only one file is ever loaded, which is exactly why the rules need no
personality gate: **the file is the gate.**

Two fields catch people out. `night` is not solar dusk (that one lives only in
`/api/sensors`) — it is the light sensor's night mode, and while it is on the
roulette already makes Sleepy dominant. And `tmr_st` is a **colour code**, not an
ordered state: `Idle=0 Run=1 Paused=2 Ring=3 Work=1 Break=4 Hydrate=4 Done=4`.
For phases use `tmr_ph`, which is the raw enum (`Idle=0 Run=1 Paused=2 Ring=3
Work=4 Break=5 Done=6 Hydrate=7`).

## Dances

All choreographies live together in `/dances/`, shared by every personality; the
rule file decides which ones a character uses. The four Haro dances ship with
the card:

| File | What it does |
|---|---|
| `haro_float.csv` | a damped drift, head rising and settling, no dwell between steps |
| `haro_call.csv` | the "Haro! Haro!" wiggle, with winks |
| `haro_scan.csv` | sweeps left and right, holds at each extreme, then startles |
| `haro_roll.csv` | a simulated roll — yaw and pitch combined, gaze following |

A rule plays them by name like any other: `PlayDance` resolves against the
merged list — compiled dances first, then the card — when the rule FIRES, not
when the file is parsed. That ordering is forced rather than chosen: at boot the
rules are read before `danceStore.reload()`, so the bank is empty during parsing,
and `DanceStore` reloads hot, so a stored index would point at a different dance
after an upload. See [PLUGINS.md](PLUGINS.md).

`haro_float.csv` is worth reading as an example of one trap: `holdMs` is the
keyframe's **total** duration, not an addition. Writing `servo 300 / hold 600`
means *move for 300 ms, then sit still for 300 ms* — half the dance motionless,
which reads as a series of nods rather than a float. Setting `hold == servo`
gives continuous motion.

## The Characters tab

Everything below can be done from the console — `http://<ip>/#perso`. It edits a
COPY and only writes on **Save**, so a half-dragged slider never reaches the
card.

**Try it now** is the honest exception: it writes, applies and says so. There is
no preview that keeps a character in RAM only — one that vanished on the next
reload would look like a bug, and a colour and a cadence are not things you can
judge from a form.

**Duplicate** unlocks the name field and writes nothing until you press Save, so
a duplicate you thought better of leaves nothing behind. **Delete** is greyed
rather than merely refused when a character cannot go — the robot says no either
way, and a button that looks live and then scolds you is a worse way to learn a
rule than one that was never offered.

**The console wears the active character's skin, and follows the room it is
in.** `theme:` is stamped on the page root; the browser's own
`prefers-color-scheme` picks the DARK or LIGHT face of that theme, with no
console toggle to find — the robot has no opinion on what room you are in, and
asking the system once is more honest than adding a switch nobody notices. A
theme MAY reshape its own background, ink and glass, not only its accents,
because a page that only swapped a link colour under a personality's name
would not actually look like a different console. What does not move is the
GUARANTEE: every surface a theme declares, in every mode it declares, still
clears the same WCAG floor the page always has — `check-contrast.py` measures
each (theme, mode) pair against the panel it is actually drawn on, and an
unmeasured mode is the one failure you cannot fix from the page.

## Characters that live on the card

Drop a file in `/stackchan-companion/personalities/` and the robot has a new
character at the next boot — no recompiling:

```yaml
# /stackchan-companion/personalities/zaku.yaml
name: zaku
rules: /stackchan-companion/rules.txt   # may reuse another character's rules
color: 0xFF4422
theme: gundam                      # a COMPILED skin, picked by name
roulette:
  enabled: 1
  min_ms: 5000
  max_ms: 11000
transition_scale: 0.7              # snappier than the compiled default (1.0)
pitch_bias_scale: 1.3              # leans into a mood a bit more than usual
weights:                           # any emotion left out is never drawn
  Normal: 1.0
  Focused: 0.5
  Suspicious: 0.3
  Annoyed: 0.2
```

**The file name is the id, and it decides edit versus create.** `haro.yaml`
matches the compiled Haro and overlays onto it — that is how a change to a
character the firmware ships with survives a reboot. A name nobody holds takes
the next free slot and becomes a new character.

**It states differences, not a declaration.** Every key is optional and an
absent one keeps the value underneath — the compiled character's when editing
one, the defaults when creating one. A half-written file therefore degrades to
"mostly the default" rather than to a character with holes in it. The single
exception is `weights:`: declaring the section at all *replaces* the table,
because a merge could never express "this character does not do Sad" — you could
add an emotion but never remove one.

**A key it does not understand is refused and named** on the serial line, never
skipped. Same for an unknown emotion or theme; the rest of the file still loads,
because one bad line is not a bad file.

**Themes are compiled, and picked by name.** A palette authored on the card
could not be contrast-checked at build time, and an unreadable console is the one
thing you cannot fix from the console. Today: `default` and `gundam`.

Eight characters fit at once (`MAX_PERSONALITIES`), the first two being the
compiled ones. Loading is idempotent — the table is reset to the firmware's own
characters before the directory is re-read — so a deleted file really removes its
character, and an edit is never applied twice.

**A 9th character is refused, out loud, not silently capped.** With the table
full, a file that would CREATE a new slot is skipped and logged
(`[perso] '<id>' ignoree : table pleine`, counted in the reload's problem
total); a file that EDITS a character already loaded still applies normally,
whatever position it comes in directory order — table-full only blocks new
creations, it never stops the rest of the directory from being read.

## Adding one in code

Two things make it exist: an entry in `src/behavior/Personalities.h` and a
rule file beside `rules.txt`. There is no enum to keep in step and no switch
to extend. The name and the rule path must be unique — `test_personalities`
checks both, because two characters sharing a rule file would reintroduce the
21-slot contention the per-file design exists to remove.

A third thing makes it *documented*: a page under
[`docs/personalities/`](../personalities/), in both languages, saying what the
character is like to live with — not enforced by any gate, so it is on the
author, not on `check-all`. See [`docs/personalities/README.md`](../personalities/README.md#adding-a-character).
