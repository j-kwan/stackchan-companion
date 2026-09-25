> **English** · [Français](HARO.fr.md)

# Haro — personality `1`

A Haro is a companion, not an instrument. That single idea is what separates this
character from the default robot, and it shows up in one concrete place: **the
default rules comment on the robot's own telemetry — battery, light, network,
camera — while Haro's rules watch the person.** The noise they make, the light
they switch on, the work they are doing.

```
POST /api/tuning?personality=1
```

## What changes when you switch

**It turns green.** One colour, replacing the emotion palette entirely. That is
the accepted trade rather than an oversight: a Haro is green, and that is most of
what makes it legible as one. What it costs is the palette telling you *which*
feeling is on the face — the shape still does.

**It rests in six cheerful expressions** instead of seventeen: Normal, Happy,
Glee, Curious, Surprised, Excited. Nothing dark is ever drawn at rest, because a
Haro does not sulk on its own. This is the part worth understanding, because it
is what gives the rules their weight: when Worried *does* appear, it can only
have come from something that happened.

**It settles down.** The draw interval goes from 6–12 s to 8–20 s, so it changes
its mind less often and holds a mood long enough for you to notice it.

## What it does

The rules live in `/stackchan-companion/rules.haro.txt` — 15 of the 21 available
slots, readable and editable from the console.

| When | It |
|---|---|
| a sharp noise | startles, then recovers quickly (2 s, not 15) |
| you talk to it | takes an interest |
| you keep talking | wiggles — the "Haro! Haro!" |
| you pet its head | wiggles back, delighted |
| you switch the light on | wakes up and bounces |
| the room goes dark | drifts off |
| a pomodoro work block | concentrates |
| the timer rings | wiggles again |
| a break starts | is delighted |
| the hydration prompt | asks a question |
| the battery drops below 12 % | worries |
| you plug it in | bounces |
| no network at all | shakes its head |
| a build succeeds / fails | rolls / is frustrated |

The last pair needs a PC script publishing the `build` field —
`POST /api/field?build=2` on success, `3` on failure. See
[PLUGINS.md](../reference/PLUGINS.md).

## The dances

Four choreographies ship on the card, in `/dances/`:

`haro_float` a damped drift, head rising and settling · `haro_call` the wiggle
with winks · `haro_scan` sweeps and holds at each extreme, then startles ·
`haro_roll` a simulated roll, gaze following.

The Haro rules play them by name — `PlayDance` resolves against the merged list
(compiled dances, then the card) when a rule fires. If one of the four is missing
from the card the rule **says so on the serial trace** instead of doing nothing
quietly. Check with `GET /api/dances/files`.

## Making it more of a Haro

Three settings are off by default and each is closer to the character than
anything in the rule file:

```
POST /api/tuning?sound_track=1&mic_enable=1    # the head turns toward whoever speaks
POST /api/tuning?leds=1                        # the body lights up in the eye colour
POST /api/tuning?sound=1                       # it chirps
```

`sound_track` is the one to try first. A head that turns toward the person
talking is the most Haro-like behaviour this machine has, it is fully
implemented and hardware-validated, and it costs nothing to switch on.

## What it still cannot do

Honest limits, so they are not mistaken for bugs:

**It does not know you are there.** There is no presence sensor. Sound, touch and
light over time are the only proxy, so it will sometimes react to an empty room
or miss you entirely.

**It does not float.** There is no vertical actuator. `haro_float` simulates the
motion with the neck; a continuous idle bob would need firmware, since a dance is
a one-shot and holds the floor while it plays.

**It is quieter than a real Haro.** The sound engine is event-driven by design —
*silence is rest* — and a Haro that repeats its own name constantly conflicts
with that principle. Which of the two should give is a decision, not an
oversight.

**It cannot tell a stroke from a tap.** The head sensor reports contact, not
intent, so `head` says a hand is there and nothing about how it got there. A
gentle pet and a poke read the same.
