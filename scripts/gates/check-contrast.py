# -*- coding: utf-8 -*-
"""Verifie les contrastes RGAA / WCAG 2.1 AA des themes de flight-radar.

    python scripts/gates/check-contrast.py          # verdict par theme
    python scripts/gates/check-contrast.py -v       # detail canal par canal

Lit la table THEMES[] DANS LA SOURCE : le controle suit le code au lieu de
recopier des valeurs qui divergeront. Sortie non nulle si un seuil casse,
pour un branchement en CI.

Seuils appliques (WCAG 2.1 AA) :
  - 1.4.3 Contraste minimum ...... texte >= 4.5:1
  - 1.4.11 Contraste non-texte ... objet graphique PORTEUR DE SENS >= 3:1
  - les elements purement DECORATIFS (subdivision d'anneaux, trainee des
    blips, filets de separation) sont exemptes par 1.4.11 lui-meme : leur
    information est portee ailleurs (etiquettes de distance, blip, mise en
    page). Ils sont listes tels quels, jamais comptes en echec.

Precision : les couleurs sont evaluees APRES quantification RGB565 et
decodage par replication de bits, exactement comme le panneau les affiche —
une palette calee en RGB888 ne dit pas la verite sur cet ecran.

Le bandeau de notification est verifie A PART (voir BANNER_* plus bas) : il
est PLEIN, donc c'est du noir SUR la couleur du message, pas la couleur du
message sur le fond du panneau.
"""
import re, sys, os

FIELDS = ["ring1", "ring2", "ringTxt", "cs", "selCol", "trail", "panelBg",
          "sep", "txtMain", "txt1", "txt2", "hint", "title", "accent",
          "alert", "altG", "altLow", "altMid", "altCruise", "milCol"]

KIND = {"ring1": "graph", "ring2": "deco", "ringTxt": "texte", "cs": "texte",
        "selCol": "graph", "trail": "deco", "panelBg": None, "sep": "deco",
        "txtMain": "texte", "txt1": "texte", "txt2": "texte", "hint": "texte",
        "title": "texte", "accent": "texte", "alert": "texte",
        "altG": "graph", "altLow": "graph", "altMid": "graph",
        "altCruise": "graph", "milCol": "graph"}

# Canaux dessines SUR LE PANNEAU ; les autres le sont sur le fond noir.
ON_PANEL = {"txtMain", "txt1", "txt2", "hint", "title", "accent", "alert", "sep"}
NEED = {"texte": 4.5, "graph": 3.0, "deco": 0.0}

# --- Notification banner: SOLID fill, BLACK text centred on the message -----
# The banner used to be coloured text on the black background; it is now a
# full-width strip FILLED with the message colour, the label printed in BLACK.
# That flips the pair being judged: the contrast that matters is black vs
# `accent` / `alert` themselves, NOT those channels vs the panel background
# checked above -- so the banner gets its own pass instead of a KIND entry.
#
# Threshold: this is TEXT, so WCAG 1.4.3 applies at 4.5:1 -- for the FOUR
# themes alike. The doctrine of this script has never carried a per-theme
# relaxation (the "Night kept at >= 3:1" note lives in main.cpp and covers the
# DIM channels drawn on black, where night vision argues for less light). A
# black-on-bright banner is bright BY CONSTRUCTION: nothing about night vision
# is preserved by lowering the bar here, so Night gets no exception. Should a
# night banner prove too luminous, the fix is to darken/shrink the banner, not
# to accept unreadable text.
BANNER_FG = 0x0000                    # TFT_BLACK
BANNER_FIELDS = ["accent", "alert"]   # the two possible banner backgrounds
BANNER_NEED = 4.5

NAMED = {"TFT_WHITE": 0xFFFF, "TFT_GREEN": 0x07E0, "TFT_ORANGE": 0xFDA0,
         "TFT_YELLOW": 0xFFE0, "TFT_DARKGREY": 0x7BEF, "TFT_RED": 0xF800,
         "TFT_CYAN": 0x07FF, "TFT_BLACK": 0x0000}


