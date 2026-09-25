# -*- coding: utf-8 -*-
"""Build the SD runway database read by the flight-radar METAR rose.

    python tools/generators/make-runways.py                 # downloads the source
    python tools/generators/make-runways.py runways.csv     # uses a local copy

Source: OurAirports `runways.csv`, mirrored by David Megginson at
https://davidmegginson.github.io/ourairports-data/runways.csv
The OurAirports data is released into the PUBLIC DOMAIN by its authors
(https://ourairports.com/data/ - "you may use it freely"), which is what makes
it shippable on the SD card alongside an AGPL-3.0 firmware.

WHY A DERIVED FILE AT ALL. A runway NUMBER IS NOT ITS HEADING: it is the
MAGNETIC bearing rounded to the ten, while the rose is drawn in TRUE degrees.
FMEE's "12/30" really lies 102/282 - 18 deg away from the 120 the number
suggests, enough to put the bar visibly askew. The heading has to be looked
up, not deduced; no METAR carries it.

THE SOURCE HAS BEEN CROSS-CHECKED AGAINST THE OFFICIAL AIP (2026-07-31).
OurAirports is community-maintained, so the figure the rose hangs on was
verified against the French AIP itself - the AIXM 5.1 dataset EUROCONTROL
publishes for France and its overseas territories (`LF_AIP_DS_PartOf_*.zip`,
https://ext.eurocontrol.int/aixm_confluence/display/AIX/France, free, no
account). Its `RunwayDirection/trueBearing` for FMEE reads 102.00 / 282.00 for
12/30 and 116.00 / 296.00 for 14/32, and its `nominalLength` confirms 12/30
(3200 m) as the main runway over 14/32 (2670 m): the same values this script
derives from OurAirports, and the same main-runway pick the sort order makes.
That dataset is NOT used as the source: it is France-only (545 aerodromes
against 10727 here) and frozen on an old AIRAC cycle. It is the CONTROL.
The paid SIA e-shop XML (XML-SIA / AIXM 4.5, one order per AIRAC cycle) would
be the same data behind a manual download - no reason to depend on it.

WHY FIXED-WIDTH RECORDS. The bin has no room to load 4 MB, nor to scan it.
Records of a CONSTANT 17 bytes turn the file into an addressable array: the
firmware divides the file size by 17 to get the record count and BINARY
SEARCHES on offsets - 14 seeks of 17 bytes for any aerodrome, no cache, no
index. A variable-width CSV would have forced a linear scan of the whole file
on every station change. One byte of drift in this writer breaks that, hence
the self-test at the end.

    ICAO....le.he.hdg\n     7 + 3 + 3 + 3 + 1 = 17 bytes
    FMEE   12 30 102\n      (fields left-aligned, padded with spaces)

WHY THE SORT ORDER. Ascending on the PADDED ident (so the byte order the
firmware compares with is the order this file is in), then DESCENDING on
length: the first record of an aerodrome is thus its MAIN runway, and the
firmware can stop at the first hit. FMEE has two (12/30 at 10499 ft and 14/32
at 8760 ft) and the long one is the one a pilot means.
"""
import csv, io, math, os, sys, urllib.request

URL = "https://davidmegginson.github.io/ourairports-data/runways.csv"
# The repo root, from `tools/generators/` — three levels up. ASSERTED and not
# assumed: a wrong root does not crash here, it makes every path below miss and
# the run reads like a broken repository instead of a moved script.
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
assert os.path.isfile(os.path.join(ROOT, 'platformio.ini')), \
    'repo root not found from ' + __file__
OUT = os.path.join(ROOT, "sdcard", "stackchan-companion", "runways.csv")

IDENT_W, END_W, HDG_W = 7, 3, 3
REC = IDENT_W + END_W + END_W + HDG_W + 1        # + the newline = 17


def load(path=None):
    """Return the source CSV text, from a local file or from OurAirports."""
    if path:
        with io.open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    sys.stderr.write("telechargement %s ...\n" % URL)
    with urllib.request.urlopen(URL, timeout=60) as r:
        return r.read().decode("utf-8", "replace")


