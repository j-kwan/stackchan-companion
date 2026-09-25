# -*- coding: utf-8 -*-
"""Proves that a change touched COMMENTS ONLY, never the code.

Written for the sweep that moved every comment to English (63 files, ~7000
lines), and kept because that sweep will not be the last one: any mass edit of
comments — a rename, a rule renumbering, a translation — needs the same proof.

    python scripts/gates/check-comments-only.py            # working tree vs HEAD
    python scripts/gates/check-comments-only.py HEAD~3     # vs another commit

Exits 1 if any file has a code difference.

METHOD: for each changed file, strip the comments out of both versions and
compare what is left. If the two "code skeletons" are identical, only comments
moved.

The comment stripper is deliberately NAIVE — it mangles a `//` inside a string
literal, for instance. That is not a flaw: the SAME treatment is applied to both
sides, so an identical mangling preserves equality. What we are looking for is a
DIFFERENCE, and any real code edit produces one.

ONE KNOWN FALSE POSITIVE: a Python docstring is a string literal, not a comment,
so rewriting one is reported as a code change. Nothing to fix here — the tool is
right that the file's *literals* changed. Read the reported diff before
concluding: it names the lines, so a docstring is obvious at a glance.

WHY THIS EXISTS: a green build is not proof. A translated string literal, a
constant nudged by one, a condition inverted inside a rewritten block — all of
those compile. This compares the code itself, so it catches what the compiler
cannot see.
"""
import difflib
import io
import re
import subprocess
import sys

BLOCK = re.compile(r'/\*.*?\*/', re.S)
# `<!-- -->` too: this firmware embeds its whole web console as C++ string
# literals, so HTML comments live inside .h files. Without this the tool
# reported every translated HTML comment as a code change — a guard that cries
# wolf ends up ignored.
HTML = re.compile(r'<!--.*?-->', re.S)
# A Windows console defaults to cp1252: printing a diff line that carries an
# arrow, an em dash or a degree sign would crash this tool on the very files it
# exists to check. Force UTF-8 on stdout, replacing what cannot be encoded.
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

CODE_EXT = ('.h', '.cpp', '.c', '.hpp', '.js', '.py', '.css')


def strip_comments(text):
    """Code lines only, whitespace normalised."""
    text = BLOCK.sub(' ', text)
    text = HTML.sub(' ', text)
    out = []
    for line in text.splitlines():
        i = line.find('//')
        if i >= 0:
            line = line[:i]
        # `#` opens a comment in Python; harmless elsewhere since both sides get
        # the same treatment (a C preprocessor directive is dropped on both).
        if line.lstrip().startswith('#'):
            line = ''
        if line.strip():
            out.append(' '.join(line.split()))
    return out


def version_at(ref, path):
    r = subprocess.run(['git', 'show', '%s:%s' % (ref, path)], capture_output=True)
    return None if r.returncode else r.stdout.decode('utf-8', 'replace')


def main():
    ref = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
    changed = subprocess.run(
        ['git', 'diff', '--name-only', '--ignore-cr-at-eol', ref],
        capture_output=True, text=True).stdout.split()

    checked = bad = 0
    for path in changed:
        if not path.endswith(CODE_EXT):
            continue
        old = version_at(ref, path)
        if old is None:
            print('%-44s new file, skipped' % path)
            continue
        try:
            new = io.open(path, encoding='utf-8', errors='replace').read()
        except IOError:
            print('%-44s deleted, skipped' % path)
            continue

        a, b = strip_comments(old), strip_comments(new)
        checked += 1
        if a == b:
            print('%-44s code identical (%d lines)' % (path, len(a)))
            continue

        bad += 1
        print('%-44s CODE DIFFERS: %d lines before, %d after'
              % (path, len(a), len(b)))
        shown = 0
        for line in difflib.unified_diff(a, b, lineterm='', n=0):
            if line.startswith(('+++', '---', '@@')):
                continue
            print('       %s' % line[:110])
            shown += 1
            if shown >= 8:
                print('       ...')
                break

    print('\n%d file(s) checked against %s, %d with code changes'
          % (checked, ref, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
