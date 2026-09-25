#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check-vendored.py — the vendored copies must not drift from their source.

`src/guest/SceGuest.h` is contractually COPYABLE AS-IS into a third-party
project (docs/guests/README.md), so it cannot hard-depend on
`firmware/common/`. It therefore carries `#if __has_include(...)` blocks with a
vendored fallback for the shared headers.

That fallback is exactly the shape rule 17 warns about: a second copy of code
that must behave identically. Four divergent YAML parsers already cost two
outages. This script makes the divergence impossible to ship rather than
merely discouraged.

BOTH SIDES ARE DELIMITED BY MARKERS, and that is the correction of a real hole
(review 08-02): the first version sliced the shared header "from the first
`namespace sce {` to EOF", so everything above it — the includes — was outside
the comparison. `Yaml.h` had `#include <stddef.h>` and its vendored copy did
not, and the gate printed `ok`. In-repo nothing broke (the real header is
included); the third party who copies SceGuest.h alone, which is the whole
contract, would have got a compile error this gate certified as fine.
Each vendorable block therefore declares what it NEEDS from outside itself, and
the guest side must satisfy it.

Exit code 1 on any drift, so it can sit in a pre-push hook.
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

GUEST = 'src/guest/SceGuest.h'
# name -> shared header carrying `>>> VENDORABLE BEGIN <name> <<<`
CASES = {
    'Yaml': 'firmware/common/Yaml.h',
    'I18n': 'firmware/common/I18n.h',
    'FirmwareInfo': 'firmware/common/FirmwareInfo.h',
    'Trace': 'firmware/common/Trace.h',
    'Gesture': 'firmware/common/Gesture.h',
}


def read(rel):
    with io.open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


def strip_comments(src):
    """Remove comments WITHOUT touching string or char literals.

    A blunt `//[^\\n]*` also eats the tail of any line containing `https://`,
    which would make a divergent `return "A"` vs `return "B"` compare equal
    (review 08-02). So we walk the source in states.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in '"\'':
            q = c
            out.append(c)
            i += 1
            while i < n:
                out.append(src[i])
                if src[i] == '\\':          # escape: take the next verbatim
                    if i + 1 < n:
                        out.append(src[i + 1])
                    i += 2
                    continue
                if src[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def tokens(src):
    return re.sub(r'\s+', ' ', strip_comments(src)).strip()


def between(src, begin, end):
    """Body between two markers, EXCLUDING the remainder of the begin line.

    The guest marker reads `>>> VENDORED FROM <path> — DO NOT EDIT HERE <<<`,
    so matching on the path alone would drag that tail into the comparison.
    """
    m = re.search(re.escape(begin) + r'[^\n]*\n(.*?)' + re.escape(end),
                  src, flags=re.S)
    return m.group(1) if m else None


def main():
    guest = read(GUEST)
    problems = 0
    for name, shared_rel in CASES.items():
        shared = read(shared_rel)
        src = between(shared, '>>> VENDORABLE BEGIN %s <<<' % name,
                              '>>> VENDORABLE END %s <<<' % name)
        cpy = between(guest, '>>> VENDORED FROM %s' % shared_rel,
                             '>>> END VENDORED <<<')
        if src is None:
            print('MANQUE  %s : marqueurs VENDORABLE %s absents' % (shared_rel, name))
            problems += 1
            continue
        if cpy is None:
            print('MANQUE  %s : bloc vendore de %s absent' % (GUEST, shared_rel))
            problems += 1
            continue
        a, b = tokens(cpy), tokens(src)
        if a != b:
            print('DIVERGE %s <-> %s' % (GUEST, shared_rel))
            for i, (ca, cb) in enumerate(zip(a, b)):
                if ca != cb:
                    print('        premier ecart a l offset %d :' % i)
                    print('          vendore : ...%s' % a[max(0, i - 40):i + 40])
                    print('          partage : ...%s' % b[max(0, i - 40):i + 40])
                    break
            else:
                print('        longueurs differentes : %d vs %d' % (len(a), len(b)))
            problems += 1
            continue
        # What the block needs from outside must be present on the guest side —
        # the vendored copy is compiled ALONE there.
        needs = re.findall(r'NEEDS:\s*(<[^>]+>)', shared)
        missing = [h for h in needs if ('#include %s' % h) not in guest]
        if missing:
            print('MANQUE  %s : %s a besoin de %s, absent du guest'
                  % (GUEST, name, ', '.join(missing)))
            problems += 1
            continue
        print('ok      %-5s %s == %s (%d octets, NEEDS %s)'
              % (name, os.path.basename(shared_rel), GUEST, len(a),
                 ', '.join(needs) if needs else '-'))
    print('%d copie(s) verifiee(s), %d probleme(s)' % (len(CASES), problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