def collect(text):
    """Filter the source down to the runways we can actually draw.

    Dropped, and counted: a runway with no TRUE heading (nothing to draw), a
    CLOSED one (drawing it would send someone to a runway that is not there),
    and any ident too long for its fixed field - truncating an ICAO would make
    a record that answers to the WRONG aerodrome, which is worse than missing.
    """
    rows, skip = [], {"nohdg": 0, "closed": 0, "wide": 0}
    for r in csv.DictReader(io.StringIO(text)):
        ident = (r.get("airport_ident") or "").strip().upper()
        le = (r.get("le_ident") or "").strip().upper()
        he = (r.get("he_ident") or "").strip().upper()
        hdg = (r.get("le_heading_degT") or "").strip()
        if not ident or not hdg:
            skip["nohdg"] += 1
            continue
        if (r.get("closed") or "").strip() in ("1", "yes", "true"):
            skip["closed"] += 1
            continue
        if len(ident) > IDENT_W or len(le) > END_W or len(he) > END_W:
            skip["wide"] += 1
            continue
        try:
            h = int(round(float(hdg))) % 360
        except ValueError:
            skip["nohdg"] += 1
            continue
        try:
            length = float((r.get("length_ft") or "").strip())
        except ValueError:
            length = -1.0                    # unknown length sorts last
        rows.append((ident.ljust(IDENT_W), le, he, h, length))
    # The padded ident is the sort key BECAUSE it is the key the firmware
    # compares: sorting on the bare ident would put "FM" before "FMEE" by a
    # different rule than a memcmp over 7 space-padded bytes.
    rows.sort(key=lambda t: (t[0], -t[4]))
    return rows, skip


def render(rows):
    """Serialise to the fixed-width form. Returns bytes, never str: text mode
    on Windows would turn every \\n into \\r\\n and make 18-byte records."""
    out = bytearray()
    for ident, le, he, h, _len in rows:
        rec = "%s%s%s%03d\n" % (ident, le.ljust(END_W), he.ljust(END_W), h)
        assert len(rec) == REC, rec
        out += rec.encode("ascii", "replace")
    return bytes(out)


def find(blob, icao):
    """The firmware's lookup, replayed here: lower_bound over the record
    offsets, then the first record of that ident. Returns (le, he, hdg, reads).
    Kept in this script so the invariant the bin depends on is TESTED at
    generation time, not discovered on the robot."""
    key = icao.upper().ljust(IDENT_W).encode("ascii")
    lo, hi, reads = 0, len(blob) // REC, 0
    while lo < hi:
        mid = (lo + hi) // 2
        reads += 1
        if blob[mid * REC:mid * REC + IDENT_W] < key:
            lo = mid + 1
        else:
            hi = mid
    if lo >= len(blob) // REC:
        return None
    rec = blob[lo * REC:(lo + 1) * REC]
    reads += 1
    if rec[:IDENT_W] != key:
        return None
    s = rec.decode("ascii")
    return (s[7:10].strip(), s[10:13].strip(), int(s[13:16]), reads)


def selftest(blob, rows):
    """Prove the two properties the firmware relies on, over the WHOLE file:
    every ident is reachable by the binary search, and the record it lands on
    is the LONGEST runway of that aerodrome."""
    longest = {}
    for ident, le, he, h, ln in rows:
        if ident not in longest:
            longest[ident] = (le, he, h)     # first = longest, by the sort
    bad = 0
    for ident, want in longest.items():
        got = find(blob, ident.strip())
        if got is None or got[:3] != want:
            bad += 1
            if bad <= 5:
                sys.stderr.write("  ECHEC %s: attendu %s, obtenu %s\n"
                                 % (ident.strip(), want, got))
    return bad


def main(argv):
    rows, skip = collect(load(argv[1] if len(argv) > 1 else None))
    if not rows:
        sys.stderr.write("aucune piste retenue - source vide ou inattendue\n")
        return 1
    blob = render(rows)
    with open(OUT, "wb") as f:
        f.write(blob)
    n = len(rows)
    idents = len(set(r[0] for r in rows))
    print("%s" % OUT)
    print("  %d pistes / %d aerodromes, %d octets (%.0f Kio), %d o/enreg."
          % (n, idents, len(blob), len(blob) / 1024.0, REC))
    print("  ecartes : %d sans cap vrai, %d fermees, %d champs trop longs"
          % (skip["nohdg"], skip["closed"], skip["wide"]))
    print("  recherche dichotomique : %d lectures de %d octets au pire"
          % (int(math.ceil(math.log(n + 1, 2))) + 1, REC))
    bad = selftest(blob, rows)
    print("  auto-test : %d aerodromes, %d echecs" % (idents, bad))
    for icao in ("FMEE", "FMEP", "LFPG"):
        got = find(blob, icao)
        print("  %s -> %s" % (icao, "absent" if not got else
                              "%s/%s @ %03d deg (%d lectures)" % got))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
