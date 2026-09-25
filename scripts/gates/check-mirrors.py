#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check-mirrors.py — constants copied out of the firmware must still match it.

The choreography editor (`tools/choregraphies/`) is a PC tool: it cannot
include `Units.h`, so it restates the firmware's limits by hand and says so —
"Showing them here avoids writing a CSV that the robot would silently clip."

That sentence names the failure exactly, and a hand copy is how you get it. On
2026-08-01 `YAW_RANGE` went from 40 to 130 and BOTH sides had to be edited; had
only one moved, the editor would have kept authoring dances the robot trims
without a word — no error, no log, just movements that stop short.

So the mirror is checked. Each entry names where the value LIVES and where it
is COPIED, and the two must agree.

Exit 1 on any drift, so it can sit in check-all.ps1 and the pre-push hook.
"""
import io
import os
import re
import sys

# The repo root, from `scripts/gates/` — three levels up. ASSERTED and not
# assumed: a wrong root does not crash here, it makes every path below miss and
# the run reads like a broken repository instead of a moved script.
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
assert os.path.isfile(os.path.join(ROOT, 'platformio.ini')), \
    'repo root not found from ' + __file__


def read(rel):
    with io.open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


def cint(src, name):
    """`inline constexpr int NAME = 123;`"""
    m = re.search(r'constexpr\s+int\s+' + re.escape(name) + r'\s*=\s*(-?\d+)', src)
    return int(m.group(1)) if m else None


def js_num(src, pattern):
    m = re.search(pattern, src)
    return int(m.group(1)) if m else None


def main():
    units = read('src/engine/Units.h')
    app = read('tools/choregraphies/app.js')
    html = read('tools/choregraphies/index.html')
    dance = read('src/app/DanceStore.h')
    units_t = read('src/engine/Tuning.h')

    yaw_range = cint(units, 'YAW_RANGE')
    pitch_min = cint(units, 'PITCH_MIN')
    pitch_max = cint(units, 'PITCH_MAX')
    pitch_neu = cint(units, 'PITCH_NEUTRAL')

    checks = []
    problems_extra = [0]

    # --- yaw: the editor states ±YAW_RANGE, in the model AND in the slider
    checks.append(('LIM.yaw.min', -yaw_range if yaw_range is not None else None,
                   js_num(app, r'yaw:\s*\{\s*min:\s*(-?\d+)')))
    checks.append(('LIM.yaw.max', yaw_range,
                   js_num(app, r'yaw:\s*\{[^}]*max:\s*(-?\d+)')))
    checks.append(('slider yaw min', -yaw_range if yaw_range is not None else None,
                   js_num(html, r'id="yaw"[^>]*min="(-?\d+)"')))
    checks.append(('slider yaw max', yaw_range,
                   js_num(html, r'id="yaw"[^>]*max="(-?\d+)"')))

    # --- pitch OFFSETS: PITCH_MIN..MAX expressed around the neutral pose
    # UNCONDITIONAL. Registering a check inside `if parsed:` meant an
    # unreadable constant silently REMOVED the check: the script printed
    # "4 miroir(s) verifie(s), 0 probleme(s)" and exited 0 while three of the
    # mirrors it exists to hold were no longer compared (review 08-02). A miss
    # must be an ILLISIBLE line and a failure, never a shorter list.
    pdelta = (lambda a, b: a - b if None not in (a, b) else None)
    checks.append(('LIM.pitch.min', pdelta(pitch_min, pitch_neu),
                   js_num(app, r'pitch:\s*\{\s*min:\s*(-?\d+)')))
    checks.append(('LIM.pitch.max', pdelta(pitch_max, pitch_neu),
                   js_num(app, r'pitch:\s*\{[^}]*max:\s*(-?\d+)')))

    # --- the editor's key/name caps are DanceStore's, minus one each (the exit
    #     keyframe, and the NUL) — it says so in its own comments.
    # --- the guest bins aim the head through ONE pure header (space: the
    #     satellite, flight-radar: the tracked flight). It restates four
    #     Units.h constants, since a guest bin cannot include Units.h. The
    #     radar used to carry its own YAW_REL_MAX and its own formula, which
    #     had the sign backwards.
    track = read('firmware/common/headtrack.h')
    tnum = (lambda name: (lambda m: float(m.group(1)) if m else None)(
        re.search(r'constexpr\s+float\s+' + name + r'\s*=\s*([\d.]+)f', track)))
    yaw_center = cint(units, 'YAW_CENTER')
    fl = (lambda v: float(v) if v is not None else None)
    checks.append(('headtrack YAW_CENTER',    fl(yaw_center), tnum('YAW_CENTER')))
    checks.append(('headtrack YAW_REL_MAX',   fl(yaw_range),  tnum('YAW_REL_MAX')))
    checks.append(('headtrack PITCH_NEUTRAL', fl(pitch_neu),  tnum('PITCH_NEUTRAL')))
    checks.append(('headtrack PITCH_MIN',     fl(pitch_min),  tnum('PITCH_MIN')))

    # --- MAX_LINE is declared in BOTH bounded-read loops with hand-sync
    #     comments ("must match SceGuest::MAX_LINE — deliberate twins"). The
    #     read loop is the part A2.23 leaves duplicated on purpose; the SIZE it
    #     agrees on is still a hand copy, so it is checked like any other.
    guest_h = read('src/guest/SceGuest.h')
    sdcfg = read('src/app/SdConfig.h')
    mline = (lambda src: (lambda m: int(m.group(1)) if m else None)(
                 re.search(r'MAX_LINE\s*=\s*(\d+)', src)))
    checks.append(('MAX_LINE guest/companion', mline(guest_h), mline(sdcfg)))

    mk = re.search(r'MAX_KEYS\s*=\s*(\d+)', dance)
    nl = re.search(r'NAME_LEN\s*=\s*(\d+)', dance)
    checks.append(('MAX_KEYS (store-1)',
                   int(mk.group(1)) - 1 if mk else None,
                   js_num(app, r'MAX_KEYS\s*=\s*(\d+)')))
    checks.append(('MAX_NAME (NAME_LEN-1)',
                   int(nl.group(1)) - 1 if nl else None,
                   js_num(app, r'MAX_NAME\s*=\s*(\d+)')))

    # --- the LINUX TWINS. `check-all.sh` restates the two thresholds that make
    #     the native-test gate mean anything, and `find-port.sh` restates the
    #     USB identity of each board. Both are hand copies of the PowerShell
    #     originals, so both drift the same way: a suite added on Windows and
    #     not raised on Linux gives a Linux run that passes with fewer tests
    #     than it should, and a board added to one table stays unknown to the
    #     other while the script keeps answering "ABSENT" as if it were true.
    ps_all = read('scripts/check-all.ps1')
    sh_all = read('scripts/check-all.sh')
    num = (lambda src, pat: (lambda m: int(m.group(1)) if m else None)(
               re.search(pat, src)))
    checks.append(('MIN_SUITES ps1/sh',
                   num(ps_all, r'\$MIN_SUITES\s*=\s*(\d+)'),
                   num(sh_all, r'MIN_SUITES=(\d+)')))
    checks.append(('MIN_TESTS ps1/sh',
                   num(ps_all, r'\$MIN_TESTS\s*=\s*(\d+)'),
                   num(sh_all, r'MIN_TESTS=(\d+)')))

    # --- THE SAME TWO FLOORS, AS THE DOCUMENTATION STATES THEM. The mirror
    #     above holds the two scripts to each other and stopped there, so the
    #     six places that QUOTE the floors drifted freely: on the day this was
    #     written the scripts said 316 and four documents said 315, including
    #     the ROADMAP line that tells a reader what a green run proves. A floor
    #     is a claim about coverage; a stale copy of it understates the gate by
    #     exactly the suites somebody added and forgot to announce.
    #
    #     Every THREE-DIGIT count next to "suites"/"cas"/"cases"/"tests" in
    #     these files is compared, not one pinned occurrence, so a NEW mention
    #     is held the day it is written. Two digits are left alone on purpose:
    #     "26 suites" is the suite floor and is checked as such, while a bare
    #     "3 cas" in a sentence is prose.
    floor_tests = num(ps_all, r'\$MIN_TESTS\s*=\s*(\d+)')
    floor_suites = num(ps_all, r'\$MIN_SUITES\s*=\s*(\d+)')
    QUOTERS = ('docs/ROADMAP.md', 'docs/ROADMAP.fr.md',
               'scripts/README.md', 'scripts/README.fr.md', 'CLAUDE.md',
               'CONTRIBUTING.md', 'CONTRIBUTING.fr.md',
               'README.md', 'README.fr.md')
    quote_checked = 0
    for path in QUOTERS:
        body = read(path)
        for lineno, line in enumerate(body.split('\n'), 1):
            found = []
            # One optional word may sit between the count and "cas"/"cases"/
            # "tests", same reasoning as the "suites" pattern below: README.md
            # writes "427 unit tests" (word in between) where README.fr.md
            # writes "427 tests unitaires" (word after) — checking only bare
            # adjacency held the FR side and let the EN side drift unseen.
            for n in re.findall(r'(\d{3})\s*(?:\w+\s+)?(?:cas|cases|tests)\b', line):
                found.append(('cas', floor_tests, int(n)))
            # "316/316", the form the CLAUDE.md work cycle uses: both halves.
            for a, b in re.findall(r'\b(\d{3})/(\d{3})\b', line):
                found.append(('ratio', floor_tests, int(a)))
                found.append(('ratio', floor_tests, int(b)))
            # One optional word may sit between the count and "suites": the
            # English side writes "26 native suites" where the French writes
            # "26 suites natives", and requiring adjacency checked one twin
            # while letting the other drift — which is the failure this whole
            # block exists to stop.
            for n in re.findall(r'(\d{1,3})\s+(?:\w+\s+)?suites\b', line):
                found.append(('suites', floor_suites, int(n)))
            for kind, want, got in found:
                checks.append(('%s:%d %s' % (path, lineno, kind), want, got))
                quote_checked += 1

    # --- THE "LIQUID GLASS" PALETTE, in its THREE C++ copies. The console
    #     set the identity; Launcher.h, SceGuest.h's lobby and ha-remote each
    #     restate it, and SceGuest.h says so in as many words ("synced BY
    #     HAND"). A hand-synced copy with nothing checking it is a copy that
    #     drifts on the first accent change -- and the robot then wears two
    #     identities depending on which screen you are looking at.
    #
    #     Compared by VALUE, per colour, so the failure names the colour that
    #     moved rather than announcing that something did.
    launcher = read('src/app/Launcher.h')
    guest_h = read('src/guest/SceGuest.h')
    ha = read('firmware/ha-remote/main.cpp')
    hexc = (lambda src, name: (lambda m: m.group(1).lower() if m else None)(
                re.search(name + r'\s*=\s*0x([0-9A-Fa-f]{4})', src)))
    for c in ('ACC', 'ACC2', 'CARD', 'BORD', 'MUT', 'DIM', 'KO'):
        checks.append(('palette C_%s launcher/lobby' % c,
                       hexc(launcher, 'C_' + c), hexc(guest_h, 'L_' + c)))
    # ha-remote carries all but DIM's neighbours under the same names.
    for c in ('ACC', 'ACC2', 'CARD', 'BORD', 'MUT', 'DIM', 'KO'):
        checks.append(('palette C_%s launcher/ha' % c,
                       hexc(launcher, 'C_' + c), hexc(ha, 'C_' + c)))

    ps_port = read('scripts/dev/find-port.ps1')
    sh_port = read('scripts/dev/find-port.sh')
    hexid = (lambda src, pat: (lambda m: m.group(1).lower() if m else None)(
                 re.search(pat, src)))
    for board in ('cores3', 'fire'):
        # PowerShell: "VID_303A&PID_1001" on the board's row; sh: two variables.
        win = re.search(board + r"\s*=\s*@\{\s*Pattern\s*=\s*'VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})'",
                        ps_port)
        checks.append(('USB vid ' + board,
                       win.group(1).lower() if win else None,
                       hexid(sh_port, r'vid_' + board + r'=([0-9a-fA-F]{4})')))
        checks.append(('USB pid ' + board,
                       win.group(2).lower() if win else None,
                       hexid(sh_port, r'pid_' + board + r'=([0-9a-fA-F]{4})')))

    # --- THE rules.txt TEMPLATE. The firmware writes it to a card that has
    #     none (RuleStore::writeDefaultIfAbsent), and the repo carries a copy so
    #     it can be read without owning the robot. That template is the only
    #     documentation of the rule FORMAT a user ever sees, so a copy that
    #     drifts teaches a syntax the parser twelve lines below it rejects —
    #     which is what the old `rules.txt.example` did: it documented
    #     `touch_head`, `approval` and `decision`, three fields nothing in this
    #     repository publishes.
    #
    #     Compared as TEXT and not line-count: the firmware holds it as C string
    #     literals, so the comparison unescapes them back and asks for equality.
    store = read('src/app/RuleStore.h')
    m = re.search(r'f\.print\(\s*(.*?)\);', store, re.S)
    tpl = None
    if m:
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
        tpl = ''.join(p.replace('\\n', '\n').replace('\\"', '"')
                       .replace("\\\\", "\\") for p in parts)
    copy = read('sdcard/stackchan-companion/rules.txt.example')
    checks.append(('rules.txt template',
                   len(tpl) if tpl is not None else None,
                   len(copy)))
    if tpl is not None and tpl != copy:
        # Naming the FIRST differing line beats "the two lengths differ".
        a, b = tpl.split('\n'), copy.split('\n')
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else '<absent>'
            y = b[i] if i < len(b) else '<absent>'
            if x != y:
                print('DERIVE    rules.txt ligne %d\n  firmware: %s\n  copie   : %s'
                      % (i + 1, x, y))
                break
        problems_extra[0] += 1

    # --- OVER-LONG LINES. RuleStore::load reads into `char line[128]` and
    #     DISCARDS a longer line WHOLE rather than truncating it, so a comment
    #     that overflows silently vanishes from the file the user is reading to
    #     learn the format — and a RULE that overflows silently never loads.
    #
    #     127 AND NOT 126: `readBytesUntil('\n', line, sizeof(line) - 1)` accepts
    #     127 bytes, and the discard only fires when a 127-byte read is NOT
    #     followed by the newline. So 127 is the reader's real limit, and it is
    #     the number the template promises its own readers — a gate stricter
    #     than the parser would reject a template that works.
    for i, line in enumerate((tpl or '').split('\n')):
        if len(line.encode('utf-8')) > 127:
            print('LONGUEUR  rules.txt ligne %d : %d octets (max 127)'
                  % (i + 1, len(line.encode('utf-8'))))
            problems_extra[0] += 1

    # --- A GUEST THAT CREATES A TASK MUST ATTACH THE GUARD. The cooperative
    #     stop (sce::CoopStop) exists because a bin whose net task was never
    #     parked turned a ~9 s companion reflash into >10 min of SD/SPI
    #     contention (space, 2026-08-10) — and the way that happened is the
    #     recurring pattern this whole file exists for: the discipline was
    #     applied to one bin and silently missing from another. So the rule is
    #     checked, not remembered: any guest main.cpp that calls xTaskCreate
    #     must also wire `guest.netGuard = &...`.
    import glob as _glob
    for gpath in sorted(_glob.glob(os.path.join(ROOT, 'firmware', '*', 'main.cpp'))):
        rel = os.path.relpath(gpath, ROOT).replace(os.sep, '/')
        if rel.startswith('firmware/companion/'):
            continue                        # the companion is not a guest
        gsrc = io.open(gpath, encoding='utf-8').read()
        if 'xTaskCreate' not in gsrc:
            continue                        # no task, nothing to park
        if re.search(r'guest\.netGuard\s*=\s*&', gsrc):
            print('ok        garde tache %-11s netGuard attache'
                  % rel.split('/')[1])
        else:
            print('MANQUE    %s : xTaskCreate sans guest.netGuard '
                  '(contrat docs/guests/README.md)' % rel)
            problems_extra[0] += 1

    # --- THE SAME SHAPE, ONE LEVEL DOWN: a bin that MOUNTS the card must also
    #     WATCH it. The radar grew a hot-plug probe on 08-04; space and
    #     ha-remote kept treating the boot mount as permanent, so a card
    #     inserted afterwards was never seen — settings silently not persisted
    #     for the whole session, with /config still offering to save them — and
    #     a card pulled out was never noticed, with the footer still promising
    #     persistence. One bin had the mechanism, two had the bug: exactly the
    #     asymmetry the rule above was written for, so it is checked the same
    #     way rather than trusted to the next reader.
    for gpath in sorted(_glob.glob(os.path.join(ROOT, 'firmware', '*', 'main.cpp'))):
        rel = os.path.relpath(gpath, ROOT).replace(os.sep, '/')
        if rel.startswith('firmware/companion/'):
            continue                        # the companion has its own probe
        gsrc = io.open(gpath, encoding='utf-8').read()
        if 'SD.begin(' not in gsrc:
            continue                        # no card, nothing to watch
        if re.search(r'SdWatch\s+\w+', gsrc) and '.update(' in gsrc:
            print('ok        garde carte %-13s SdWatch attache'
                  % rel.split('/')[1])
        else:
            print('MANQUE    %s : SD.begin sans sce::SdWatch '
                  '(firmware/common/SdWatch.h)' % rel)
            problems_extra[0] += 1

    # --- CLAUDE.md exists TWICE, at the repo root and in `.claude/`, because
    #     the two locations are read by different tools. Nothing made them
    #     agree: they were kept identical by hand, which is the definition of a
    #     twin that drifts — and this file's own opening paragraph says how that
    #     ends. Whole-file comparison, since the whole file is the copy.
    #
    #     `.claude/CLAUDE.md` is NOT tracked here — a global gitignore covers
    #     `.claude/` — so its ABSENCE is the normal state of a fresh clone and
    #     must not be an error: a gate that crashes where it cannot apply is
    #     worse than no gate at all. It also stays OUT of `checks`, so the
    #     structural count below keeps being a contract about the repository
    #     rather than about a local convenience copy.
    if os.path.isfile(os.path.join(ROOT, '.claude', 'CLAUDE.md')):
        claude_root = read('CLAUDE.md')
        claude_dot = read('.claude/CLAUDE.md')
        if claude_root == claude_dot:
            print('ok        CLAUDE.md copie        %d octets' % len(claude_root))
        else:
            a, b = claude_root.split('\n'), claude_dot.split('\n')
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else '<absent>'
                y = b[i] if i < len(b) else '<absent>'
                if x != y:
                    print('DERIVE    CLAUDE.md ligne %d\n  racine : %s\n  .claude: %s'
                          % (i + 1, x, y))
                    break
            problems_extra[0] += 1
    else:
        print('hors sujet .claude/CLAUDE.md absent (copie locale non versionnee)')

    # --- the console's TUN table restates EVERY tuning default by hand, and it
    #     had already drifted: `soundtrack_sign` was advertised as 1 with the
    #     help text "+1 = validated K151" while Tuning.h had been re-defaulted
    #     to -1 by schema v4, because +1 turned the head AWAY from the sound.
    #     A user following the console's own help re-broke a fixed hardware
    #     regression (review 08-02). Forty hand copies, checked once.
    #     Row shape: ["key", min, max, step, DEFAULT, "help", "group"]
    console = read('src/app/WebConsole.h')
    tun_rows = re.findall(
        r'\["([a-z0-9_]+)",\s*-?[\d.]+,\s*-?[\d.]+,\s*-?[\d.]+,\s*(-?[\d.]+)\s*,',
        console)
    tun_checked = 0
    for key, shown in tun_rows:
        m = re.search(r'float\s+' + re.escape(key) + r'\s*=\s*(-?[\d.]+)f?\s*;', units_t)
        if not m:
            continue          # console rows also cover non-Tuning knobs
        tun_checked += 1
        checks.append(('TUN ' + key, float(m.group(1)), float(shown)))

    problems = problems_extra[0]
    for label, expected, got in checks:
        if expected is None or got is None:
            print('ILLISIBLE %-22s firmware=%s editeur=%s' % (label, expected, got))
            problems += 1
        elif expected != got:
            print('DERIVE    %-22s firmware=%s editeur=%s' % (label, expected, got))
            problems += 1
        else:
            print('ok        %-22s %s' % (label, got))
    # The COUNT is part of the contract: a check that stops being registered
    # is a check that stops holding.
    # 20 + the fourteen palette pairs (seven colours x two copies). Four of
    # the 20 are headtrack.h's copies of Units.h.
    STRUCTURAL, MIN_TUN = 34, 30
    # The quoted floors are counted apart because their number is not fixed by
    # this file: it is however many times the five documents state a floor. A
    # MINIMUM rather than an equality, so adding a mention never fails the
    # gate, while a document that stops stating the floors — or a regex that
    # stops matching them — drops below it and says so.
    MIN_QUOTES = 12
    if len(checks) - tun_checked - quote_checked != STRUCTURAL:
        print('TABLE    %d miroirs structurels, %d attendus'
              % (len(checks) - tun_checked - quote_checked, STRUCTURAL))
        problems += 1
    if quote_checked < MIN_QUOTES:
        print('TABLE    %d planchers cites verifies, %d attendus au minimum'
              % (quote_checked, MIN_QUOTES))
        problems += 1
    if tun_checked < MIN_TUN:
        print('TABLE    %d defauts TUN compares, %d attendus au minimum'
              % (tun_checked, MIN_TUN))
        problems += 1
    print('%d miroir(s) verifie(s), %d probleme(s)' % (len(checks), problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
