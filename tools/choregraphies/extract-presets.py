# -*- coding: utf-8 -*-
"""Extrait du firmware ce dont l'editeur a besoin pour montrer les VRAIS yeux.

    python tools/choregraphies/extract-presets.py

Produit `presets.js`, qui contient :

  PRESETS         les formes brutes de `src/engine/presets/*.h`
  EMOTIONS        les 30 noms canoniques de `src/engine/Emotions.h` — ce sont
                  les SEULS que le firmware accepte dans un CSV
  EMOTION_PRESETS pour chaque emotion, le preset de l'oeil GAUCHE, celui du
                  DROIT (12 emotions sont asymetriques) et, le cas echeant, le
                  preset qui ancre la fermeture des paupieres
  EMOTION_RGB     la couleur de chaque emotion (`emotionToRgb`) — les yeux ne
                  sont pas tous cyan : jaune pour la joie, rouge-orange pour
                  la colere, lavande pour la peur…
  EYE_COLOR_DIM   l'assombrissement global de la palette (`eye_color_dim`)

Recopier tout cela a la main l'aurait fait diverger du firmware des la
premiere retouche : le script relit les sources et regenere le fichier.
A relancer apres toute modification d'un preset ou de `EyeRig::setEmotion`.
"""
import io, os, re, sys, json

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENG = os.path.join(ROOT, "src", "engine")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "presets.js")

# Ordre POSITIONNEL des champs, tel que declare dans EyeConfig.h. Les presets
# utilisent des initialiseurs positionnels : l'ordre fait foi, et en oublier
# un decale silencieusement tous les suivants.
FIELDS = ["OffsetX", "OffsetY", "Height", "Width",
          "Slope_Top", "Slope_Bottom", "Radius_Top", "Radius_Bottom",
          "Inverse_Radius_Top", "Inverse_Radius_Bottom",
          "Inverse_Offset_Top", "Inverse_Offset_Bottom",
          "Radius_Top_Outer", "Radius_Bottom_Outer", "OuterIsLeft"]
FLOATS = {"Slope_Top", "Slope_Bottom"}

# Une valeur seule sur sa ligne, virgule finale FACULTATIVE : le dernier champ
# d'un initialiseur n'en a pas, et l'exiger le faisait disparaitre en silence.
NUM = re.compile(r"^\s*(-?[0-9]*\.?[0-9]+)f?\s*,?\s*$")


def strip_comments(line):
    return re.sub(r"//.*", "", re.sub(r"/\*.*?\*/", "", line))


def parse_presets(path):
    src = io.open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"Preset_(\w+)\s*=\s*\{(.*?)\};", src, re.S):
        name, body = m.group(1), m.group(2)
        vals = []
        for line in body.splitlines():
            g = NUM.match(strip_comments(line))
            if g:
                vals.append(float(g.group(1)))
        if len(vals) < 4:
            print("  ! %-24s ignore (%d valeurs lues)" % (name, len(vals)))
            continue
        if len(vals) > len(FIELDS):
            sys.exit("%s : %d valeurs pour %d champs — EyeConfig.h a change ?"
                     % (name, len(vals), len(FIELDS)))
        vals += [0.0] * (len(FIELDS) - len(vals))     # champs omis = defaut
        out[name] = {k: (v if k in FLOATS else int(v))
                     for k, v in zip(FIELDS, vals)}
    return out


