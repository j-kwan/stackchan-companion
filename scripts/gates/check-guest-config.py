# -*- coding: utf-8 -*-
"""Le portail de l'ALLER-RETOUR des reglages d'un bin invite.

POURQUOI IL EXISTE. Un reglage d'invite traverse quatre listes ecrites a la
main dans le meme fichier : `addSetting` le declare sur la page /config,
`settingGet` l'affiche, `settingSet` l'accepte, et `saveConfig` l'ecrit sur la
carte. Les quatre doivent contenir la MEME cle, et rien ne le verifiait.

La quatrieme est la dangereuse : `saveConfig` TRONQUE le fichier et le
reecrit depuis sa propre liste. Une cle que `loadConfig` sait lire mais que
`saveConfig` n'ecrit pas n'est pas seulement non persistee -- elle est
DETRUITE a la premiere sauvegarde faite depuis la page web, y compris quand
elle avait ete ecrite a la main sur la carte. C'est la panne du 08-05
(`auto_bright`), et le commentaire qui la raconte est toujours dans
firmware/space/main.cpp : la lecon a ete ecrite, pas outillee.

Le companion a le portail symetrique (`check-console.py` : chaque cle Tuning
doit avoir un controle dans la console). Les invites n'en avaient aucun.

CE QU'IL VERIFIE, par bin :
  1. toute cle declaree est LISIBLE      (sinon le champ s'affiche vide)
  2. toute cle declaree est ACCEPTEE     (sinon la saisie est jetee en silence)
  3. toute cle declaree est SAUVEGARDEE  (sinon elle meurt au redemarrage)
  4. toute cle RELUE du yaml est sauvegardee (sinon la sauvegarde la detruit)
  5. le nombre de champs tient sous MAX_SETTINGS, lu dans SceGuest.h

Les exceptions sont NOMMEES ici, jamais filtrees en silence : une cle exclue
sans raison ecrite est la maniere dont un portail cesse de tenir.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

BINS = ('flight-radar', 'space', 'ha-remote')

# ---------------------------------------------------------------------------
# LES BINS DONT LES QUATRE LISTES SONT GENEREES, et pourquoi ils sont verifies
# AUTREMENT plutot qu'ajoutes a BINS ci-dessus.
#
# `led-fluid` ne contient aucun `addSetting("cle"` litteral : il boucle sur une
# TABLE (ui::params(), firmware/led-fluid/panels.h) pour declarer la page,
# dessiner le panneau, ecrire le yaml et le relire. Les quatre listes ne
# peuvent donc pas diverger -- c'est la conception que ce portail reclame,
# poussee jusqu'au bout.
#
# L'inscrire dans BINS le ferait PASSER EN NE VERIFIANT RIEN : les regexes
# ci-dessous ne trouveraient aucune cle declaree, la boucle tournerait a vide
# et la ligne finale annoncerait « ok 0 champs ». Un portail vert qui n'a rien
# regarde est pire que pas de portail, et c'est exactement le silence que ce
# fichier existe pour casser.
#
# Ce qui RESTE faillible dans ce bin, c'est le pont entre la table et l'etat
# vivant : `paramValue` et `paramApply` sont deux chaines de strcmp ecrites a
# la main, une branche par ligne de la table. Une ligne ajoutee sans sa branche
# donne un champ qui s'affiche a sa valeur par defaut et une saisie jetee en
# silence -- la meme panne, un etage plus bas. C'est CELA qui est verifie.
# ---------------------------------------------------------------------------
TABLE_BINS = {
    'led-fluid': ('firmware/led-fluid/panels.h', 'firmware/led-fluid/main.cpp'),
}

# ---------------------------------------------------------------------------
# LES EXCEPTIONS, une par ligne, avec sa raison. Rien d'autre n'est tolere.
# ---------------------------------------------------------------------------
# Cles LISIBLES mais jamais declarees : des diagnostics en lecture seule que la
# page affiche sans champ de saisie. Le sens va dans un seul sens par
# construction, donc les tests 2 et 3 ne les concernent pas.
READ_ONLY = {
    'space': {'diag'},
    'flight-radar': set(),
    'ha-remote': set(),
}
# Cles que `saveConfig` n'ecrit VOLONTAIREMENT pas. Une entree ici doit dire
# pourquoi -- "on a oublie" n'est pas une raison, c'est precisement la panne.
NOT_SAVED = {
    # ALIAS accepte a la lecture, jamais reecrit : la sauvegarde regenere le
    # fichier sous le nom canonique `launch_poll_min`, donc la VALEUR survit
    # et seule l'orthographe change. Rien n'est detruit.
    'space': {'ll2_poll_min'},
    # `track` n'est PAS un reglage, c'est une COMMANDE : l'indicatif suivi.
    # Le firmware le reecrit lui-meme depuis quatre endroits, la page ne le
    # renvoie jamais en echo (settingGet rend ""), et le persister ferait
    # rejouer au demarrage une poursuite que personne n'a demandee.
    'flight-radar': {'track'},
}


def read(rel):
    with io.open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as f:
        return f.read()


def block(src, anchor_re):
    """Le corps qui suit l'ancre, delimite par comptage d'accolades.

    Decouper au prochain `}` naif attraperait la premiere accolade fermante
    venue (un `if`, une lambda imbriquee) et le portail verifierait un
    fragment en croyant lire la fonction.

    La DECLARATION AVANCEE est sautee : `static bool saveConfigSd();` se
    trouve avant la definition dans flight-radar, et s'arreter dessus faisait
    lire au portail un corps qui n'etait pas le sien -- il annoncait alors
    zero cle sauvegardee pour un bin qui en sauvegarde vingt-deux. Un portail
    qui se trompe de corps accuse tout, ce qui revient a n'accuser rien.
    """
    i = -1
    for m in re.finditer(anchor_re, src):
        j = src.find('{', m.end() - 1)
        k = src.find(';', m.end() - 1)
        if j >= 0 and (k < 0 or j < k):
            i = j
            break
    if i < 0:
        return None
    depth, j = 0, i
    while j < len(src):
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
        j += 1
    return src[i:]


def keys_strcmp(body):
    """Les cles reconnues par une chaine de `strcmp(k, "cle")`."""
    if not body:
        return set()
    return set(re.findall(r'strcmp\s*\(\s*\w+\s*,\s*"([^"]+)"\s*\)', body))


def keys_delegated(src, body):
    """Les cles d'une fonction a qui le corps DELEGUE la reconnaissance.

    `space` ne repete pas sa chaine de strcmp dans settingSet : il appelle
    `cfgApply(k, v.c_str())`, c'est-a-dire l'analyseur du yaml, de sorte que
    la page web et la carte reconnaissent les memes cles par construction --
    la meilleure des trois conceptions, et celle que ce portail ne doit
    surtout pas punir. On suit donc tout appel de la forme `foo(k, ...)`.
    """
    if not body:
        return set()
    out = set()
    for fn in set(re.findall(r'\b(\w+)\s*\(\s*k\s*,', body)):
        if fn in ('strcmp', 'strncmp', 'snprintf', 'strlcpy'):
            continue
        out |= keys_strcmp(block(src, r'\b\w[\w:<>&*\s]*\b' + fn + r'\s*\('))
    return out


def keys_written(body):
    """Les cles qu'une fonction de sauvegarde ecrit.

    Une seule `f.printf` peut en porter PLUSIEURS ("host: %s\\nport: %u\\n"),
    et ha-remote le fait : ne lire que la premiere cle de chaque appel
    declarait perdus `port`, `ssl` et `brightness`, qui sont ecrits juste a
    cote. On lit donc le format entier, chaque cle etant en debut de ligne.
    """
    if not body:
        return set()
    out = set()
    for call in re.findall(r'f\.printf\s*\((.*?)\)\s*;', body, re.S):
        lit = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', call))
        for k in re.findall(r'(?:^|\\n)\s*([A-Za-z_]\w*)\s*:', lit):
            out.add(k)
    return out


def main():
    problems = 0
    checked = 0

    guest_h = read('src/guest/SceGuest.h')
    m = re.search(r'MAX_SETTINGS\s*=\s*(\d+)', guest_h)
    if not m:
        print('MAX_SETTINGS introuvable dans src/guest/SceGuest.h')
        return 1
    max_settings = int(m.group(1))

    for b in BINS:
        rel = 'firmware/%s/main.cpp' % b
        src = read(rel)

        declared = re.findall(r'addSetting\s*\(\s*"([^"]+)"', src)
        get_body = block(src, r'settingGet\s*=\s*\[')
        set_body = block(src, r'settingSet\s*=\s*\[')
        readable = keys_strcmp(get_body) | keys_delegated(src, get_body) \
            | keys_strcmp(block(src, r'\bstatic\s+String\s+settingGet\s*\('))
        writable = keys_strcmp(set_body) | keys_delegated(src, set_body)
        # La sauvegarde : `f.printf("cle: ...")`, quel que soit le nom de la
        # fonction (saveConfig ici, saveConfigSd ailleurs).
        saved = keys_written(block(src, r'\bstatic\s+bool\s+saveConfig\w*\s*\('))
        # La relecture : cfgApply pour space, le corps de loadConfig ailleurs.
        loaded = keys_strcmp(block(src, r'\bstatic\s+bool\s+cfgApply\s*\(')) \
            | keys_strcmp(block(src, r'\bstatic\s+void\s+loadConfig\w*\s*\('))

        ro = READ_ONLY.get(b, set())
        skip_save = NOT_SAVED.get(b, set())

        def fail(fmt, *a):
            print(('%-13s ' % b) + (fmt % a))

        for k in declared:
            checked += 1
            if k not in readable:
                fail('ILLISIBLE   %s : declaree par addSetting, absente de '
                     'settingGet -> le champ s affiche VIDE', k)
                problems += 1
            # Une cle EN LECTURE SEULE n'a ni saisie a accepter ni valeur a
            # persister : les deux tests suivants ne la concernent pas.
            if k in ro:
                continue
            if k not in writable:
                fail('IGNOREE     %s : declaree par addSetting, absente de '
                     'settingSet -> la saisie est jetee en silence', k)
                problems += 1
            if k not in saved and k not in skip_save:
                fail('NON SAUVEE  %s : declaree par addSetting, absente de '
                     'saveConfig -> perdue au redemarrage', k)
                problems += 1

        # LA CLE DETRUITE. Relue de la carte, jamais reecrite : la premiere
        # sauvegarde depuis /config tronque le fichier et l emporte.
        for k in sorted(loaded - saved - skip_save):
            checked += 1
            fail('DETRUITE    %s : relue par le yaml, absente de saveConfig '
                 '-> la sauvegarde TRONQUE le fichier et la supprime', k)
            problems += 1

        n = len(declared)
        checked += 1
        if n > max_settings:
            fail('PLAFOND     %d champs pour MAX_SETTINGS=%d -> les derniers '
                 'sont perdus', n, max_settings)
            problems += 1
        elif n == max_settings:
            # Pas une erreur, mais le prochain champ ajoute disparaitra en
            # n etant annonce que par une ligne de console que personne ne lit.
            fail('PLEIN       %d champs sur %d : le prochain sera perdu',
                 n, max_settings)
            problems += 1
        else:
            print('%-13s ok %2d champs (max %d), %d lisibles, %d sauvees'
                  % (b, n, max_settings, len(readable), len(saved)))

    # -----------------------------------------------------------------------
    # Les bins a table : la table EST la declaration, le pont vers l etat
    # vivant est ce qui peut encore manquer une ligne (voir TABLE_BINS).
    # -----------------------------------------------------------------------
    for b, (table_rel, main_rel) in sorted(TABLE_BINS.items()):
        table_src = read(table_rel)
        src = read(main_rel)

        # Les lignes de la table : { "cle", "English", "Francais", Kind::...
        keys = re.findall(r'\{\s*"([A-Za-z_]\w*)"\s*,\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*Kind::',
                          table_src)
        m_count = re.search(r'paramCount\s*\(\)\s*\{\s*return\s+(\d+)\s*;', table_src)

        def fail_t(fmt, *a):
            print(('%-13s ' % b) + (fmt % a))

        if not keys:
            fail_t('TABLE VIDE  aucune ligne reconnue dans %s -> le portail ne '
                   'verifie plus rien', table_rel)
            problems += 1
            continue

        # Le compte annonce doit suivre la table : paramCount() borne toutes
        # les boucles, donc une ligne ajoutee sans lui est une ligne que
        # PERSONNE ne lit -- ni la page, ni le panneau, ni le yaml.
        checked += 1
        if not m_count:
            fail_t('COMPTE      paramCount() introuvable dans %s', table_rel)
            problems += 1
        elif int(m_count.group(1)) != len(keys):
            fail_t('COMPTE      paramCount()=%s pour %d lignes de table -> les '
                   'lignes au-dela sont invisibles partout', m_count.group(1), len(keys))
            problems += 1

        readable = keys_strcmp(block(src, r'\bstatic\s+int\s+paramValue\s*\('))
        writable = keys_strcmp(block(src, r'\bstatic\s+void\s+paramApply\s*\('))
        for k in keys:
            checked += 1
            if k not in readable:
                fail_t('ILLISIBLE   %s : dans la table, absente de paramValue '
                       '-> le champ affiche sa valeur par defaut', k)
                problems += 1
            if k not in writable:
                fail_t('IGNOREE     %s : dans la table, absente de paramApply '
                       '-> la saisie est jetee en silence', k)
                problems += 1

        checked += 1
        if len(keys) > max_settings:
            fail_t('PLAFOND     %d champs pour MAX_SETTINGS=%d -> les derniers '
                   'sont perdus', len(keys), max_settings)
            problems += 1
        else:
            print('%-13s ok %2d champs de table (max %d), lus et acceptes'
                  % (b, len(keys), max_settings))

    print('%d point(s) verifie(s), %d probleme(s)' % (checked, problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