def rgb565_to_888(c):
    """Decodage du panneau : bits repliques, pas un simple decalage."""
    r5, g6, b5 = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def luminance(rgb):
    """WCAG : courbe sRGB PAR MORCEAUX (un ^2.2 approche donne un faux verdict)."""
    lin = []
    for v in rgb:
        s = v / 255.0
        lin.append(s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4)
    return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]


def ratio(fg, bg):
    a, b = luminance(rgb565_to_888(fg)), luminance(rgb565_to_888(bg))
    return (max(a, b) + 0.05) / (min(a, b) + 0.05)


def parse_themes(path, fields):
    src = open(path, encoding="utf-8").read()
    blk = src[src.index("static const Theme THEMES["):
              src.index("static const Theme* TH =")]
    blk = re.sub(r"//.*", "", blk)                 # commentaires : hex parasites
    for name, val in NAMED.items():
        blk = blk.replace(name, "0x%04X" % val)
    vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{4}", blk)]
    if not vals or len(vals) % len(fields):
        sys.exit("table THEMES illisible dans %s : %d valeurs pour %d champs"
                 % (path, len(vals), len(fields)))
    return [dict(zip(fields, vals[i:i + len(fields)]))
            for i in range(0, len(vals), len(fields))]


# =============================================================================
# THE SPACE BIN — its own palette, and it had never been checked
# =============================================================================
# This gate existed and read ONE file. `firmware/space/main.cpp` carries a
# second `THEMES[]` with a different field list, so it was never parsed — and
# `hint`, which that bin uses for real INFORMATION (the "waiting for the clock"
# splash shown at size 2 on every boot, the PASSES radio chip, every sky-dome
# caption, the TBC/TBD launch chip), sat at 2.78:1 on Deep and 1.66:1 on Night.
# A gate that covers one of two palettes is a gate that reports CONFORME about
# half the product.
#
# The coastlines are why this needs a PER-FIELD background rather than the
# radar's "panel or black": they are drawn over the map's own day and night
# fills, and judging them against black would flatter them.
SPACE_FIELDS = ["bg", "panelBg", "txtMain", "txt1", "txt2", "hint", "accent",
                "accent2", "alert", "sep", "bgDay", "bgNight", "coastDay",
                "coastNight", "stripEye", "stripAid", "stripOff"]

SPACE_KIND = {
    "bg": None, "panelBg": None, "bgDay": None, "bgNight": None,
    "txtMain": "texte", "txt1": "texte", "txt2": "texte", "hint": "texte",
    "accent": "texte", "accent2": "texte", "alert": "texte",
    "sep": "deco",
    "coastDay": "graph", "coastNight": "graph",
    # The strip's three tints ARE the information (what it would take to see
    # this object right now), so they are judged as graphics — `stripOff`
    # included: main.cpp's own comment says an object below the horizon must
    # stay findable in the row.
    "stripEye": "graph", "stripAid": "graph", "stripOff": "graph",
}

# field -> the field it is drawn ON. Absent = the black background.
SPACE_BG = {
    "txtMain": "panelBg", "txt1": "panelBg", "txt2": "panelBg",
    "hint": "panelBg", "accent": "panelBg", "accent2": "panelBg",
    "alert": "panelBg", "sep": "panelBg",
    "coastDay": "bgDay", "coastNight": "bgNight",
}


