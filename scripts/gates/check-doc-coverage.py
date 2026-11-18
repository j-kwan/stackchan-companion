# -*- coding: utf-8 -*-
"""Le portail des documents qui ENUMERENT quelque chose que le code definit.

Deux listes, meme faiblesse : elles sont ecrites a la main a cote d'une source
de verite qui, elle, bouge.

  · docs/reference/API.md doit citer les 44 couples methode+chemin que
    `WebApi::route()` enregistre reellement. Une route ajoutee sans ligne ici
    est une route que personne ne trouvera autrement qu'en lisant le C++ ; une
    ligne ici sans route derriere est pire, elle promet une API qui n'existe
    pas.
  · docs/reference/EMOTIONS.md doit citer les 30 valeurs de `eEmotions`. Ce
    sont les noms qu'acceptent POST /api/emotion, une action de rules.txt et
    la colonne `emotion` d'une danse : la liste EST l'interface.
  · docs/reference/CHOREGRAPHIES.md doit citer les 15 danses integrees de
    `dances::table()`. Meme raison : ce sont les noms que prend
    POST /api/dance, et une danse ajoutee sans ligne dans le tableau est une
    danse que personne ne saura jouer.

Les deux sens sont verifies, parce que les deux se trompent differemment.

Ce portail ne verifie PAS les descriptions -- personne ne peut. Il verifie que
rien ne manque et que rien n'est invente, ce qui est exactement ce qui pourrit
tout seul.
"""
import io
import os
import re
import sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
assert os.path.isfile(os.path.join(ROOT, 'platformio.ini')), \
    'repo root not found from ' + __file__


def read(rel):
    with io.open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


def check_api():
    """Les couples (methode, chemin) du routeur, face au tableau du document."""
    src = read('src/app/WebApi.h')
    code = set()
    for m in re.finditer(r'route\("([^"]+)",\s*HTTP_(\w+)', src):
        code.add((m.group(2).upper(), m.group(1)))

    doc = read('docs/reference/API.md')
    listed = set()
    # `| \`/api/x\` | GET | ... |` -- la methode dans sa propre colonne, pour
    # qu'un chemin servi par deux verbes ait bien DEUX lignes.
    for m in re.finditer(r'^\|\s*`([^`]+)`\s*\|\s*(GET|POST|DELETE)\s*\|', doc, re.M):
        listed.add((m.group(2), m.group(1)))

    problems = 0
    for meth, path in sorted(code - listed):
        print('ABSENTE   %-6s %-24s servie par le firmware, absente de API.md'
              % (meth, path))
        problems += 1
    for meth, path in sorted(listed - code):
        print('FANTOME   %-6s %-24s citee par API.md, aucune route derriere'
              % (meth, path))
        problems += 1
    if not problems:
        print('ok        API.md : %d route(s) methode+chemin' % len(code))
    return problems, len(code)


def check_emotions():
    """Les valeurs de l'enum, face au catalogue."""
    src = read('src/engine/Emotions.h')
    blk = src[src.index('enum eEmotions'):src.index('EMOTIONS_COUNT')]
    code = set(re.findall(r'^\s{4}([A-Z]\w*)\s*=\s*\d+', blk, re.M))

    doc = read('docs/reference/EMOTIONS.md')
    listed = set(re.findall(r'^\|\s*`(\w+)`\s*\|', doc, re.M))

    problems = 0
    for e in sorted(code - listed):
        print('ABSENTE   %-14s dans eEmotions, absente de EMOTIONS.md' % e)
        problems += 1
    for e in sorted(listed - code):
        print('FANTOME   %-14s citee par EMOTIONS.md, absente de eEmotions' % e)
        problems += 1
    if not problems:
        print('ok        EMOTIONS.md : %d expression(s)' % len(code))
    return problems, len(code)


def check_dances():
    """Les 15 danses integrees, face au tableau du §5."""
    src = read('src/behavior/Dances.h')
    code = set(re.findall(r'^\s*\{\s*"([A-Za-z]\w*)"', src, re.M))

    doc = read('docs/reference/CHOREGRAPHIES.md')
    listed = set(re.findall(r'^\|\s*`(\w+)`\s*\|', doc, re.M))

    problems = 0
    for d in sorted(code - listed):
        print('ABSENTE   %-14s dans dances::table(), absente de CHOREGRAPHIES.md' % d)
        problems += 1
    for d in sorted(listed - code):
        print('FANTOME   %-14s citee par CHOREGRAPHIES.md, aucune danse derriere' % d)
        problems += 1
    if not problems:
        print('ok        CHOREGRAPHIES.md : %d danse(s)' % len(code))
    return problems, len(code)


def check_docmap():
    """Les sous-repertoires de docs/, face au diagramme mermaid de docs/README.md.

    docs/hardware/ et docs/personalities/ ont existe SANS noeud dans ce
    diagramme depuis le premier commit visible de cette branche -- chacun a sa
    propre section en prose plus bas dans le fichier, jamais reliee au schema
    que la table des matieres promet de resumer. `assets/` est exclu : ce
    n'est pas une categorie de documentation, seulement des images.
    """
    subdirs = set(
        d for d in os.listdir(os.path.join(ROOT, 'docs'))
        if os.path.isdir(os.path.join(ROOT, 'docs', d)) and d != 'assets'
    )

    problems = 0
    for rel in ('docs/README.md', 'docs/README.fr.md'):
        doc = read(rel)
        mermaid = doc[doc.index('```mermaid'):doc.index('```', doc.index('```mermaid') + 3)]
        for d in sorted(subdirs):
            if (d + '/') not in mermaid:
                print('ABSENT    %-14s dans le diagramme mermaid de %s' % (d, rel))
                problems += 1
    if not problems:
        print('ok        docs/README.md(.fr) : %d repertoire(s)' % len(subdirs))
    return problems, len(subdirs)


def main():
    p1, n1 = check_api()
    p2, n2 = check_emotions()
    p3, n3 = check_dances()
    p4, n4 = check_docmap()
    # Des PLANCHERS, pas des egalites : la liste grandit, et un portail qu'il
    # faut reajuster a chaque ajout finit par etre ajuste sans etre lu. En
    # dessous, c'est que l'extraction a casse, pas que le projet a maigri.
    problems = p1 + p2 + p3 + p4
    if n1 < 40:
        print('TABLE     %d routes extraites, au moins 40 attendues' % n1)
        problems += 1
    if n2 != 30:
        print('TABLE     %d emotions extraites, 30 attendues' % n2)
        problems += 1
    if n3 < 15:
        print('TABLE     %d danses extraites, au moins 15 attendues' % n3)
        problems += 1
    print('%d point(s) verifie(s), %d probleme(s)' % (n1 + n2 + n3 + n4, problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
