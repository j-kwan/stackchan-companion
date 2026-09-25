# -*- coding: utf-8 -*-
"""Checks that every document has its translation, and that the two match.

The documentation ships in two languages: ENGLISH is canonical and lives at the
original paths (`docs/ROADMAP.md`), FRENCH is a sibling (`docs/ROADMAP.fr.md`).
Both are updated together — a one-sided edit is a silent divergence, and the
reader has no way of telling which file lies.

"Updated together" cannot be machine-checked down to the sentence. Two cheaper
things can, and between them they catch the mistakes that actually happen.

The SKELETON — headings, table rows, status markers — catches a section added
on one side only, a table row appended to one file, a status flipped in English
and forgotten in French.

The VOLUME catches what the skeleton cannot see: prose added under an existing
heading, a paragraph expanded, a diagram's explanation rewritten. A translation
can fall hundreds of lines behind with every structural check green, and did.

    python scripts/gates/check-doc-parity.py

Exits 1 on any problem, so it can gate a commit hook or CI.

WHY NOT translate-and-compare: a real semantic check would need a translation
engine and would still cry wolf on legitimate wording differences. The skeleton
is cheap, deterministic, and has no false positives — and a guard that cries
wolf ends up ignored, which is worth less than no guard at all.
"""
import io
import os
import re
import sys

# A Windows console defaults to cp1252: printing a diff line that carries an
# arrow, an em dash or a degree sign would crash this tool on the very files it
# exists to check. Force UTF-8 on stdout, replacing what cannot be encoded.
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# `tmp/` is the GENERATED deployment kit (see .gitignore): a copied binary
# tree with an operator's card, not part of the bilingual corpus. `unpublished/`
# is a private, gitignored workspace (notes, correspondence drafts) - never
# tracked, never meant to be bilingual.
SKIP_DIRS = {'.pio', 'graphify-out', '.git', 'node_modules', '.claude', 'tmp',
             'unpublished'}
# CLAUDE.md is the agent's working contract, mirrored into a gitignored copy,
# and its rules are quoted verbatim from the code. It stays out of the
# bilingual scheme on purpose.
EXCLUDE = {'./CLAUDE.md'}

HEADING = re.compile(r'^(#{1,6})\s')
ROW = re.compile(r'^\s*\|')
STATUS = ('✅', '\U0001f527', '\U0001f532', '⏳', '❌')  # ok, fixed, todo, wip, failed
FENCE = re.compile(r'^\s*```')
# A .fr.md header points back at the English file; it has no counterpart there.
FR_HEADER = re.compile(r'^\s*>\s*\[English\]')


def skeleton(path):
    """(heading depths, table row count, sequence of status markers).

    Headings are reduced to their DEPTH: the text is translated, the nesting is
    not. Code fences are skipped — a shell snippet is identical in both files
    and would only add noise.
    """
    depths, rows, states = [], 0, []
    in_code = False
    for line in io.open(path, encoding='utf-8'):
        if FENCE.match(line):
            in_code = not in_code
            continue
        if in_code or FR_HEADER.match(line):
            continue
        m = HEADING.match(line)
        if m:
            depths.append(len(m.group(1)))
        if ROW.match(line):
            rows += 1
            found = [s for s in STATUS if s in line]
            if len(found) == 1:
                states.append(found[0])
    return depths, rows, states