def check_space(root, verbose):
    path = os.path.join(root, "firmware", "space", "main.cpp")
    themes = parse_themes(path, SPACE_FIELDS)
    names = (["Deep", "Night (ambre)"]
             + ["theme %d" % i for i in range(2, len(themes))])[:len(themes)]
    failed = False
    print("")
    print("Bin space - palettes :")
    for name, d in zip(names, themes):
        bad, worst = [], (None, 99.0)
        for f in SPACE_FIELDS:
            k = SPACE_KIND[f]
            if k is None:
                continue
            bg = d[SPACE_BG[f]] if f in SPACE_BG else 0x0000
            cr = ratio(d[f], bg)
            if k != "deco":
                if cr < worst[1]:
                    worst = (f, cr)
                if cr < NEED[k]:
                    bad.append((f, cr, NEED[k]))
            if verbose:
                print("  %-11s 0x%04X  %5.2f:1  %-6s %s"
                      % (f, d[f], cr, k,
                         "ok" if k == "deco" or cr >= NEED[k] else "ECHEC"))
        print("  %-14s %s  (plus faible non decoratif : %s %.2f:1)"
              % (name, "CONFORME" if not bad else "NON CONFORME",
                 worst[0], worst[1]))
        for f, cr, need in bad:
            print("     ECHEC %s %.2f:1 < %.1f:1" % (f, cr, need))
        failed |= bool(bad)
    return failed


# ---------------------------------------------------------------------------
# The "Liquid Glass" palette: the launcher, the guest lobby, ha-remote.
#
# It was OUTSIDE this gate entirely. Three files state "RGAA >= 4.5:1" in a
# comment beside their secondary-text colour and nothing measured it -- an
# accessibility claim is worth exactly what verifies it. check-mirrors.py holds
# the three copies to each other; this pass says whether the shared value is
# readable in the first place.
#
# The background is not black here but the CARD: these are text colours drawn
# on the rounded panels the design is named after, and judging them against
# black would flatter every one of them.
GLASS_KIND = {"ACC": "texte", "ACC2": "texte", "MUT": "texte", "DIM": "deco",
              "KO": "texte", "BORD": "deco"}
# DIM is decorative BY NAME and by use: it is the "off"/disabled ink, where a
# low contrast is the message. BORD is a card outline, its information carried
# by the fill it surrounds (WCAG 1.4.11 exempts it for the same reason the
# radar's ring subdivisions are exempt).


def check_glass(root, verbose):
    path = os.path.join(root, "src", "app", "Launcher.h")
    src = open(path, encoding="utf-8").read()
    val = {}
    for name in list(GLASS_KIND) + ["CARD"]:
        m = re.search(r"C_" + name + r"\s*=\s*0x([0-9A-Fa-f]{4})", src)
        if m:
            val[name] = int(m.group(1), 16)
    missing = [n for n in list(GLASS_KIND) + ["CARD"] if n not in val]
    print("")
    print("Palette Liquid Glass (launcher / lobby invite / ha-remote),"
          " sur la carte :")
    if missing:
        print("  ILLISIBLE : %s absente(s) de Launcher.h" % ", ".join(missing))
        return True
    bg = val["CARD"]
    bad = []
    for name, kind in sorted(GLASS_KIND.items()):
        cr = ratio(val[name], bg)
        ok = kind == "deco" or cr >= NEED[kind]
        if not ok:
            bad.append((name, cr, NEED[kind]))
        if verbose or not ok:
            print("  C_%-5s 0x%04X  %5.2f:1  %-6s %s"
                  % (name, val[name], cr, kind,
                     "exempt" if kind == "deco" else ("ok" if ok else "ECHEC")))
    print("  %-14s %s" % ("verdict", "CONFORME" if not bad else "NON CONFORME"))
    for name, cr, need in bad:
        print("     ECHEC C_%s %.2f:1 < %.1f:1" % (name, cr, need))
    return bool(bad)


