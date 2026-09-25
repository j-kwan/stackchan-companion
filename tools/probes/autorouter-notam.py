# -*- coding: utf-8 -*-
"""Fetch real NOTAMs from autorouter.aero, to write the bin's parser against.

    set AUTOROUTER_USER=you@example.com
    set AUTOROUTER_PASS=yourpassword
    python tools/probes/autorouter-notam.py FMEE            # aerodrome
    python tools/probes/autorouter-notam.py FMEE FMMM       # + its FIR

WHY A SCRIPT AND NOT A curl ONE-LINER. The credentials are your autorouter
account itself (the API reuses the web login, there is no separate key), so
they must not end up in a shell history, in a transcript, or in this repo.
They are read from the ENVIRONMENT only, never from the command line, and the
token this prints is truncated.

WHAT IT IS FOR. The flight-radar NOTAM view opens no connection today because
no free source had ever answered for this station, and writing a parser against
a response nobody has seen would ship a screen that LOOKS informed and is
guessing. This captures a REAL response to `notam-<ICAO>.json` — the same
method that produced the METAR view.

Source: autorouter.aero, whose NOTAM database comes from EUROCONTROL EAD.
Note that not every NOTAM is flagged for international dissemination, which is
why an EAD query and an FAA query on the same aerodrome legitimately return
different lists.

⚠ An activated account is not enough: autorouter requires API access to be
enabled separately, "via the support ticket function". A 400/401 on the token
request with correct credentials means exactly that, and not a typo.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

TOKEN_URL = "https://api.autorouter.aero/v1.0/oauth2/token"
NOTAM_URL = "https://api.autorouter.aero/v1.0/notam"


# Tokens live a WEEK and autorouter allows only 20 active ones per account.
# Minting a fresh one on every run is therefore a countdown to a 403
# "toomanytokens", and it is easy to reach: this script plus every reboot of
# the robot (which used to keep its token in RAM only) burned one each.
# Cached here and reused OPTIMISTICALLY — the recorded expiry is printed but
# not obeyed (08-02): the server is the authority, a stale token costs one
# rejected request, and a needless mint costs one of twenty for a week.
# Gitignored: it is a bearer token for an account that can file flight plans.
TOKEN_CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           ".autorouter-token.json")


def token(user, password, force=False):
    """client_credentials grant: email + password in, bearer token out.

    USES THE CACHE OPTIMISTICALLY (user 08-02): if a token is on disk it is
    returned WITHOUT checking its recorded expiry. The scarce resource is the
    TOKEN — twenty active per account, seven days each — and a stale one costs
    a single rejected request, which the caller turns into one re-auth. Judging
    the age locally minted a fresh token whenever our clock disagreed with the
    server, which is the opposite of the goal.

    `force=True` skips the cache: the caller was refused and wants a new one.
    Returns (token, ttl_seconds, from_cache).
    """
    if not force:
        try:
            with open(TOKEN_CACHE, encoding="utf-8") as f:
                c = json.load(f)
            if c.get("user") == user and c.get("access_token"):
                return c["access_token"], int(c.get("exp", 0) - time.time()), True
        except Exception:
            pass                 # no cache or unreadable: mint one
    body = urllib.parse.urlencode({
        "grant_type": "client_credentials",
        "client_id": user,
        "client_secret": password,
    }).encode("ascii")
    req = urllib.request.Request(TOKEN_URL, data=body)
    with urllib.request.urlopen(req, timeout=30) as r:
        d = json.loads(r.read().decode("utf-8"))
    tok, ttl = d["access_token"], int(d.get("expires_in", 0))
    try:
        with open(TOKEN_CACHE, "w", encoding="utf-8") as f:
            json.dump({"user": user, "access_token": tok,
                       "exp": int(time.time()) + ttl}, f)
    except Exception:
        pass                     # a cache we cannot write is not a failure
    return tok, ttl, False


def notams(tok, icaos, limit=100):
    q = urllib.parse.urlencode({
        "itemas": json.dumps(icaos),
        "offset": 0,
        "limit": limit,
    })
    req = urllib.request.Request(NOTAM_URL + "?" + q,
                                 headers={"Authorization": "Bearer " + tok})
    with urllib.request.urlopen(req, timeout=45) as r:
        return json.loads(r.read().decode("utf-8"))


def ident(n):
    """P0825/17 — series, number, year, as a briefing prints it."""
    return "%s%04d/%02d" % (n.get("series") or "?", n.get("number") or 0,
                            n.get("year") or 0)


def main(argv):
    user = os.environ.get("AUTOROUTER_USER")
    pw = os.environ.get("AUTOROUTER_PASS")
    if not user or not pw:
        sys.stderr.write("AUTOROUTER_USER / AUTOROUTER_PASS non definis "
                         "(voir l'entete de ce fichier)\n")
        return 2
    icaos = [a.upper() for a in argv[1:]] or ["FMEE"]
    try:
        tok, ttl, cached = token(user, pw)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")[:300]
        sys.stderr.write("token: HTTP %d %s\n" % (e.code, body))
        # A 403 has (at least) TWO causes and they call for OPPOSITE actions,
        # so the code alone must never decide the message: this script told a
        # user to open a support ticket when the account was perfectly fine and
        # had merely accumulated tokens (2026-08-01). Read the `error` field.
        try:
            err = (json.loads(body) or {}).get("error", "")
        except Exception:
            err = ""
        if err == "toomanytokens":
            sys.stderr.write(
                "  -> RIEN a demander a personne : le compte va bien, il a "
                "simplement trop de jetons ACTIFS (limite 20).\n"
                "     Un jeton vit 7 jours ; chaque execution sans cache et "
                "chaque redemarrage du robot en consommait un.\n"
                "     Ce script les met desormais en cache (%s) et les "
                "reutilise ; attendez l'expiration des plus vieux,\n"
                "     ou revoquez-les depuis votre compte autorouter.\n"
                % os.path.basename(TOKEN_CACHE))
        elif e.code == 403:
            # The OTHER 403, seen live 2026-07-31: the credentials are RIGHT
            # and the account is active, but API access is a separate
            # permission autorouter grants on request.
            sys.stderr.write(
                "  -> identifiants VALIDES, mais l'acces API n'est pas "
                "accorde sur ce compte.\n"
                "     Demandez-le par un ticket support autorouter "
                "(compte active != acces API).\n")
        elif e.code in (400, 401):
            sys.stderr.write("  -> identifiants refuses (e-mail ou mot de "
                             "passe du compte autorouter)\n")
        return 1
    print("token %s... (%s, %d s restantes selon le cache)"
          % (tok[:8], "du CACHE" if cached else "NEUF", max(0, ttl)))

    for icao in icaos:
        # UN SEUL renouvellement, et seulement si le jeton venait du CACHE.
        # Refuse avec un jeton NEUF, ce n'est pas son age : insister brulerait
        # l'allocation de vingt pour une requete qui ne passera pas.
        try:
            d = notams(tok, [icao])
        except urllib.error.HTTPError as e:
            if e.code in (401, 403) and cached:
                sys.stderr.write("%s: HTTP %d avec le jeton en cache "
                                 "-> renouvellement\n" % (icao, e.code))
                try:
                    tok, ttl, cached = token(user, pw, force=True)
                except urllib.error.HTTPError as e2:
                    sys.stderr.write("renouvellement refuse : HTTP %d\n" % e2.code)
                    return 1
                try:
                    d = notams(tok, [icao])
                except urllib.error.HTTPError as e3:
                    sys.stderr.write(
                        "%s: HTTP %d avec un jeton NEUF - ce n'est pas l'age du "
                        "jeton.\n     Verifiez l'acces API du compte, ou le code "
                        "OACI demande. On arrete.\n" % (icao, e3.code))
                    return 1
            else:
                sys.stderr.write("%s: HTTP %d\n" % (icao, e.code))
                continue
        rows = d.get("rows") or []
        print("\n=== %s : %s NOTAM(s)" % (icao, d.get("total", len(rows))))
        out = "notam-%s.json" % icao
        with open(out, "w", encoding="utf-8") as f:
            json.dump(d, f, indent=1, ensure_ascii=False)
        print("    reponse BRUTE ecrite dans %s" % out)
        # LE MEME DECOMPTE QUE LE ROBOT, et il est la pour une raison precise
        # (2026-08-01) : l'outil listait 27 NOTAM pour FMEE pendant que l'ecran
        # en annoncait 6, et rien ne disait que les deux repondaient a des
        # questions differentes. L'API rend TOUT ce qui est publie, y compris
        # ce qui n'entre en vigueur que la semaine prochaine ; le robot ne
        # montre que ce qui vaut MAINTENANT, filtre briefing compris.
        now = int(time.time())
        # LA MEME CHAINE QUE LE FIRMWARE, dans le meme ordre — et sans
        # pretendre a l'egalite, parce que ce script ne peut PAS savoir si
        # notam_brief est actif sur le robot (revue 08-02 : l'ancienne ligne
        # annoncait "= ce que l'ecran affiche" en filtrant sur le purpose M/K,
        # la ou le firmware ecarte les checklists par leur Q-code, applique le
        # rang M conditionnellement, puis plafonne).
        inforce = [n for n in rows
                   if int(n.get("startvalidity") or 0) <= now
                   <= int(n.get("endvalidity") or 0)]
        # 1. checklists : Q) KKKK, ecartees D'OFFICE cote firmware
        nochk = [n for n in inforce
                 if not ((n.get("code23") or "") == "KK"
                         and (n.get("code45") or "") == "KK")]
        # 2. filtre briefing (notam_brief), rang M et tout purpose inconnu
        def prio(p):
            p = p or ""
            if p.startswith("NBO"): return 0
            if p.startswith("BO"):  return 1
            if p.startswith("B"):   return 2
            return 3
        brief = [n for n in nochk if prio(n.get("purpose")) < 3]
        CAP = 40   # NOTAM_MAX cote firmware
        print("    en vigueur %d | sans les checklists %d | filtre briefing %d"
              % (len(inforce), len(nochk), len(brief)))
        print("    -> l'ecran montre min(%d, %d) avec notam_brief=1, "
              "min(%d, %d) avec notam_brief=0"
              % (len(brief), CAP, len(nochk), CAP))
        for n in rows[:5]:
            e = (n.get("iteme") or "").replace("\n", " ")
            print("  %-10s Q)%s%s  %s" % (ident(n), n.get("code23") or "??",
                                          n.get("code45") or "??", e[:90]))
        if len(rows) > 5:
            print("  ... et %d de plus (tous dans le .json)" % (len(rows) - 5))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
