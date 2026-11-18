#!/usr/bin/env python3
"""
scale_presets.py — StackChan-Companion
==================================
Generates EyePresetsM5.h from the exact esp32-eyes presets (EyePresets.h).
Scale factor: ESP32_SCALE = 2.5  (128x64 -> 320x160)

    python3 scale_presets.py        # from the project root

Source: playfultechnology/esp32-eyes + luisllamasbinaburo/ESP32_Faces (AGPL-3.0)

ONE-SHOT GENERATOR, kept for the record: the presets it produced have been
hand-tuned many times since (art direction T8, the _Alt asymmetries, the
validated visual invariants of A2.17). Re-running it would OVERWRITE all of
that. It documents where the shapes came from, it is not a build step.
"""

SCALE = 2.5
# `src/engine/presets/`, not `src/presets/`: the headers moved when engine/ was
# split out, and this path was never updated — the script wrote into a directory
# that no longer exists (review 2026-07-30).
OUTPUT_PATH = "src/engine/presets/EyePresetsM5.h"

# Format: (OffsetX, OffsetY, Height, Width,
#          Slope_Top, Slope_Bottom,
#          Radius_Top, Radius_Bottom,
#          Inv_Radius_Top, Inv_Radius_Bottom,
#          Inv_Offset_Top, Inv_Offset_Bottom)
# Exact values taken from esp32-eyes/EyePresets.h
PRESETS = {
    # ---- Symmetric presets (same preset for L and R) ----
    "Normal":           (  0,  0, 40, 40,  0.0,  0.0,  8,  8,  0, 0, 0, 0),
    "Happy":            (  0,  0, 10, 40,  0.0,  0.0, 10,  0,  0, 0, 0, 0),
    "Glee":             (  0,  0,  8, 40,  0.0,  0.0,  8,  0,  0, 5, 0, 0),
    "Sad":              (  0,  0, 15, 40, -0.5,  0.0,  1, 10,  0, 0, 0, 0),
    "Focused":          (  0,  0, 14, 40,  0.2,  0.0,  3,  1,  0, 0, 0, 0),
    "Surprised":        ( -2,  0, 45, 45,  0.0,  0.0, 16, 16,  0, 0, 0, 0),
    "Frustrated":       (  3, -5, 12, 40,  0.0,  0.0,  0, 10,  0, 0, 0, 0),
    "Furious":          ( -2,  0, 30, 40,  0.4,  0.0,  2,  8,  0, 0, 0, 0),
    "Scared":           ( -3,  0, 40, 40, -0.1,  0.0, 12,  8,  0, 0, 0, 0),
    "Awe":              (  2,  0, 35, 45, -0.1,  0.1, 12, 12,  0, 0, 0, 0),
    "Angry":            ( -3,  0, 20, 40,  0.3,  0.0,  2, 12,  0, 0, 0, 0),
    "Annoyed":          (  0,  0, 12, 40,  0.0,  0.0,  0, 10,  0, 0, 0, 0),
    "Skeptic":          (  0,  0, 40, 40,  0.0,  0.0, 10, 10,  0, 0, 0, 0),
    "Unimpressed":      (  3,  0, 12, 40,  0.0,  0.0,  1, 10,  0, 0, 0, 0),
    "Sleepy":           (  0, -2, 14, 40, -0.5, -0.5,  3,  3,  0, 0, 0, 0),
    "Suspicious":       (  0,  0, 22, 40,  0.0,  0.0,  8,  3,  0, 0, 0, 0),
    "Squint":           (-10, -3, 35, 35,  0.0,  0.0,  8,  8,  0, 0, 0, 0),
    "Worried":          (  0,  0, 25, 40, -0.1,  0.0,  6, 10,  0, 0, 0, 0),
    # ---- Alt variants (asymmetric left eye) ----
    "Worried_Alt":      (  0,  0, 35, 40, -0.2,  0.0,  6, 10,  0, 0, 0, 0),
    "Annoyed_Alt":      (  0,  0,  5, 40,  0.0,  0.0,  0,  4,  0, 0, 0, 0),
    "Skeptic_Alt":      (  0, -6, 26, 40,  0.3,  0.0,  1, 10,  0, 0, 0, 0),
    "Unimpressed_Alt":  (  3, -3, 22, 40,  0.0,  0.0,  1, 16,  0, 0, 0, 0),
    "Sleepy_Alt":       (  0, -2,  8, 40, -0.5, -0.5,  3,  3,  0, 0, 0, 0),
    "Suspicious_Alt":   (  0, -3, 16, 40,  0.2,  0.0,  6,  3,  0, 0, 0, 0),
    "Squint_Alt":       (  5,  0, 20, 20,  0.0,  0.0,  5,  5,  0, 0, 0, 0),
}

INT_FIELDS   = [0, 1, 2, 3, 6, 7, 8, 9, 10, 11]
FLOAT_FIELDS = [4, 5]
FIELD_NAMES  = [
    "OffsetX", "OffsetY", "Height", "Width",
    "Slope_Top", "Slope_Bottom",
    "Radius_Top", "Radius_Bottom",
    "Inverse_Radius_Top", "Inverse_Radius_Bottom",
    "Inverse_Offset_Top", "Inverse_Offset_Bottom",
]

def scale(values):
    out = []
    for i, v in enumerate(values):
        if i in INT_FIELDS:
            out.append(int(round(v * SCALE)))
        else:
            out.append(float(v))
    return out

def fmt_preset(name, values):
    s = scale(values)
    lines = [f"// {name}"]
    lines.append(f"static const sce::EyeConfig Preset_{name} = {{")
    for i, (fn, val) in enumerate(zip(FIELD_NAMES, s)):
        comma = "," if i < len(FIELD_NAMES) - 1 else ""
        if isinstance(val, float):
            lines.append(f"    /* {fn:<30} */ {val:.2f}f{comma}")
        else:
            lines.append(f"    /* {fn:<30} */ {val}{comma}")
    lines.append("};")
    return "\n".join(lines)

HEADER = """\
#pragma once
// =============================================================================
// EyePresetsM5.h — StackChan-Companion   [FICHIER GÉNÉRÉ — ne pas éditer manuellement]
// =============================================================================
// Presets esp32-eyes portés sur StackChan CoreS3 (zone yeux 320×160 px)
// Facteur d'échelle appliqué : ×2.5  (source 128×64 → 320×160)
//
// Généré par : tools/generators/scale_presets.py
// Source     : playfultechnology/esp32-eyes (AGPL-3.0)
//              luisllamasbinaburo/ESP32_Faces (AGPL-3.0)
//
// Notes :
//   - Les Slope_Top/Bottom sont des ratios adimensionnels : NON scalés
//   - Les presets _Alt sont utilisés pour l'œil gauche (asymétrie)
//   - EMOTIONS_COUNT ne tient compte que des presets principaux (pas les Alt)
// =============================================================================

#include "../core/EyeConfig.h"

"""

import os

def main():
    out = HEADER
    for name, values in PRESETS.items():
        out += fmt_preset(name, values) + "\n\n"

    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w") as f:
        f.write(out)

    print(f"✓ Généré : {OUTPUT_PATH}")
    print(f"  {len(PRESETS)} presets (dont {sum(1 for k in PRESETS if '_Alt' in k)} variantes Alt)")
    for name, values in PRESETS.items():
        s = scale(values)
        print(f"  {'*' if '_Alt' in name else ' '} {name:<20} W={s[3]:3d} H={s[2]:3d} "
              f"RT={s[6]:3d} RB={s[7]:3d} SlopeT={s[4]:.2f}")

if __name__ == "__main__":
    main()