# ---------------------------------------------------------------------------
# THE EMBEDDED WEB CONSOLE, and its per-personality skins.
#
# It was outside this gate for the same reason the Liquid Glass palette was:
# nobody had measured it. That mattered little while there was ONE palette
# nobody edited. It stops being harmless the moment a personality can pick a
# SKIN -- each new theme multiplies an unverified surface, and an unreadable
# console is the one failure you cannot fix from the console.
#
# TRUE COLOUR, NOT RGB565: this is a browser, so the panel quantisation that
# governs every other check here does not apply. The ratio is computed on the
# 888 values as written.
#
# THE BACKGROUND IS THE PANEL, NOT THE PAGE. Text sits on `--glass`, a veil at
# a few percent over the solid body -- judging it against the raw body colour
# would flatter every ink by a few tenths. The veil is composited PER VARIANT
# (never a fixed constant), because a theme's light face inverts which one is
# darker: the veil tints away from the body's own colour either way, so a
# constant tuned for one direction would silently stop describing the other.
CONSOLE_KIND = {"txt": "texte", "mut": "texte", "acc": "texte",
                "acc2": "texte", "ok": "texte", "ko": "texte"}
# `--acc`/`--acc2` are TEXT: they ink the active tab label, the section titles
# and the link colour -- not only the gradient they are named for.
#
# `--bord`/`--bord2` are deliberately ABSENT rather than listed as decorative:
# they are `rgba()` veils, not hex inks, so they would need compositing to be
# measured at all -- and the answer would then be discarded, since a panel
# outline is exempt under 1.4.11 exactly as the radar's ring subdivisions are.
# Measuring something to ignore the result is how a gate acquires the habit of
# printing numbers nobody reads.


def _hex888(s):
    s = s.lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def _token888(tok):
    """A declared value that is either `#hex` or `rgba(r,g,b[,a])`, returned as
    (r,g,b,a) -- alpha defaults to 1.0 for hex, since every text ink in this
    file is opaque and only the veils/borders are ever translucent."""
    tok = tok.strip()
    if tok.startswith("#"):
        return _hex888(tok) + (1.0,)
    m = re.match(r"rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)"
                 r"\s*(?:,\s*([\d.]+)\s*)?\)", tok)
    r, g, b = int(float(m.group(1))), int(float(m.group(2))), int(float(m.group(3)))
    a = float(m.group(4)) if m.group(4) is not None else 1.0
    return (r, g, b, a)


def _over(fg_rgba, bg):
    """Composite a translucent veil over an opaque background."""
    a = fg_rgba[3]
    return tuple(int(round(fg_rgba[i] * a + bg[i] * (1 - a))) for i in range(3))


def ratio888(fg, bg):
    a, b = luminance(fg), luminance(bg)
    return (max(a, b) + 0.05) / (min(a, b) + 0.05)


def _find_block(src, i):
    """`i` points just past an opening `{`. Returns (inner text, index past the
    matching closing `}`) -- brace-COUNTING, not `str.index("}", i)`: the media
    blocks nest a `:root{...}` inside `@media(...){...}`, and the naive version
    returns at the FIRST `}`, which is the inner rule's, not the outer query's."""
    depth, j = 1, i
    while depth:
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
        j += 1
    return src[i:j - 1], j


def _props(text):
    """Every `--name: colour` this block declares, ignoring anything else (a
    gradient, a `var()` reference, a comment) -- the regex simply does not
    match those, so scanning the whole block rather than statement-by-statement
    is robust to it regardless of the file's own formatting choices."""
    out = {}
    for m in re.finditer(r"--([a-zA-Z0-9-]+):\s*(#[0-9a-fA-F]{3,6}|rgba?\([^)]*\))", text):
        out[m.group(1)] = _token888(m.group(2))
    return out