def main():
    docs = []
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            if f.endswith('.md') and not f.endswith('.fr.md'):
                p = os.path.join(root, f).replace(os.sep, '/')
                if p not in EXCLUDE:
                    docs.append(p)
    docs.sort()

    problems = 0
    for en in docs:
        fr = en[:-3] + '.fr.md'
        if not os.path.exists(fr):
            print('MISSING    %s  ->  no %s' % (en, os.path.basename(fr)))
            problems += 1
            continue

        de, re_, se = skeleton(en)
        df, rf, sf = skeleton(fr)

        if de != df:
            print('STRUCTURE  %s' % en)
            print('           %d headings in EN, %d in FR (depths: %s vs %s)'
                  % (len(de), len(df), de[:12], df[:12]))
            problems += 1
        if re_ != rf:
            print('TABLES     %s' % en)
            print('           %d table rows in EN, %d in FR' % (re_, rf))
            problems += 1
        if se != sf:
            print('STATUSES   %s' % en)
            bad = [(i, a, b) for i, (a, b) in enumerate(zip(se, sf)) if a != b]
            print('           %d statuses in EN, %d in FR, %d differ'
                  % (len(se), len(sf), len(bad)))
            for i, a, b in bad[:5]:
                print('             #%d: EN %s / FR %s' % (i + 1, a, b))
            problems += 1

        # --- VOLUME. The skeleton checks above compare STRUCTURE, and a
        #     translation can fall a long way behind without changing a single
        #     heading: prose added under an existing title, a paragraph
        #     expanded, a diagram's explanation rewritten. That is not
        #     hypothetical — WORKFLOWS.fr.md once sat 248 lines behind its
        #     English twin with every structural check green, which is the
        #     failure this file exists to prevent.
        #
        #     The corridor comes from the corpus, not from taste: across every
        #     substantial pair here French runs 1.03 to 1.12 times the English
        #     byte count (it is a wordier language, reliably so). [0.90, 1.30]
        #     leaves generous room around that band while a genuinely stale
        #     twin — 0.73 in the case above — falls outside it.
        #
        #     SIZE FLOOR, because a ratio is meaningless on a short file: a
        #     four-line README swings past 1.6 on one added sentence, and a
        #     gate that cries wolf is a gate people learn to ignore.
        be, bf = os.path.getsize(en), os.path.getsize(fr)
        if be >= 4000:
            ratio = bf / float(be)
            if ratio < 0.90 or ratio > 1.30:
                print('VOLUME     %s' % en)
                print('           FR/EN = %.2f (expected 0.90-1.30): %d vs %d '
                      'bytes — one side is probably behind' % (ratio, bf, be))
                problems += 1

        # Each side must announce the other: a reader landing on the French file
        # has to know it is not the canonical one.
        if '.fr.md' not in io.open(en, encoding='utf-8').readline():
            print('HEADER     %s  does not link to its French version' % en)
            problems += 1
        if '[English]' not in io.open(fr, encoding='utf-8').readline():
            print('HEADER     %s  does not link to the English version' % fr)
            problems += 1

    # ---- WHERE THE LINKS GO -------------------------------------------
    # Two rules, checked over EVERY markdown file rather than only the twins,
    # because the READMEs outside docs/ link into it just as much.
    #
    # 1. A document links WITHIN ITS OWN LANGUAGE. A French page that sends the
    #    reader to an English one drops them out of their language mid-
    #    sentence; the reverse is worse, since an English reader landing on a
    #    French page has no way of knowing the English twin exists. Only
    #    reported when the same-language twin actually EXISTS — a link to a
    #    document with no translation has nowhere else to go.
    #
    #    THE LANGUAGE SWITCHER IS EXEMPT, and must stay: every file opens with
    #    a link to its OWN twin. That is the one cross-language link a document
    #    is supposed to carry, and the check above requires it.
    #
    # 2. A link points at a file that exists. Cheap, and it was never checked:
    #    fourteen dead ones were live when this was written.
    #
    # 3. A label that IS a filename names the file it actually opens. Rule 1
    #    rewrote targets and left the labels behind, so eighteen links read
    #    `README.md` and went to `README.fr.md` — the reader is told one thing
    #    and given another, which is worse than either being wrong alone.
    #    Only labels that are a bare filename are judged: a prose label
    #    ("mechanisms as diagrams") is free to say whatever it likes.
    link_re = re.compile(r'\[([^\]]*?)\]\(([^)\s#]+\.md)(#[^)]*)?\)')
    label_is_file = re.compile(r'`?([A-Za-z0-9_.-]+\.md)`?$')
    all_md = []
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        all_md += [os.path.join(root, f) for f in files if f.endswith('.md')]
    for path in sorted(all_md):
        rel = path.replace(os.sep, '/')
        if rel.startswith('./'):
            rel = rel[2:]
        is_fr = rel.endswith('.fr.md')
        own_twin = os.path.normpath(
            (path[:-6] + '.md') if is_fr else (path[:-3] + '.fr.md'))
        body = io.open(path, encoding='utf-8', errors='replace').read()
        for m in link_re.finditer(body):
            label, target = m.group(1), m.group(2)
            if target.startswith('http'):
                continue
            lm = label_is_file.match(label.strip())
            if lm and lm.group(1) != os.path.basename(target):
                print('LIBELLE    %s  [%s] ouvre %s'
                      % (rel, label.strip(), os.path.basename(target)))
                problems += 1
            resolved = os.path.normpath(
                os.path.join(os.path.dirname(path), target))
            if not os.path.isfile(resolved):
                print('LIEN MORT  %s  ->  %s' % (rel, target))
                problems += 1
                continue
            if resolved == own_twin:
                continue                      # the switcher
            t_is_fr = target.endswith('.fr.md')
            if is_fr and not t_is_fr:
                cand = target[:-3] + '.fr.md'
            elif (not is_fr) and t_is_fr:
                cand = target[:-6] + '.md'
            else:
                continue
            if os.path.isfile(os.path.normpath(
                    os.path.join(os.path.dirname(path), cand))):
                print('HORS LANGUE %s  ->  %s   (utiliser %s)'
                      % (rel, target, cand))
                problems += 1

    print('\n%d document(s) checked, %d problem(s)' % (len(docs), problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
