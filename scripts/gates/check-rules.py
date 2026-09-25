# -*- coding: utf-8 -*-
"""Le portail des fichiers de regles (rules*.txt) livres sur la carte.

Une ligne que `RuleStore::parseLine` refuse est ABSENTE : pas d'erreur, pas de
trace, seulement un robot qui ne reagit pas. `GET /api/rules` le montre -- sur
le fichier CHARGE, donc jamais pour le jeu d'une personnalite inactive. Ecrit a
la main deux fois pendant la revue du 09-12, ce controle a trouve six vrais
defauts ; il est ici pour ne plus dependre de quelqu'un qui y pense.

Tout ce qu'il compare est LU DANS LA SOURCE, comme check-mirrors.py : les 30
noms d'emotion (`emotionName`), les 15 danses compilees (`dances::table()`),
les champs que le firmware publie (`fields.set(...)` de main.cpp), les
plafonds (`MAX_RULES`, les regles integrees, la longueur de ligne, l'arene).
Recopier ces listes ici serait fabriquer le jumeau qui diverge.

ERREUR (le portail echoue) :
  · une ligne que le parseur jette : plus de 127 octets, moins de 7 champs,
    op inconnu, action inconnue, emotion inconnue, `set` sans cle ;
  · PlayDance vers une danse ni compilee ni livree dans sdcard/dances/ ;
  · un champ CHAINE (ip, clk, tmr) compare comme un nombre ;
  · le PIEGE DU CHAMP ABSENT : un champ que le firmware ne publie pas vaut 0,
    et une regle SANS porte dont la condition est vraie a 0 part toute seule ;
  · un seuil INATTEIGNABLE sur un champ a plage connue (light 0-100...) ;
  · un depassement SILENCIEUX du plafond de regles ou de l'arene ;
  · une personnalite qui nomme un fichier de regles absent de la carte ;
  · un champ qui ne differe d'un champ publie que par la casse, ou dont le
    nom depasse 13 caracteres (le moteur compare les 13 premiers).
NOTE (sans echec) : un champ que rien de connu ne publie -- c'est le cas
normal d'un champ a vous (`build`), pousse par POST /api/field.

    python scripts/gates/check-rules.py                 # tous les fichiers
    python scripts/gates/check-rules.py chemin/rules.txt
"""
import glob
import io
import os
import re
import sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
assert os.path.isfile(os.path.join(ROOT, 'platformio.ini')), \
    'repo root not found from ' + __file__

SD = os.path.join(ROOT, 'sdcard')
CARD_PREFIX = '/stackchan-companion/'

# Plages des champs NUMERIQUES publies par le firmware. C'est la seule table
# ecrite a la main, parce qu'une plage ne se lit pas dans un `fields.set` :
# chaque ligne dit d'ou elle vient. Un champ absent d'ici n'est simplement pas
# controle en plage. `tmr_ph` s'y ajoute a l'execution, lu dans BandTimer.h.
RANGES = {
    'batt':        (0, 100),   # battPctCached, pourcentage AXP2101
    'chg':         (0, 1),     # booleen
    'cam':         (0, 1),     # booleen
    'night':       (0, 1),     # booleen (roulette sombre)
    'mic':         (0, 1),     # booleen
    'head':        (0, 1),     # booleen (Si12T appuye)
    'dark_sleepy': (0, 1),     # booleen (option)
    'light':       (0, 100),   # Ltr553::level01From(v) * 100
    'micL':        (0, 1),     # enveloppe de crete, 0..1
    'micR':        (0, 1),
    'rssi':        (-100, 0),  # dBm, 0 hors connexion
}


def show(path):
    """Chemin relatif au depot quand c'est possible (un fichier sur un autre
    lecteur, passe en argument, n'a pas de chemin relatif sous Windows)."""
    try:
        return os.path.relpath(path, ROOT).replace(os.sep, '/')
    except ValueError:
        return path


def read(rel):
    with io.open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


# ---- les faits, lus dans la source ------------------------------------------

