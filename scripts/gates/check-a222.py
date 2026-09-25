#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check-a222.py — the single-call-site rule, checked against the BINARY.

Rule A2.22: GCC 8.4 Xtensa DROPS the second of two similar drawing calls in the
same function body. It cost a whole session in July (the Dead "X" whose second
arm was simply absent from the binary, proven by reading back the canvas buffer)
and the remedy is a discipline the source cannot express: every repeated shape
must come from ONE call site, in a loop, with a table for what varies.

A discipline the source cannot express is a discipline that rots. This script
checks it where it is actually true or false — in the linked ELF — by counting
the calls to a drawing primitive inside a symbol and comparing with what the
design says there should be.

It is deliberately a SHORT, EXPLICIT table and not a heuristic: every entry is a
place where the count was reasoned about and verified by hand, and the number is
the design decision, not a measurement to be refreshed when it drifts. A drift
IS the bug.

Usage:  python scripts/gates/check-a222.py [env ...]      (default: flight-radar)
Exit 1 on any mismatch, so it can sit in a pre-push hook.
"""
import os
import re
import shutil
import subprocess
import sys

# The repo root, from `scripts/gates/` — three levels up. ASSERTED and not
# assumed: a wrong root does not crash here, it makes every path below miss and
# the run reads like a broken repository instead of a moved script.
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
assert os.path.isfile(os.path.join(ROOT, 'platformio.ini')), \
    'repo root not found from ' + __file__
# TWO TOOLCHAIN LAYOUTS, because the objdump MOVED and this gate went quiet.
# The classic espressif32 platform ships a toolchain per chip
# (`toolchain-xtensa-esp32s3`); the pioarduino platform ships ONE unified
# toolchain (`toolchain-xtensa-esp-elf`) carrying all the xtensa targets. When
# the platform changed under it, the hardcoded path stopped existing and the
# check below printed "verification SAUTEE" and returned SUCCESS — the whole
# A2.22 gate inert, announcing itself in one line among hundreds, with
# `check-all` still green. That is precisely the failure this file's own header
# describes for an unlisted env: a guarantee that passes because it ran nothing.
# Both layouts are searched, and a missing objdump is now a hard FAILURE rather
# than a shrug (see main): a gate that cannot run has not passed.
# AND BOTH SPELLINGS OF THE BINARY. Hardcoding `.exe` made the hard failure
# just added above permanent on Linux and macOS, where the file has no suffix:
# the gate went from "announces a skip and passes" to "cannot pass at all",
# which is the same bug with the sign flipped. `check-all.sh` runs there.
_TOOLCHAINS = ['toolchain-xtensa-esp-elf', 'toolchain-xtensa-esp32s3']
_NAMES = ['xtensa-esp32s3-elf-objdump.exe', 'xtensa-esp32s3-elf-objdump']
_CANDIDATES = [os.path.join(os.path.expanduser('~'), '.platformio', 'packages',
                            tc, 'bin', n)
               for tc in _TOOLCHAINS for n in _NAMES]
# PATH last: a toolchain installed by the system package manager or by the IDF
# exporter is a legitimate answer, and it is the one a CI image usually has.
_ON_PATH = next((w for w in (shutil.which(n) for n in _NAMES) if w), None)
if _ON_PATH:
    _CANDIDATES.append(_ON_PATH)
OBJDUMP = next((p for p in _CANDIDATES if os.path.exists(p)), _CANDIDATES[0])

# env -> [(symbol, primitive, expected, why)]
EXPECT = {
    # THE COMPANION WAS NOT IN THIS TABLE, and it is the binary that runs
    # the 30 expressions (added 08-03). `main()` does `EXPECT.get(env, [])`,
    # so an unlisted env contributes ZERO checks and passes in silence -
    # including `drawBlush`, rewritten for this very rule on 08-03 (one
    # fillCircle call site in a loop, replacing eight anti-aliased wedges)
    # and drawn every frame of Blush, Glee and Smug. An edit turning that
    # stamp loop back into two similar calls in one body would lose half
    # the strokes in the linked binary with the whole gate green: the July
    # "Dead X" failure this script exists to make unshippable.
    'companion': [
        # THE SOUND VISUALISER (band mode 2). Three styles paint columns through ONE table
        # emptied by ONE fillRect: the painters deliberately DRAW NOTHING, they
        # only fill the table, which is what lets the single call site stay
        # single however many styles there are. A second fillRect appearing
        # here is a style that started drawing on its own, and GCC 8.4 Xtensa
        # is entitled to delete one of the two.
        ('_ZN3sce8Renderer9drawSoundEv', 'fillRect', 1,
         "every column of every style, through one table and one call"),
        ('_ZN3sce8Renderer9drawSoundEv', 'drawString', 0,
         "the visualiser draws no text of its own: the not-listening state is "
         "sndAsleep's, so nothing can grow a second text call beside the "
         "rectangle loop and be thinned out of the binary"),
        ('_ZN3sce8Renderer9sndAsleepEv', 'drawString', 1,
         "the one line the band shows when the microphone is not listening"),
        # The rest line is its OWN function for the same reason: inside
        # drawMouth it would have been a second fillRect beside the loop.
        ('_ZN3sce8Renderer7sndRestEv', 'fillRect', 1,
         "the wipe, and since 08-05 nothing else: the forced centre rule that "
         "used to sit beside it was a SECOND source for the rest state, "
         "disagreeing with the columns that already drew it"),
        ('_ZN3sce7effects9drawBlush', 'fillCircle', 1,
         "the blush: eight strokes stamped through ONE call site in a "
         "nested loop - written as eight calls, GCC 8.4 Xtensa is "
         "entitled to drop some and half the cheeks vanish"),
        # THE PHASE ICONS (08-24). SIX pixel-art stamps - hourglass, brain,
        # drop, mug, bell, flag - and TWO fillRect calls for all of them: the
        # opaque backing, then the stamp loop. The tables are data and the
        # loop is the only thing that draws, which is the whole design: as an
        # if-chain of six little loops this would be six similar fillRect
        # bodies in one function, and GCC 8.4 Xtensa may drop some. The
        # symptom would be one phase whose icon silently never appears -
        # invisible in review, invisible in the build, visible only to
        # somebody running a pomodoro to its fourth cycle.
        ('_ZN3sce8Renderer13drawBandClockEv', 'fillRect', 2,
         "the icon backing and the ONE stamp loop that draws all six phase "
         "icons - a third call site is a phase that started drawing itself"),
        ('_ZN3sce8Renderer12drawBandRuleEi', 'fillRect', 1,
         "the progress rule along the band's top edge: the centred mark and "
         "the two black flanks that erase what it no longer covers are a "
         "three-entry TABLE walked by one loop. Written as three fillRect "
         "calls in one body they are exactly the group GCC 8.4 Xtensa may "
         "thin, and the symptom is a mark that never closes on one side"),
        ('_ZN3sce8Renderer14drawTouchMarksEv', 'fillRect', 1,
         "the touch crosshairs: up to four segments (a vertical and a "
         "horizontal per contact) are a TABLE walked by one loop. Four "
         "similar fillRect calls in one body is the group the backend may "
         "thin, and the symptom is a crosshair permanently missing an arm - "
         "which reads as a lost contact in the one overlay whose whole job "
         "is to say where the contacts are"),
    ],
    # The space bin's two run-loop shapes. Both are drawn from a SINGLE call
    # site inside a loop precisely because the natural way to write them is
    # two or three similar calls in one body - the form GCC 8.4 Xtensa is
    # entitled to thin out. The moon one has a visible failure mode that was
    # already fixed once: the lit and dark halves must come from the same
    # scanline, or the limb shows a bright rim (user 08-04).
    'space': [
        # The scanline shapes. Both carry `noinline` in the source for the
        # same reason: called from one place, GCC folds them back into their
        # caller and the split exists in the source but not in the binary -
        # which is the illusion this script exists to expose (it caught that
        # on drawSkyNames, then again on these two).
        ('_ZL12drawMoonDisc', 'drawFastHLine', 1,
         "the moon thumbnail: lit half and shadow half are two segments "
         "filled through ONE call in a loop, both derived from the same "
         "scanline half-width. As two calls GCC may drop one and half the "
         "moon disappears; as two computations the limb grows a bright rim, "
         "which is the bug the user reported on 08-04"),
        ('_ZL11drawLitBody', 'drawFastHLine', 1,
         "a body in the Sun-Earth-Moon diagram, lit on the side facing the "
         "Sun: same two-segment shape, same single call site"),
        ('_ZL12drawMoonViewd', 'drawFastHLine', 1,
         "ONE: the rule above the phase strip. It was three until the sun "
         "rays came out of the diagram (08-04) - the count follows the "
         "design, and the point of pinning it is that a FOURTH appearing, or "
         "this one vanishing, is a change somebody made without noticing"),
        # ---- THE SIX VIEWS DRAW NO TEXT OF THEIR OWN (08-04) ----------
        # They fill a shared sce::ui::CellText and it flushes once. Pinning
        # ZERO on each body is a STRONGER statement than pinning one: it says
        # the view has no text call site left to lose, so nothing can quietly
        # grow a second drawString beside the table and be thinned out of the
        # binary the way the Dead X was.
        ('_ZL12drawMoonViewd', 'fillRect', 1,
         "the identity bar under the illumination figure: cyan and indigo "
         "segments from ONE loop. Written as two calls GCC may drop one and "
         "the ramp silently becomes a plain cyan rule"),
        # THE TWO LIT BODIES of the Sun-Earth-Moon diagram, Earth and Moon.
        # They used to go to two DIFFERENT functions (the second was a phase
        # body), which made the A2.22 shape impossible by construction; since
        # that one was removed they are TWO CALLS TO THE SAME FUNCTION from a
        # single body -- precisely what GCC 8.4 Xtensa is entitled to thin
        # out. Both are in the binary today, and pinning two is what turns
        # "the Moon became a bare circle" into a gate failure. The symbol is
        # drawMoonView because the diagram is inlined into it.
        ('_ZL12drawMoonViewd', 'drawLitBody', 2,
         "Earth and the Moon in the Sun-Earth-Moon diagram: two calls to one "
         "noinline body. Lose the second and the Moon keeps its outline but "
         "not its lit half, which reads as a drawing that was never finished"),
        ('_ZL12drawMoonViewd', 'drawString', 0,
         "phase name and the two fact lines: filled into the shared cell"
         " table, never drawn from this body"),
        ('_ZL16drawLaunchesViewv', 'drawString', 0,
         "the hero block and the queue rail: filled into the shared cell"
         " table, never drawn from this body"),
        ('_ZL11drawSkyDomed', 'drawString', 0,
         "the sky dome's cardinal points and caption: filled into the shared cell"
         " table, never drawn from this body"),
        ('_ZL11drawSkyViewd', 'drawString', 0,
         "the orrery's side panel: filled into the shared cell"
         " table, never drawn from this body"),
        ('_ZL14drawPassesViewd', 'drawString', 0,
         "the pass timetable, header and rows: filled into the shared cell"
         " table, never drawn from this body"),
        ('_ZL11drawIssViewd', 'drawString', 0,
         "the three figure rows under the map: filled into the shared cell"
         " table, never drawn from this body"),
        ('_ZN3sce2ui8CellText5flushE', 'drawString', 1,
         "THE call site, for every view of this bin. `noinline` so the"
         " compiler cannot fold it back into six callers and recreate the"
         " six sites the class exists to remove -- and so the symbol EXISTS"
         " here to be counted at all"),
        ('_ZL12drawSkyNamesd', 'drawString', 1,
         "the chart's three-letter labels, split out of drawSkyView because "
         "inline, this loop and the panel's cell loop were two similar "
         "drawString calls in one body"),
        ('_ZL10drawRocket', 'fillRect', 1,
         "the launch vehicle: nose, body, bands, engine bells, boosters and "
         "fins are TABLES emptied through one call site per primitive. "
         "Written as a dozen literal calls, GCC may drop some and a rocket "
         "that silently loses a booster is unfalsifiable by eye"),
        ('_ZL10drawRocket', 'fillTriangle', 1,
         "the cones and the fins, same table, same single call site"),
        ('_ZL18drawLaunchesNoticev', 'drawString', 1,
         "the empty state, split out of the view for the same reason as the "
         "PASSES and ISS ones"),
        # THE PAD VIEW draws every filled shape through THREE tables, one call
        # site each. ONE is the whole point: the four rules (banner, pad ground,
        # horizon separator, time axis), the four axis ticks and the eight
        # rectangles (status chip, two spine segments, selection plate, five
        # status pips) are each a set of near-identical calls in one body --
        # exactly what GCC 8.4 Xtensa is entitled to thin out. A SECOND call
        # site appearing on any of these three is a shape somebody drew beside
        # the table instead of into it.
        ('_ZL16drawLaunchesViewv', 'drawFastHLine', 1,
         "the four rules -- banner, pad ground, horizon separator, time axis "
         "-- through ONE table and ONE call"),
        ('_ZL16drawLaunchesViewv', 'drawFastVLine', 1,
         "the horizon's NOW tick and its named day/week/month ticks, same "
         "table idiom: they are drawn conditionally on how far the axis "
         "reaches, so the count is the call SITE and not the number of ticks"),
        ('_ZL16drawLaunchesViewv', 'fillRect', 1,
         "the status chip, the two segments of the facts spine, the selection "
         "plate behind the chosen silhouette and the five status pips under "
         "the horizon markers -- eight rectangles, one call site"),
        ('_ZL10drawWxIcon', 'fillCircle', 1,
         "the pad weather icon: the sun's disc and the cloud's three lobes, "
         "one table, one call. Written as four calls a lobe can go missing and "
         "the result still looks like a cloud, which is the failure mode this "
         "gate exists for -- unfalsifiable by eye"),
        ('_ZL10drawWxIcon', 'fillRect', 1,
         "the same icon's eight sun rays, the cloud's flat base and the rain "
         "or fog strokes, from the second table"),
        ('_ZL14drawPolarModal', 'drawString', 6,
         "the pass chart's caption: SIX, and pinned rather than merged "
         "because they differ in datum and colour, not just in text. It "
         "carries `noinline` so the symbol EXISTS to be checked -- inlined, "
         "the bin's only modal sat outside this gate entirely (review 08-04)"),
        ('_ZL14drawPolarModal', 'drawCircle', 3,
         "the horizon rim and the 30/60 degree rings"),
        ('_ZL11drawSkyDomed', 'drawCircle', 2,
         "TWO: the three altitude rings from ONE call site in a loop, and the "
         "outline round the Moon's phase disc"),
        ('_ZL16drawPassesNotice', 'drawString', 1,
         "the three empty/interim states of PASSES, split out of the view for "
         "exactly this reason: with them inline the body had four similar "
         "drawString calls and the binary came back with three"),
        ('_ZL14drawPassesViewd', 'drawFastHLine', 2,
         "the header rule and the pass profile's horizon line -- the second "
         "is drawn once per row from ONE call site in the profile loop"),
        ('_ZL14drawPassesViewd', 'fillRect', 1,
         "the selected row's highlight"),
        ('_ZL13drawIssNoticev', 'drawString', 1,
         "the ISS view's no-element-set state, split out for the same reason "
         "as the PASSES one"),
        ('_ZL11drawIssViewd', 'drawFastVLine', 3,
         "THREE: the day/night background (up to three runs per column, 320 "
         "columns, ONE call site in a loop - this is the one that matters), "
         "the dotted prime meridian, and the observer cross. A dropped call "
         "here blanks the map, and the count is what turns that into a gate "
         "failure instead of a squint"),
        ('_ZL11drawIssViewd', 'drawFastHLine', 2,
         "the dotted equator and the observer cross. The coastline is plotted "
         "with drawPixel, which LovyanGFX inlines - no call site to count"),
    ],
    'flight-radar': [
        ('_ZL14viewHoldBannerv', 'drawString', 1,
         "hold + refresh + weather-worsened share ONE call site; written as "
         "three branches with their own calls, GCC emitted one"),
        ('_ZL12metarCatChip', 'drawString', 1,
         "day colour code and night weight draw the same label"),
        ('_ZL14drawNotamEmptyb', 'drawString', 3,
         "the reason, the 6-row table in ONE loop, and the HTTP code "
         "(String overload) - the table is what must not become 6 calls"),
        ('_ZL15drawRadarFooterPKct', 'drawString', 1,
         "the radar's bottom line: tracking hint, idle hint and banner all "
         "go through ONE call - it was three drawStrings in three branches "
         "(08-02), which is precisely what GCC is entitled to thin out"),
        # THE METAR CARD. Its margin columns used to be drawn from a table this
        # body carried itself; they now fill the shared sce::ui::CellText, so
        # what is pinned here is the text the card draws BESIDE that table.
        ('_ZL9drawMetarv', 'drawString', 2,
         "TWO, and none of them the margin columns: those go through the "
         "shared cell table. What is left is the failure panel's row loop and "
         "the raw report's three-row loop - two loops, two call sites. It was "
         "THREE until 08-25, when the failure line and its HTTP code were two "
         "similar calls in this one body: dropping CORE_DEBUG_LEVEL from 3 to "
         "1 moved the inlining budget and the ESP32 backend emitted only one "
         "of them, taking the HTTP number off the screen with it. They are a "
         "table and a loop now, which is why the count went DOWN by one - a "
         "pair that cannot be thinned because there is no pair"),
        ('_ZL9drawMetarv', 'fillRect', 1,
         "the card's wipe. A second one appearing means a panel started "
         "painting its own background beside the wipe, which is the pair GCC "
         "is entitled to thin out"),
        ('_ZN3sce2ui8CellText5flushE', 'drawString', 1,
         "THE call site for the margin columns, and the same symbol the space "
         "bin pins: this env reached it by retiring its own copy of the class, "
         "so the point is pinned here too rather than trusted to the other "
         "binary's build"),
    ],
    # THE SECOND COMPILER BACKEND. Same source, different target (ESP32 vs
    # ESP32-S3), and A2.22 is a code-generation rule - so checking it on one
    # backend says nothing about the other. check-all.ps1 builds this env on
    # the stated grounds that an unwatched variant rots in silence; leaving it
    # out of THIS gate was the same omission one level down.
    # The xtensa-esp32s3 objdump reads the ESP32 ELF fine (same e_machine).
    # ITS TABLE IS DERIVED FROM THE RADAR'S, below, exactly the way space-fire
    # derives from space - both radar envs share one `build_src_filter`, so
    # every symbol checked on one is compiled into the other. It is written
    # after the shared entries are in place, so there is no literal list here.
}

# ---------------------------------------------------------------------------
# space-fire: the SAME source as `space`, on the ESP32 backend instead of the
# ESP32-S3. A2.22 is a rule about CODE GENERATION, so a green S3 binary says
# nothing about this one — that is the whole reason both radar envs are gated.
#
# DERIVED, deliberately, and not copied: a point added to `space` covers this
# env the same day. The two radar tables were hand-maintained and had already
# drifted apart, which is the same source guarded by two different lists.
#
# ONE exclusion, NAMED rather than filtered away silently: on this backend GCC
# inlines `drawSkyNames` into its caller, so no symbol remains to count. The
# body it folds into, `drawSkyView`, is still pinned at ZERO drawString calls
# below — so a text call site appearing there is still caught, which is what
# the split was protecting in the first place.
# ---------------------------------------------------------------------------
# THE NO-CARD SCREEN now belongs to SceGuest, so it is ONE symbol checked in
# every bin that includes the header instead of a copy per bin. That is the
# point of having factored it: the rule is pinned once, and a new guest bin
# inherits both the screen and its guard.
_NOSD = ('_ZN3sce8SceGuest9paintNoSd', 'drawString', 1,
         "the no-card screen: up to eleven rows differing only by colour, "
         "through ONE call. It is the screen that says what a missing card "
         "costs, so a row thinned out of the binary would be a warning nobody "
         "would ever notice missing. The title uses print(), a different "
         "primitive, so it costs no second site")
_NOSD_BAN = ('_ZN3sce8SceGuest9paintNoSd', 'fillRect', 2,
             "TWO, and they are two SHAPES, not a peeled pair: fillScreen "
             "resolves to the same fillRect primitive (the wipe), and the "
             "ALERT banners - title, and failed-retry band - come from ONE "
             "loop whose 1-or-2 count is made opaque with an empty asm so "
             "GCC cannot peel it into a third. A banner thinned out of the "
             "binary is an alert nobody sees missing")
# ---- ha-remote's OWN bodies. It applies A2.22 five times and said so in
# comments; nothing watched any of it, so a revert or a "simplifying" rewrite
# passed with the gate green. Two of these needed `noinline` first: an inlined
# body has no symbol, and a body with no symbol cannot be watched.
EXPECT['ha-remote'] = EXPECT.get('ha-remote', []) + [
    ('_ZL6headerPKcS0_', 'fillRect', 2,
     "the cyan->indigo rule under the title is TWO halves written as one "
     "loop, and GCC unrolls it back into two calls - which is fine and is "
     "what must stay: the danger is a half DROPPED, and a header with one "
     "colour reads as a rendering bug nobody can explain"),
    ('_ZL6headerPKcS0_', 'drawString', 2,
     "title, and the optional right-hand label - two branches, two labels"),
    ('_ZL8sliderAtiPKciiiS0_t', 'drawString', 2,
     "the slider's name and its value; both must survive or the control "
     "shows a number with nothing saying what it sets"),
    ('_ZL8sliderAtiPKciiiS0_t', 'fillCircle', 1,
     "the knob, ONE call site by construction (its comment names the trap): "
     "written as two similar calls, the second is what GCC 8.4 Xtensa drops"),
    ('_ZL7catIconiiit', 'drawFastVLine', 2,
     "the two mirrored rays of the icon, kept as a parameterized pair rather "
     "than two calls - the source comment marks it as such"),
    ('_ZL8drawListv', 'fillTriangle', 1,
     "the up/down arrows used to be two MIRRORED fillTriangle; they are now "
     "ONE call site with the apex parameterized by direction. Two again "
     "would mean the rewrite came back, and one arrow would silently vanish"),
    ('_ZL8drawListv', 'drawRoundRect', 3,
     "the row frames come from a loop, not from near-identical calls written "
     "out - the comment at the second one names the pair it replaced"),
]

# led-fluid: the fluid is painted STRAIGHT to the display, dot by changed dot,
# while the two panels are composed in a PSRAM sprite. Both surfaces are pinned
# here — the primitive is the same symbol whichever one it draws into, and the
# compiler's licence to thin a body out does not care about the target either.
EXPECT['led-fluid'] = [
    ('_ZL9paintGridRKN3sce5fluid7DotGridE', 'fillCircle', 1,
     "THE dots, and the whole picture: one call site inside the row/column "
     "loop. The obvious way to write it is two similar calls - a lit dot and "
     "a faded one - and that is exactly the shape GCC 8.4 Xtensa is entitled "
     "to thin out; half the fluid would be missing from the binary while the "
     "source still showed it"),
    ('_ZL9paintGridRKN3sce5fluid7DotGridE', 'fillRect', 1,
     "the wipe: fillScreen resolves to the same primitive. It runs only when "
     "the grid geometry changes (a new dot size), and losing it would leave "
     "the previous grid's dots under the new one forever"),
    ('_ZL12drawTabStripv', 'fillRoundRect', 2,
     "the two tab chips: written as ONE call in a two-iteration loop, which "
     "GCC unrolls back into two - and that is the outcome to keep. What the "
     "count forbids is a DROPPED one, which would leave the panel with a "
     "single tab and no way to reach the other"),
    ('_ZL14drawSliderRowsv', 'fillCircle', 1,
     "the knob of every slider, one call site for all seven rows"),
    ('_ZL14drawSliderRowsv', 'drawFastHLine', 1,
     "the track under every knob, same single site: a track without a knob "
     "still reads as a control, a knob without a track does not"),
    ('_ZL14drawToggleRowsv', 'fillRoundRect', 1,
     "the three switches, ONE call site - and in its own body rather than "
     "beside the tab strip's, which would be two fillRoundRect in one "
     "function and put both at the compiler's mercy"),
    ('_ZL15drawPresetChipsv', 'fillRoundRect', 1,
     "the five preset chips, for the same reason and in their own body too"),
    ('_ZL11drawHueRectv', 'fillRect', 1,
     "the hue/saturation gradient: every column band through ONE call"),
    ('_ZL13drawCrosshairv', 'drawCircle', 2,
     "the crosshair is a white ring INSIDE a black one, written as one call "
     "over two radii and unrolled back to two. Both must survive: alone, the "
     "white ring vanishes against a pale gradient and the picker loses the "
     "only mark showing what is currently chosen"),
    ('_ZL13drawBrightBarv', 'fillCircle', 1,
     "the brightness knob"),
    ('_ZL13drawBrightBarv', 'drawFastHLine', 1,
     "and its track"),
    ('_ZL12drawSwatchesv', 'fillCircle', 1,
     "the six sample dots, ONE call site: they are the only honest preview of "
     "what the chosen hue will look like as fluid, so a thinned-out half "
     "would be a preview that lies by omission"),
]

for _e in ('flight-radar', 'space', 'ha-remote', 'led-fluid'):
    EXPECT.setdefault(_e, []).append(_NOSD)
    EXPECT.setdefault(_e, []).append(_NOSD_BAN)

# THE SECOND RADAR BACKEND, derived rather than hand-kept. The comment above
# claimed this derivation while the code below it was a literal list of three
# entries against the radar's own six -- so the two lists it says were
# reconciled, `metarCatChip` and `drawNotamEmpty`, were missing again, and with
# them the no-card screen the loop above hands every other guest. A gate that
# describes a mechanism it does not run is worse than no gate: it is read as
# coverage. Derived AFTER the loop for the same reason space-fire is, so the
# shared entries come across once and not twice.
#
# No exclusion so far: unlike space's drawSkyNames, no radar body is folded
# away by the ESP32 backend. If one ever is, name it here rather than delete
# the point -- an exclusion nobody can read is how the two lists drifted.
_RADAR_FIRE_INLINED = ()
EXPECT['flight-radar-fire'] = [e for e in EXPECT['flight-radar']
                               if not e[0].startswith(_RADAR_FIRE_INLINED)]
# The A+C debug overlay exists only on the buttons profile, exactly as on
# space-fire: appended after the derivation so the touch build is not asked
# for a symbol it cannot contain.
EXPECT['flight-radar-fire'].append(
    ('_ZL9drawDebugv', 'drawString', 2,
     "the A+C debug overlay: FIFTEEN rows through ONE loop, plus the "
     "footer - written as fifteen calls, a dropped one would take a "
     "diagnostic line with it and the screen would never mention it"))

_FIRE_INLINED = ('_ZL12drawSkyNamesd',)
EXPECT['space-fire'] = [e for e in EXPECT['space']
                        if not e[0].startswith(_FIRE_INLINED)]
# The A+C debug overlay exists only on the buttons profile (SCE_INPUT_BUTTONS),
# so the symbol is in space-fire's ELF and not in space's — the same reason the
# radar pins its drawDebug on flight-radar-fire only. Appended AFTER the
# derivation on purpose: putting it in EXPECT['space'] would make the gate
# demand a symbol the touch build cannot contain.
EXPECT['space-fire'].append(
    ('_ZL9drawDebugv', 'drawString', 2,
     "the A+C debug overlay: THIRTEEN rows through ONE loop, plus the "
     "footer - written as thirteen calls, a dropped one would take a "
     "diagnostic line with it and the screen would never mention it"))

# led-fluid-fire: the SAME source as `led-fluid`, on the ESP32 backend. DERIVED
# for the reason the other two ports are - a point added to the CoreS3 table has
# to cover this build too, and a copy is a table that stops agreeing the first
# time somebody edits one of them. No exclusion list: nothing in this bin is
# compiled out on the Fire except the PY32 ring, whose writes are I2C and draw
# nothing. A2.22 is a code-GENERATION rule, so verifying it on the S3 says
# nothing about the Fire's ESP32 - which is the whole reason this entry exists.
EXPECT['led-fluid-fire'] = list(EXPECT['led-fluid'])


def symbols(elf):
    out = subprocess.run([OBJDUMP, '-t', elf], capture_output=True, text=True,
                         errors='replace').stdout
    found = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) < 6:
            continue
        if not re.fullmatch(r'[0-9a-f]{8}', f[0] or ''):
            continue
        try:
            size = int(f[-2], 16)
        except ValueError:
            continue
        found.setdefault(f[-1], (int(f[0], 16), size))
    return found


def count_calls(elf, addr, size, primitive):
    a = '--start-address=0x%x' % addr
    b = '--stop-address=0x%x' % (addr + size)
    out = subprocess.run([OBJDUMP, '-d', a, b, elf], capture_output=True,
                         text=True, errors='replace').stdout
    return sum(1 for l in out.splitlines() if primitive in l and 'call' in l)


def main(argv):
    envs = argv[1:] or ['flight-radar']
    if not os.path.exists(OBJDUMP):
        # FAILURE AND NOT A SKIP. This used to return 0, so a toolchain that
        # moved turned the whole gate into one line of prose among hundreds
        # and `check-all` stayed green while A2.22 verified NOTHING. A check
        # that cannot run has not passed; say so with the exit code, which is
        # the only part anything downstream reads.
        print('objdump INTROUVABLE - A2.22 NE PEUT PAS ETRE VERIFIE.')
        for p in _CANDIDATES:
            print('   cherche : %s' % p)
        return 1
    problems = 0
    for env in envs:
        elf = os.path.join(ROOT, '.pio', 'build', env, 'firmware.elf')
        if not os.path.exists(elf):
            print('%s : firmware.elf absent - construire d abord' % env)
            problems += 1
            continue
        table = EXPECT.get(env, [])
        syms = symbols(elf)
        for want, primitive, expected, why in table:
            match = sorted(k for k in syms if k.startswith(want))
            if not match:
                print('MANQUE  %s : symbole %s absent' % (env, want))
                problems += 1
                continue
            # AMBIGUITY IS A FAILURE, not a choice. The prefix exists because
            # GCC decorates (`$constprop$350`), but if it ever matches TWO
            # symbols, picking one silently means checking one body while the
            # other regresses — the exact blind spot this script is here to
            # remove (review 08-02).
            if len(match) > 1:
                print('AMBIGU  %s : le prefixe %s designe %d symboles : %s'
                      % (env, want, len(match), ', '.join(match)))
                problems += 1
                continue
            addr, size = syms[match[0]]
            n = count_calls(elf, addr, size, primitive)
            if n != expected:
                print('A2.22   %s : %s -> %d appel(s) a %s, attendu %d'
                      % (env, match[0], n, primitive, expected))
                print('        %s' % why)
                problems += 1
            else:
                print('ok      %-24s %s x%d' % (match[0], primitive, n))
    print('%d point(s) verifie(s), %d probleme(s)'
          % (sum(len(EXPECT.get(e, [])) for e in envs), problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
