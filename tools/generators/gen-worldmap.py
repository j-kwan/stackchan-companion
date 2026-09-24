#!/usr/bin/env python3
"""
gen-worldmap.py — rasterise Natural Earth land polygons into a 1-bit PROGMEM
bitmap for the `space` guest bin's ISS view.

WHY A GENERATOR AND NOT A CHECKED-IN BLOB: the header this writes is 5.7 KB of
hex with no readable structure. Without the script that produced it, nobody can
change the resolution, fix a projection mistake or explain where the shape came
from — and a mystery blob in a firmware is exactly the kind of thing that
outlives everyone who understood it.

SOURCE: Natural Earth 110m land polygons (public domain), the coarsest of the
three Natural Earth scales — which is the right one here: at 300 px for 360
degrees of longitude, one pixel is 1.2 degrees, roughly 130 km at the equator.
A finer source would be resampled away.

    curl -O https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson
    python tools/generators/gen-worldmap.py ne_110m_land.geojson firmware/space/worldmap.h

PROJECTION: plate carrée (equirectangular), lon -180..180 left to right, lat
+90..-90 top to bottom — the same mapping `mapLonX`/`mapLatY` use in main.cpp.
The two MUST agree; that is why the constants are printed into the header.

RASTERISATION: even-odd scanline fill. Each output row is one latitude; every
polygon edge crossing that latitude contributes an x-crossing, the crossings
are sorted and filled in pairs. Even-odd handles holes (the Caspian, Lake
Victoria) for free — a winding rule would need the polygons oriented, and
Natural Earth does not guarantee that.
"""

import json
import sys

W, H = 320, 160                      # must match MAP_W / MAP_H in main.cpp
ROW_BYTES = (W + 7) // 8


def rings(geojson):
    """Every linear ring of every (Multi)Polygon, as a list of (lon, lat)."""
    out = []
    for feat in geojson["features"]:
        geom = feat["geometry"]
        polys = ([geom["coordinates"]] if geom["type"] == "Polygon"
                 else geom["coordinates"])
        for poly in polys:
            for ring in poly:
                if len(ring) >= 3:
                    out.append(ring)
    return out


def rasterise(all_rings):
    """1 bit per pixel, row-major, MSB first inside each byte."""
    bits = bytearray(ROW_BYTES * H)
    for py in range(H):
        # Sample at the pixel CENTRE: sampling at the edge puts the equator
        # exactly on a boundary and makes the fill flip-flop by one row.
        lat = 90.0 - (py + 0.5) * 180.0 / H
        xs = []
        for ring in all_rings:
            for i in range(len(ring) - 1):
                lon1, lat1 = ring[i][0], ring[i][1]
                lon2, lat2 = ring[i + 1][0], ring[i + 1][1]
                # Half-open test (lat1 <= lat < lat2): a vertex landing exactly
                # on the scanline is then counted ONCE, not twice, which is
                # what keeps the parity right.
                if (lat1 <= lat) != (lat2 <= lat):
                    t = (lat - lat1) / (lat2 - lat1)
                    xs.append(lon1 + t * (lon2 - lon1))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            x0 = int((xs[i] + 180.0) / 360.0 * W)
            x1 = int((xs[i + 1] + 180.0) / 360.0 * W)
            if x1 < x0:
                x0, x1 = x1, x0
            x0 = max(0, min(W - 1, x0))
            x1 = max(0, min(W - 1, x1))
            for px in range(x0, x1 + 1):
                bits[py * ROW_BYTES + (px >> 3)] |= 0x80 >> (px & 7)
    return bits