def source_facts():
    f = {}
    emo = read('src/engine/Emotions.h')
    blk = emo[emo.index('emotionName(eEmotions e)'):]
    blk = blk[blk.index('{', blk.index('NAMES')):blk.index('};')]
    f['emotions'] = {n.lower() for n in re.findall(r'"(\w+)"', blk)}

    dan = read('src/behavior/Dances.h')
    f['dances'] = {n.lower() for n in re.findall(r'^\s*\{\s*"([A-Za-z]\w*)"', dan, re.M)}
    f['sd_dances'] = {os.path.splitext(os.path.basename(p))[0].lower()
                      for p in glob.glob(os.path.join(SD, 'dances', '*.csv'))}

    main = read('firmware/companion/main.cpp')
    num, txt = set(), set()
    for name, args in re.findall(r'fields\.set\(\s*"(\w+)"\s*,([^;]*)\)\s*;', main):
        # set(name, float, "texte") = champ CHAINE : la regle ne lit que le float.
        (txt if args.count(',') >= 1 else num).add(name)
    f['fw_num'] = num - txt
    f['fw_str'] = txt

    bt = read('src/engine/BandTimer.h')
    ph = re.search(r'enum class Phase\s*:\s*\w+\s*\{([^}]*)\}', bt).group(1)
    RANGES['tmr_ph'] = (0, len([p for p in ph.split(',') if p.strip()]) - 1)

    # Champs pousses par le pont statusline : ce sont des sources connues.
    bridge = read('scripts/dev/claude-statusline.ps1')
    # Les paires y sont construites en liste ("claude=...", "ctx=$ctx") puis
    # jointes par '&' : on lit les cles des chaines, pas une URL litterale.
    f['bridge'] = set(re.findall(r'"(\w+)=', bridge))
    assert {'claude', 'ctx'} <= f['bridge'], 'pont statusline mal lu : ' + repr(f['bridge'])

    eng = read('src/behavior/RuleEngine.h')
    f['max_rules'] = int(re.search(r'MAX_RULES\s*=\s*(\d+)', eng).group(1))
    f['builtins'] = len(re.findall(r'\bR::Rule\s+\w+\s*;', main))

    rs = read('src/app/RuleStore.h')
    f['line_max'] = int(re.search(r'char line\[(\d+)\]', rs).group(1)) - 1
    f['arena'] = int(re.search(r'_arena\[(\d+)\]', rs).group(1))
    f['desc_max'] = int(re.search(r'DESC_MAX\s*=\s*(\d+)', rs).group(1))

    per = read('src/behavior/Personalities.h')
    f['perso_files'] = set(re.findall(r'"(/stackchan-companion/rules[^"]*\.txt)"', per))
    for y in glob.glob(os.path.join(SD, 'stackchan-companion', 'personalities', '*.yaml')):
        with io.open(y, encoding='utf-8') as fh:
            m = re.search(r'^rules:\s*(\S+)', fh.read(), re.M)
        if m:
            f['perso_files'].add(m.group(1))
    return f


# ---- l'emulation de parseLine -----------------------------------------------

OPS = {'gt': lambda a, b: a > b, 'ge': lambda a, b: a >= b,
       'lt': lambda a, b: a < b, 'le': lambda a, b: a <= b,
       'eq': lambda a, b: a == b, 'ne': lambda a, b: a != b}
ACTIONS = {'ambientdark', 'setemotion', 'playdance', 'blink', 'winkleft',
           'winkright', 'set'}


def num(s):
    """strtof : lit le prefixe numerique, 0 sinon."""
    m = re.match(r'\s*[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?', s)
    return float(m.group(0)) if m else 0.0


def split_rule(line):
    # Neuf jetons au plus, le reste de la ligne est perdu, comme en C.
    return [t.strip(' \t\r\n') for t in line.split('|')[:9]]


def reachable(op, v, lo, hi):
    """La condition peut-elle etre vraie pour une valeur dans [lo, hi] ?"""
    if op == 'gt': return v < hi
    if op == 'ge': return v <= hi
    if op == 'lt': return v > lo
    if op == 'le': return v >= lo
    if op == 'eq': return lo <= v <= hi
    return True  # ne