def parse_emotions(path):
    """Les 30 noms canoniques, dans l'ordre de l'enum."""
    src = io.open(path, encoding="utf-8").read()
    m = re.search(r"NAMES\[EMOTIONS_COUNT\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("liste des emotions introuvable dans Emotions.h")
    return re.findall(r'"(\w+)"', m.group(1))


def parse_colors(path, emotions):
    """`emotionToRgb` : la couleur RGB888 de chaque emotion.

    Le switch groupe les cas :  case A: case B: ... return 0xRRGGBB;
    On lit donc chaque paquet de `case` jusqu'a son `return`, et le
    `default:` sert de couleur de repli pour toute emotion non citee.
    """
    src = strip_comments_block(io.open(path, encoding="utf-8").read())
    body = src[src.index("uint32_t emotionToRgb"):]
    body = body[:body.index("\n}")]
    out, fallback = {}, None
    for cases, rgb in re.findall(
            r"((?:(?:case\s+\w+|default)\s*:\s*)+)return\s+(0x[0-9A-Fa-f]+)\s*;", body):
        val = int(rgb, 16)
        names = re.findall(r"case\s+(\w+)\s*:", cases)
        if "default" in cases and not names:
            fallback = val
        for n in names:
            out[n] = val
    if fallback is None:
        sys.exit("emotionToRgb : pas de `default:` — la couleur de repli est requise")
    # Une emotion absente du switch tombe sur le default : on la MATERIALISE
    # plutot que de laisser l'editeur deviner.
    for e in emotions:
        out.setdefault(e, fallback)
    return {e: out[e] for e in emotions}


def parse_dim(path):
    """`tuning.eye_color_dim` — l'assombrissement applique a TOUTE la palette."""
    src = io.open(path, encoding="utf-8").read()
    m = re.search(r"eye_color_dim\s*=\s*([0-9.]+)f", src)
    if not m:
        sys.exit("eye_color_dim introuvable dans Tuning.h")
    return float(m.group(1))


def parse_mapping(path, emotions):
    """`EyeRig::setEmotion` : quel preset pour quel oeil, par emotion.

    Deux formes dans le switch :
        transitionTo(Preset_X, ...)                 -> les deux yeux
        transitionTo(L ? Preset_A : Preset_B, ...)  -> gauche A, droite B
    `lidCenterOn(Preset_Y)` designe le preset qui ancre la fermeture.
    """
    src = io.open(path, encoding="utf-8").read()
    body = src[src.index("void setEmotion"):]
    body = body[:body.index("\n    }")] if "\n    }" in body else body
    # Un bloc `case X:` court jusqu'au `case` suivant.
    parts = re.split(r"\bcase\s+(\w+)\s*:", body)
    out = {}
    for i in range(1, len(parts) - 1, 2):
        emo, blk = parts[i], strip_comments_block(parts[i + 1])
        if emo not in emotions:
            continue
        t = re.search(r"transitionTo\(\s*L\s*\?\s*Preset_(\w+)\s*:\s*Preset_(\w+)", blk)
        if t:
            left, right = t.group(1), t.group(2)
        else:
            t = re.search(r"transitionTo\(\s*Preset_(\w+)", blk)
            if not t:
                continue
            left = right = t.group(1)
        lc = re.search(r"lidCenterOn\(\s*Preset_(\w+)", blk)
        out[emo] = {"left": left, "right": right,
                    "lidCenter": lc.group(1) if lc else None}
    return out


def strip_comments_block(s):
    return "\n".join(strip_comments(l) for l in s.splitlines())


def main():
    presets = {}
    pdir = os.path.join(ENG, "presets")
    for f in sorted(os.listdir(pdir)):
        if f.endswith(".h"):
            got = parse_presets(os.path.join(pdir, f))
            print("%-28s %2d presets" % (f, len(got)))
            presets.update(got)
    if not presets:
        sys.exit("aucun preset extrait — le format des en-tetes a-t-il change ?")

    emotions = parse_emotions(os.path.join(ENG, "Emotions.h"))
    print("%-28s %2d emotions" % ("Emotions.h", len(emotions)))
    colors = parse_colors(os.path.join(ENG, "Emotions.h"), emotions)
    print("%-28s %2d couleurs (%d teintes distinctes)"
          % ("Emotions.h/emotionToRgb", len(colors), len(set(colors.values()))))
    dim = parse_dim(os.path.join(ENG, "Tuning.h"))
    print("%-28s eye_color_dim = %.2f" % ("Tuning.h", dim))
    mapping = parse_mapping(os.path.join(ENG, "EyeRig.h"), emotions)
    print("%-28s %2d correspondances" % ("EyeRig.h", len(mapping)))

    # Une emotion sans correspondance retomberait sur Normal a l'ecran sans
    # que personne ne le sache : on le DIT.
    missing = [e for e in emotions if e not in mapping]
    if missing:
        print("  ! sans preset explicite (default -> Normal) : " + ", ".join(missing))
    # `lidCenter` compte AUSSI : s'il manque, l'editeur retombe en silence sur
    # l'ancrage bas et montre une fermeture de paupieres fausse pour les 12
    # emotions « rondes » (A2.17). Il vaut None la plupart du temps, d'ou le
    # filtre `if p`.
    unknown = sorted({p for m in mapping.values()
                      for p in (m["left"], m["right"], m["lidCenter"])
                      if p and p not in presets})
    if unknown:
        sys.exit("presets cites par EyeRig mais introuvables : " + ", ".join(unknown))

    def dump(name, obj):
        return "window.%s = %s;\n" % (name, json.dumps(obj, indent=2,
                                                       sort_keys=True,
                                                       ensure_ascii=False))
    io.open(OUT, "w", encoding="utf-8", newline="\n").write(
        "// GENERE par extract-presets.py — NE PAS EDITER A LA MAIN.\n"
        "// Sources : src/engine/presets/*.h, Emotions.h, EyeRig.h\n"
        "// Relancer le script apres toute modification de l'un des trois.\n"
        "// Charge en <script> simple, pas un module ES : l'editeur doit\n"
        "// s'ouvrir d'un double-clic, or file:// bloque les modules.\n"
        + dump("PRESETS", presets)
        + dump("EMOTIONS", emotions)
        + dump("EMOTION_PRESETS", mapping)
        + dump("EMOTION_RGB", colors)
        + "window.EYE_COLOR_DIM = %r;\n" % dim)
    print("-> %s" % os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