def outline(bits):
    """Keep only the SHORELINE: a land pixel with at least one sea neighbour.

    The map is drawn over a day/night background, and a filled continent turns
    that background into a second variable — the eye then has to separate
    "land or sea" from "lit or dark" in the same block of colour. An outline
    states the geography once and leaves the fill to say one thing only
    (user 08-04: "the map should be more discreet, outline only?").

    Four-neighbourhood, and the map EDGE counts as sea so a continent running
    off the side still gets its coast drawn.
    """
    def land_at(px, py):
        if px < 0 or px >= W or py < 0 or py >= H:
            return False
        return bool(bits[py * ROW_BYTES + (px >> 3)] & (0x80 >> (px & 7)))

    out = bytearray(ROW_BYTES * H)
    for py in range(H):
        for px in range(W):
            if not land_at(px, py):
                continue
            if (land_at(px - 1, py) and land_at(px + 1, py) and
                    land_at(px, py - 1) and land_at(px, py + 1)):
                continue                      # interior: dropped
            out[py * ROW_BYTES + (px >> 3)] |= 0x80 >> (px & 7)
    return out


def emit(bits, path):
    land = sum(bin(b).count("1") for b in bits)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"""#pragma once
// =============================================================================
// worldmap.h — GENERATED, do not edit by hand
// =============================================================================
// 1-bit land mask for the space bin's ISS view, {W}x{H}, plate carree
// (lon -180..180 left to right, lat +90..-90 top to bottom). Row-major, MSB
// first, {ROW_BYTES} bytes per row.
//
// Produced by tools/generators/gen-worldmap.py from Natural Earth 110m land polygons
// (public domain). Re-run that script to change the resolution — and change
// MAP_W / MAP_H in main.cpp to match, or the coastline will not line up with
// the ground track drawn over it.
//
// COASTLINE ONLY: the interior of each landmass is dropped (see `outline` in
// the generator). {land} of {W * H} pixels carry ink ({100.0 * land / (W * H):.1f} %).
// The map is drawn over day/night shading, and a filled continent would turn
// that shading into a second variable in the same block of colour.
// =============================================================================

#include <stdint.h>
#include <pgmspace.h>

namespace spa {{

inline constexpr int WORLD_W = {W};
inline constexpr int WORLD_H = {H};
inline constexpr int WORLD_ROW_BYTES = {ROW_BYTES};

inline const uint8_t WORLD_MASK[] PROGMEM = {{
""")
        for i in range(0, len(bits), 16):
            row = ", ".join(f"0x{b:02X}" for b in bits[i:i + 16])
            f.write(f"    {row},\n")
        f.write("""};

// True when the pixel is land. Bounds-checked: the caller loops over the map
// rectangle, and an off-by-one there would otherwise read past the array.
inline bool worldIsLand(int px, int py) {
    if (px < 0 || px >= WORLD_W || py < 0 || py >= WORLD_H) return false;
    const uint8_t b = pgm_read_byte(&WORLD_MASK[py * WORLD_ROW_BYTES + (px >> 3)]);
    return (b & (0x80 >> (px & 7))) != 0;
}

} // namespace spa
""")
    return land


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    with open(sys.argv[1], encoding="utf-8") as f:
        gj = json.load(f)
    rs = rings(gj)
    print(f"{len(rs)} ring(s)")
    filled = rasterise(rs)
    fpct = 100.0 * sum(bin(b).count("1") for b in filled) / (W * H)
    print(f"remplissage : {fpct:.1f} % de terres")
    # Earth is 29 % land. A rasteriser that lost its parity gives 0 % or 90 %,
    # so this one number catches the whole class of failure -- and it has to be
    # checked on the FILLED map, before the outline throws the interior away.
    if not 20.0 <= fpct <= 45.0:
        print("ERREUR: couverture terrestre implausible", file=sys.stderr)
        return 1
    bits = outline(filled)
    ink = emit(bits, sys.argv[2])
    pct = 100.0 * ink / (W * H)
    print(f"{sys.argv[2]}: {len(bits)} bytes, {ink} pixels de trait ({pct:.1f} %)")
    # A shoreline is a few percent of the map. Zero means the edge detector
    # inverted; a third means it did nothing.
    if not 1.0 <= pct <= 12.0:
        print("ERREUR: trait de cote implausible", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