def check_file(path, f, set_targets):
    rel = show(path)
    with io.open(path, 'rb') as fh:
        raw_lines = fh.read().split(b'\n')
    errors, notes, rules, essential = [], [], 0, 0
    known_num = f['fw_num'] | f['bridge'] | set_targets
    lower_known = {k.lower(): k for k in f['fw_num'] | f['fw_str']}

    def err(n, msg):
        errors.append('%s:%d  %s' % (rel, n, msg))

    for n, raw in enumerate(raw_lines, 1):
        body = raw.rstrip(b'\r')
        if len(body) > f['line_max']:
            if body.strip().startswith(b'#'):
                continue  # un commentaire trop long se perd sans casser de regle
            err(n, 'ligne de %d octets > %d : JETEE entiere par RuleStore::load'
                % (len(body), f['line_max']))
            continue
        line = raw.decode('utf-8', errors='replace')
        t = line.strip(' \t\r\n')
        if not t or t.startswith('#'):
            continue
        tok = split_rule(line)
        if len(tok) < 7 or not tok[1] or not tok[2] or not tok[6]:
            err(n, 'moins de 7 champs, ou field/op/action vide : ligne ignoree')
            continue
        gate, field, op = tok[0], tok[1], tok[2].lower()
        value = num(tok[3])
        act = tok[6].lower()
        a1 = tok[7] if len(tok) > 7 else ''
        if op not in OPS:
            err(n, 'op "%s" inconnu (gt ge lt le eq ne)' % tok[2]); continue
        if act not in ACTIONS:
            err(n, 'action "%s" inconnue' % tok[6]); continue
        if act == 'setemotion' and a1.lower() not in f['emotions']:
            err(n, 'emotion "%s" inconnue : ligne ignoree' % a1); continue
        if act == 'playdance':
            if not a1:
                err(n, 'PlayDance sans nom : ligne ignoree'); continue
            if a1.lower() not in f['dances'] | f['sd_dances']:
                err(n, 'danse "%s" ni compilee ni dans sdcard/dances/ : la regle '
                       'part et ne fait RIEN' % a1)
        if act == 'set' and not a1:
            err(n, '`set` sans cle : ligne ignoree'); continue

        rules += 1
        essential += sum(len(s.encode('utf-8')) + 1 for s in (gate, field) if s)
        if act in ('set', 'playdance'):
            essential += len(a1.encode('utf-8')) + 1

        for name, role in ((field, 'champ'), (gate, 'porte')):
            if not name:
                continue
            if len(name) > 13:
                err(n, '%s "%s" : plus de 13 caracteres, le moteur n\'en compare que 13'
                    % (role, name))
            if name in f['fw_str']:
                err(n, '%s "%s" est une CHAINE : la regle lit un float toujours 0'
                    % (role, name))
            elif name not in known_num and name.lower() in lower_known:
                err(n, '%s "%s" : la casse compte, le firmware publie "%s"'
                    % (role, name, lower_known[name.lower()]))
            elif name not in known_num:
                notes.append('%s:%d  %s "%s" : publie par rien de connu (champ a vous ?)'
                             % (rel, n, role, name))

        # Le piege du champ absent : vrai a 0 et rien pour l'endormir.
        if field not in f['fw_num'] and not gate and OPS[op](0.0, value):
            err(n, 'champ "%s" non publie par le firmware, lu a 0 : condition vraie '
                   'a 0 et aucune porte, la regle part toute seule' % field)

        if field in RANGES:
            lo, hi = RANGES[field]
            if not reachable(op, value, lo, hi):
                err(n, 'seuil inatteignable : %s %s %g, or %s vit dans [%g, %g]'
                    % (field, op, value, field, lo, hi))

    cap = f['max_rules'] - f['builtins']
    if rules > cap:
        errors.append('%s  %d regles actives, %d places (%d moins %d integrees) : '
                      'les %d dernieres sont refusees EN SILENCE'
                      % (rel, rules, cap, f['max_rules'], f['builtins'], rules - cap))
    # Estimation HAUTE pour les descriptions (DESC_MAX chacune) : elle ne sert
    # qu'a une note, l'erreur ne porte que sur les noms indispensables.
    described = essential + rules * f['desc_max']
    if essential > f['arena']:
        errors.append('%s  %d octets de noms pour une arene de %d : des regles perdent '
                      'leur champ' % (rel, essential, f['arena']))
    elif described > f['arena']:
        notes.append('%s  jusqu\'a %d octets avec descriptions (arene %d) : les '
                     'dernieres regles peuvent s\'afficher sans description'
                     % (rel, described, f['arena']))
    return rules, errors, notes


def main(argv):
    f = source_facts()
    problems = []
    # Des gardes sur l'EXTRACTION : un portail dont la table est vide ne
    # refuse plus rien et dit quand meme ok.
    if len(f['emotions']) != 30:
        problems.append('TABLE     %d emotions extraites, 30 attendues' % len(f['emotions']))
    if len(f['dances']) < 15:
        problems.append('TABLE     %d danses extraites, au moins 15' % len(f['dances']))
    if len(f['fw_num']) < 8 or not f['fw_str']:
        problems.append('TABLE     champs du firmware mal extraits (%d / %d)'
                        % (len(f['fw_num']), len(f['fw_str'])))
    if f['builtins'] < 1:
        problems.append('TABLE     regles integrees introuvables dans main.cpp')

    if argv:
        files = [os.path.abspath(a) for a in argv]
    else:
        files = sorted(glob.glob(os.path.join(SD, 'stackchan-companion', 'rules*.txt*')))
        for p in sorted(f['perso_files']):
            if not p.startswith(CARD_PREFIX):
                problems.append('PERSO     fichier de regles hors de %s : %s' % (CARD_PREFIX, p))
            elif not os.path.isfile(os.path.join(SD, 'stackchan-companion',
                                                 p[len(CARD_PREFIX):])):
                problems.append('PERSO     %s nomme par une personnalite, absent de '
                                'sdcard/' % p)

    # Un champ ecrit par `set` dans N'IMPORTE QUEL fichier est une source.
    set_targets = set()
    for p in files:
        with io.open(p, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                t = split_rule(line)
                if not line.lstrip().startswith('#') and len(t) > 7 \
                        and t[6].lower() == 'set':
                    set_targets.add(t[7])

    total = 0
    for p in files:
        n, errs, notes = check_file(p, f, set_targets)
        total += n
        problems += ['ERREUR    ' + e for e in errs]
        for x in notes:
            print('note      ' + x)
        print('%-9s %s : %d regle(s)' % ('ok' if not errs else 'ECHEC',
              show(p), n))
    for x in problems:
        print(x)
    print('%d fichier(s), %d regle(s) verifiee(s), %d probleme(s)'
          % (len(files), total, len(problems)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
