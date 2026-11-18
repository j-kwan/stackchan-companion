> **English** · [Français](README.fr.md)

# `personalities/` — one page per character

The MECHANISM — what a personality owns, what it may never touch, how to write
its rules and how to add one — lives in
[`reference/PERSONALITIES.md`](../reference/PERSONALITIES.md). This directory
holds the CHARACTERS themselves: what each one is like to live with.

| Character | Index | Page |
|---|---|---|
| Default | `0` | *(no page — it is the robot the rest of the documentation describes)* |
| Haro | `1` | [`HARO.md`](HARO.md) |

The default personality has no page on purpose, and the reason is not that it
matters less. Every other document here already describes it: it *is* the robot
whose emotions `EMOTIONS.md` lists, whose rules `PLUGINS.md` explains, whose face
`EYES.md` draws. Giving it a page of its own would be a second, thinner account
of all of them, and the two would drift.

A character page answers what the reference cannot: **what is this one like?**
What it notices, what it ignores, what it is bad at. A table of settings is not a
description of a companion.

## Adding a character

Three things, and only the first needs a recompile:

1. an entry in `src/behavior/Personalities.h` — name, rule file, colour,
   roulette cadence, and the weights it rests in;
2. a rule file beside `rules.txt` on the card;
3. a page here, in both languages, telling someone what it is like.

The name and the rule path must be unique — `test_personalities` checks both,
because two characters sharing a rule file would put them back in contention for
the same 21 rule slots that the per-file design exists to separate.