def parse_console(path):
    """Resolves the ACTUAL cascade the browser would, for every (theme, mode)
    pair the file declares -- not just what one block happens to state.

    Layer order, lowest priority first (later updates win, matching CSS: equal
    specificity resolves by source order, `[data-theme]` always outranks a bare
    `:root` regardless of which query matched):
      1. base `:root{}` (dark)
      2. base `@media(light){:root{}}`            -- only in light mode
      3. `:root[data-theme=X]{}` (dark)            -- only for theme X
      4. `@media(light){:root[data-theme=X]{}}`    -- only for theme X, light mode
    """
    src = open(path, encoding="utf-8").read()

    i = src.index(":root{")
    base_dark = _props(_find_block(src, i + len(":root{"))[0])

    base_light = {}
    for mm in re.finditer(r"@media \(prefers-color-scheme:light\)\{", src):
        rest = src[mm.end():]
        if rest.lstrip().startswith(":root{"):
            k = mm.end() + rest.index(":root{") + len(":root{")
            base_light = _props(_find_block(src, k)[0])
            break

    # THE FULL SPAN of every `@media(light){...}` block, media wrapper included
    # -- computed FIRST, so the dark-block scan below can exclude anything that
    # falls inside one. Without this, `:root[data-theme="gundam"]{` inside the
    # media block was found by the SAME unscoped regex as the true dark block,
    # and because `.setdefault(...)["dark"] = ...` simply OVERWRITES on every
    # match, whichever occurred LAST IN THE FILE silently became "dark" --
    # which is the light block, since it is always written after. Caught
    # because it fed back into check_console(): "gundam" (dark) failed at
    # 2.28:1 on a colour that was never meant to be judged against that
    # background at all.
    media_spans = []
    for mm in re.finditer(r"@media \(prefers-color-scheme:light\)\{", src):
        _, end = _find_block(src, mm.end())
        media_spans.append((mm.start(), end))

    def _inside_media(pos):
        return any(a <= pos < b for a, b in media_spans)

    themes = {}
    for tm in re.finditer(r':root\[data-theme="([a-z0-9]+)"\]\{', src):
        if _inside_media(tm.start()):
            continue
        name = tm.group(1)
        blk, _ = _find_block(src, tm.end())
        themes.setdefault(name, {})["dark"] = _props(blk)
    for mm in re.finditer(r"@media \(prefers-color-scheme:light\)\{", src):
        rest = src[mm.end():]
        tm = re.match(r'\s*:root\[data-theme="([a-z0-9]+)"\]\{', rest)
        if tm:
            name = tm.group(1)
            k = mm.end() + tm.end()
            blk, _ = _find_block(src, k)
            themes.setdefault(name, {})["light"] = _props(blk)

    variants = [("default", dict(base_dark))]
    d = dict(base_dark); d.update(base_light)
    variants.append(("default (clair)", d))
    for name, layers in themes.items():
        d = dict(base_dark); d.update(layers.get("dark", {}))
        variants.append((name, d))
        d2 = dict(base_dark); d2.update(base_light)
        d2.update(layers.get("dark", {})); d2.update(layers.get("light", {}))
        variants.append(("%s (clair)" % name, d2))
    return variants


def check_console(root, verbose):
    path = os.path.join(root, "src", "app", "WebConsole.h")
    variants = parse_console(path)
    print("")
    print("Console web embarquee, encres sur le panneau de verre :")
    failed = False
    for name, val in variants:
        missing = [k for k in list(CONSOLE_KIND) + ["bg", "glass"] if k not in val]
        if missing:
            print("  %-16s ILLISIBLE : %s absente(s)"
                  % (name, ", ".join(sorted(missing))))
            failed = True
            continue
        bg = _over(val["glass"], val["bg"][:3])
        bad = []
        for var, kind in sorted(CONSOLE_KIND.items()):
            cr = ratio888(val[var][:3], bg)
            ok = kind == "deco" or cr >= NEED[kind]
            if not ok:
                bad.append((var, cr, NEED[kind]))
            if verbose:
                rgb = val[var][:3]
                print("    --%-5s %-7s %5.2f:1  %-6s %s"
                      % (var, "#%02x%02x%02x" % rgb, cr, kind,
                         "exempt" if kind == "deco" else ("ok" if ok else "ECHEC")))
        print("  %-16s %s" % (name, "CONFORME" if not bad else "NON CONFORME"))
        for var, cr, need in bad:
            print("     ECHEC --%s %.2f:1 < %.1f:1" % (var, cr, need))
        failed = failed or bool(bad)
    return failed


