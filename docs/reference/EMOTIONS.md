> **English** · [Français](EMOTIONS.fr.md)

# The 30 expressions

The catalogue. These names are what `POST /api/emotion?name=…` accepts, what a
rule in `rules.txt` names as its action, and what a dance keyframe puts in its
`emotion` column — so this page is the list you need before writing any of
those three. How an expression is *built* and *animated* is in
[`EYES.md`](EYES.md); the source of truth for the names is
`src/engine/Emotions.h`.

Names are matched **case-insensitively** by the API.

## The eighteen originals

Ported from esp32-eyes, and the base most of the rig's geometry was tuned
against.

| Name | Reads as | Notes |
|---|---|---|
| `Normal` | neutral, awake | the resting face, and the one the roulette draws most — except in the dark, where it is removed from the draw entirely |
| `Angry` | anger | sloped upper edges |
| `Glee` | delight | happy eyes, snappier |
| `Happy` | contentment | bottom-anchored closing, no centred mode |
| `Sad` | sadness | pairs with a lowered head (`pitchBiasFor`) |
| `Worried` | concern | slight asymmetry |
| `Focused` | concentration | blink rate halved |
| `Annoyed` | irritation | |
| `Surprised` | surprise | the **largest** eye of the set (112 px) — the reference for LED brightness; eyes stay wide 2–4 s before blinking resumes |
| `Skeptic` | doubt, one-sided | static asymmetry at entry |
| `Frustrated` | frustration | |
| `Unimpressed` | flat disapproval | |
| `Sleepy` | fighting off sleep | asynchronous per-eye closing, and the dominant draw in the dark |
| `Suspicious` | wariness | |
| `Nervous` | nervous squinting | the only preset with a non-zero `OffsetX` (`Nervous_Alt`, +20 px) |
| `Furious` | rage | fastest transition, snappiest blink |
| `Scared` | fright | **the shake reflex's face** — preempts everything |
| `Awe` | wonder | wide open, gentle lower slope |

## The twelve extensions

Added for this firmware, on the Wall-E/Cozmo identity rather than on the
original set.

| Name | Reads as | Notes |
|---|---|---|
| `Excited` | excitement | special render: a **star**, drawn alone on black |
| `Questioning` | questioning | asymmetric by construction |
| `Frozen` | frozen still | heavily reduced blink rate |
| `Scary` | menacing | reduced blink rate |
| `Curious` | curiosity | asymmetric; **the lift reflex's face** |
| `Doubt` | hesitation | |
| `Contempt` | contempt | static asymmetry at entry |
| `Disgust` | disgust | |
| `Smug` | self-satisfaction | |
| `Dead` | out cold | special render: an **X**; the only expression where blinking is blocked outright, and everything else — gaze, VOR, breathing, squash — is frozen too |
| `Blush` | fond embarrassment | Happy eyes plus a pink cheek **overlay** |
| `Squint` | focused squinting | `Focused` base, low slope, reduced height |

## What picks one

Four sources, and they do not have equal standing.

| Source | How it wins |
|---|---|
| **Reflexes** | shake → `Scared`, lift → `Curious`. They **preempt everything**, including a running dance (rule A2.5) |
| **API / rules** | `POST /api/emotion`, or a `rules.txt` action. Overrides the roulette while it holds |
| **Dances** | a keyframe's `emotion` column; an empty column leaves the expression unchanged |
| **The roulette** | the idle draw, every 6–12 s, and the lowest priority of the four |

The roulette's weights and its night behaviour are in [`EYES.md`](EYES.md);
the reflex chain is drawn in
[`../architecture/WORKFLOWS.md`](../architecture/WORKFLOWS.md) §4.

## Using them

```bash
# hold an expression for 3 s, then let the roulette take over again
curl -X POST "http://<ip>/api/emotion?name=Surprised&ms=3000"
```

```
# in rules.txt — fires when the Claude context passes 90 %
# enable | field | op | value | sustainMs | cooldownMs | action
claude | ctx | ge | 90 | 3000 | 60000 | SetEmotion Worried 4000
```

Two behaviours worth knowing before you use either:

- **An unknown name is refused**, with `404 unknown emotion` — never silently
  mapped to `Normal`. A typo is then a request that visibly fails rather than
  a robot that quietly does the wrong thing. The same holds in a rule: a line
  that does not parse is simply absent from the loaded table, which
  `GET /api/rules` will show you.
- **An explicit emotion aborts a running dance.** Otherwise the dance's next
  keyframe would re-apply its own expression and overwrite yours a fraction of
  a second later. Omitting `ms` holds it for 10 s.