def main():
    verbose = "-v" in sys.argv
    # Three levels up from `scripts/gates/`; asserted, see check-a222.py.
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    assert os.path.isfile(os.path.join(root, 'platformio.ini')), \
        'repo root not found from ' + __file__
    path = os.path.join(root, "firmware", "flight-radar", "main.cpp")
    themes = parse_themes(path, FIELDS)
    names = (["Gundam", "Gundam nuit (orange)", "Scope", "Scope nuit (astro)"]
             + ["theme %d" % i for i in range(4, len(themes))])[:len(themes)]
    failed = False

    for name, d in zip(names, themes):
        bad, worst = [], (None, 99.0)
        if verbose:
            print("=== %s ===" % name)
        for f in FIELDS:
            k = KIND[f]
            if k is None:
                continue
            bg = d["panelBg"] if f in ON_PANEL else 0x0000
            cr = ratio(d[f], bg)
            if k != "deco":
                if cr < worst[1]:
                    worst = (f, cr)
                if cr < NEED[k]:
                    bad.append((f, cr, NEED[k]))
            if verbose:
                print("  %-10s 0x%04X  %5.2f:1  %-6s %s"
                      % (f, d[f], cr, k,
                         "exempt" if k == "deco"
                         else ("ok" if cr >= NEED[k] else "ECHEC")))
        print("%-11s %s  (plus faible non decoratif : %s %.2f:1)"
              % (name, "CONFORME" if not bad else "NON CONFORME",
                 worst[0], worst[1]))
        for f, cr, need in bad:
            print("     ECHEC %s %.2f:1 < %.1f:1" % (f, cr, need))
        failed |= bool(bad)

    # Bandeau de notification PLEIN : texte NOIR sur la couleur du message.
    # 8 paires = 2 fonds possibles (accent, alert) x 4 themes, seuil texte.
    print("\nBandeau de notification (fond plein, texte NOIR centre) -"
          " seuil texte %.1f:1 :" % BANNER_NEED)
    for name, d in zip(names, themes):
        cells, worst = [], None
        for f in BANNER_FIELDS:
            cr = ratio(BANNER_FG, d[f])
            cells.append("noir/%-6s 0x%04X %5.2f:1 %s"
                         % (f, d[f], cr, "ok" if cr >= BANNER_NEED else "ECHEC"))
            if cr < BANNER_NEED:
                worst = (f, cr) if worst is None or cr < worst[1] else worst
                failed = True
        print("  %-21s %s" % (name, "   ".join(cells)))
        if worst:
            print("     ECHEC bandeau %s %.2f:1 < %.1f:1"
                  % (worst[0], worst[1], BANNER_NEED))

    # 1.4.1 « Utilisation de la couleur » : la tranche d'altitude n'est
    # signalee QUE par la couleur pour un blip non traque. Non verifiable
    # automatiquement — on imprime les ecarts pour orienter la revue visuelle.
    print("\nEcarts DANS la rampe d'altitude (portee par la seule couleur ;"
          "\nle militaire, lui, a aussi une FORME distincte) :")
    for name, d in zip(names, themes):
        ramp = ["altG", "altLow", "altMid", "altCruise"]
        gaps = ["%s/%s %.2f" % (ramp[i][3:], ramp[i + 1][3:],
                                ratio(d[ramp[i]], d[ramp[i + 1]]))
                for i in range(len(ramp) - 1)]
        print("  %-11s %s" % (name, "  ".join(gaps)))

    failed |= check_space(root, verbose)
    failed |= check_glass(root, verbose)
    failed |= check_console(root, verbose)

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
