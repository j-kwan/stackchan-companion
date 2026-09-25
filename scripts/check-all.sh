#!/usr/bin/env bash
# check-all.sh - every automated gate this project has, in one command (Linux).
#
# The twin of check-all.ps1. The GATES themselves are Python and were always
# portable; what needed porting is the runner around them - the tool discovery,
# the exit-code discipline, and the assertion on how MANY native tests ran.
#
#   ./scripts/check-all.sh          # everything (builds if needed)
#   ./scripts/check-all.sh --fast   # skip the firmware build + A2.22
#   ./scripts/check-all.sh --hook   # install the pre-push hook, run nothing
#
# Order is deliberate: the cheap textual checks first, the native tests next,
# and the firmware build last - a failure should cost as little time as possible
# before it is reported.
#
# THE THRESHOLDS BELOW ARE A HAND COPY of check-all.ps1's, and that is exactly
# the kind of twin this project has been bitten by. They are registered in
# scripts/gates/check-mirrors.py, so the gate denounces the drift instead of
# one platform quietly accepting fewer tests than the other.
set -uo pipefail

MIN_SUITES=37
MIN_TESTS=429

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

FAST=0
for a in "$@"; do
  case "$a" in
    --fast) FAST=1 ;;
    --hook)
      [ -d .git/hooks ] || { printf '\033[31mpas de .git/hooks ici\033[0m\n'; exit 1; }
      cat > .git/hooks/pre-push <<'HOOK'
#!/bin/sh
# Installed by scripts/check-all.sh --hook
exec ./scripts/check-all.sh
HOOK
      chmod +x .git/hooks/pre-push
      printf '\033[32mpre-push installe : .git/hooks/pre-push\033[0m\n'
      echo '  (contourner une fois : git push --no-verify)'
      exit 0 ;;
    *) echo "option inconnue : $a"; exit 1 ;;
  esac
done

# Fail EARLY and clearly rather than once per step.
PIO="$HOME/.platformio/penv/bin/pio"
[ -x "$PIO" ] || PIO="$(command -v pio || true)"
for exe in python3 "$PIO"; do
  if [ -z "$exe" ] || ! command -v "$exe" >/dev/null 2>&1; then
    printf '\033[31moutil introuvable : %s\033[0m\n' "${exe:-pio}"; exit 1
  fi
done
PY=python3

failures=()

# The PowerShell twin needs a sentinel here because $LASTEXITCODE survives a
# command that could not be LAUNCHED, so a gate that never ran was recorded as
# passed. `set -o pipefail` plus a direct $? has no such hole: a missing command
# is exit 127, which is a failure like any other.
step() {
  local name="$1"; shift
  printf '\n\033[36m== %s\033[0m\n' "$name"
  if ! "$@"; then
    failures+=("$name")
    printf '\033[31m   ECHEC (%s)\033[0m\n' "$name"
  fi
}

# NOT here: scripts/gates/check-comments-only.py. It proves a change touched
# comments ONLY and therefore exits 1 as soon as code changed - the normal case
# for every real commit. It is a tool for one kind of sweep, not a gate, and
# putting it in this list made the gate fail on its own first run.
step 'parite EN/FR des docs'    $PY scripts/gates/check-doc-parity.py
step 'contraste des themes'     $PY scripts/gates/check-contrast.py
step 'copies vendorees'         $PY scripts/gates/check-vendored.py
step 'constantes miroir'        $PY scripts/gates/check-mirrors.py
# Une cle de tuning sans controle dans la console est une cle que personne ne
# trouvera : servie par /api/tuning, donc elle a l air finie, et invisible pour
# qui n ouvre pas Swagger.
step 'couverture de la console' $PY scripts/gates/check-console.py
# Le symetrique cote INVITES. Un reglage y traverse quatre listes ecrites a la
# main, et la quatrieme tronque le fichier : une cle relue du yaml mais absente
# de saveConfig n est pas seulement non persistee, elle est DETRUITE a la
# premiere sauvegarde depuis /config (panne auto_bright du 08-05).
step 'reglages des bins invites' $PY scripts/gates/check-guest-config.py
# Deux documents ENUMERENT ce que le code definit : les 44 routes du routeur
# et les 30 valeurs de eEmotions. Une route ajoutee sans ligne dans API.md est
# introuvable autrement qu en lisant le C++ ; une ligne sans route derriere
# promet une API qui n existe pas.
step 'couverture des docs'      $PY scripts/gates/check-doc-coverage.py
# Une ligne de rules.txt que le parseur refuse est ABSENTE, sans erreur ni
# trace : le robot ne reagit simplement pas. Tous les fichiers de la carte,
# pas seulement celui de la personnalite active.
step 'fichiers de regles'       $PY scripts/gates/check-rules.py

# EXPECTED, and asserted - not merely run. `pio test` exits 0 when everything it
# RAN passed, which says nothing about what it did not run: a suite that fails
# to build, or a directory that stops being picked up, simply vanishes and the
# gate still reports success.
native_tests() {
  local out code suites ran failed errored
  out="$("$PIO" test -e native 2>&1)"; code=$?
  suites=$(printf '%s\n' "$out" | grep -cE '^native[[:space:]]+test_' || true)
  # TWO shapes of summary, and the second is the one that matters:
  #   "150 test cases: 150 succeeded"
  #   "151 test cases: 9 failed, 141 succeeded"
  # Requiring "succeeded" right after the colon makes a run WITH failures
  # report zero cases - and the gate then blames the harness for assertions
  # that really fell. A gate that disguises a failure as flakiness is worse
  # than no gate: it teaches you to re-run instead of to read.
  ran=$(printf '%s\n' "$out" | grep -oE '[0-9]+ test cases:' | grep -oE '[0-9]+' | tail -1)
  failed=$(printf '%s\n' "$out" | grep -oE '[0-9]+ failed' | grep -oE '[0-9]+' | tail -1)
  errored=$(printf '%s\n' "$out" | grep -cE '^native[[:space:]]+\S+[[:space:]]+ERRORED' || true)
  NT_OUT="$out"; NT_CODE=$code
  NT_SUITES=${suites:-0}; NT_RAN=${ran:-0}
  NT_FAILED=${failed:-0}; NT_ERRORED=${errored:-0}
}

run_native() {
  native_tests
  # A REAL assertion failure is not retried: the second attempt gives the same
  # answer after letting you believe it was luck. Only incompleteness (missing
  # suites, ERRORED) justifies the retry the Windows harness needs.
  if [ "$NT_FAILED" -gt 0 ]; then
    printf '\033[31m   %s test(s) en ECHEC - ce n est pas le harnais\033[0m\n' "$NT_FAILED"
    printf '%s\n' "$NT_OUT" | grep -E 'FAILED' | head -12
  elif [ "$NT_SUITES" -lt "$MIN_SUITES" ] || [ "$NT_RAN" -lt "$MIN_TESTS" ] || [ "$NT_CODE" -ne 0 ]; then
    printf '\033[33m   incomplet (%s cas, %s suite(s) ERRORED) - SECONDE TENTATIVE\033[0m\n' \
           "$NT_RAN" "$NT_ERRORED"
    native_tests
  fi
  printf '%s\n' "$NT_OUT" | tail -3
  if [ "$NT_FAILED" -gt 0 ]; then
    return 1
  elif [ "$NT_SUITES" -lt "$MIN_SUITES" ] || [ "$NT_RAN" -lt "$MIN_TESTS" ]; then
    printf '\033[31m   SUITES/CAS MANQUANTS : %s suites (attendu >= %s), %s cas (attendu >= %s), %s ERRORED\033[0m\n' \
           "$NT_SUITES" "$MIN_SUITES" "$NT_RAN" "$MIN_TESTS" "$NT_ERRORED"
    echo "   un vert avec des tests ABSENTS n est pas un vert."
    echo "   si une suite passe SEULE (-f <suite>), c est le harnais, pas le code."
    return 1
  fi
  printf '   %s suites, %s cas\n' "$NT_SUITES" "$NT_RAN"
  return "$NT_CODE"
}
step 'tests natifs' run_native

if [ "$FAST" -eq 0 ]; then
  # SEVEN targets. The two `-fire` variants compile the SAME source as their
  # parent with SCE_INPUT_BUTTONS and no companion: they are here because that
  # is exactly the kind of variant that rots in silence - nobody builds it until
  # the board is in hand, and a badly closed #if does not show on the main one.
  build_all() { "$PIO" run -e companion -e flight-radar -e ha-remote \
                  -e flight-radar-fire -e space -e space-fire -e led-fluid \
                  -e led-fluid-fire 2>&1 | tail -8
                return "${PIPESTATUS[0]}"; }
  step 'build des huit firmwares' build_all
  # A2.22 reads the LINKED binary, so it can only run after the build, and on
  # BOTH backends: A2.22 is a code-GENERATION rule, so checking it on the
  # ESP32-S3 says nothing about the Fire's ESP32.
  step 'A2.22 (point d appel unique, dans le binaire)' \
       $PY scripts/gates/check-a222.py companion flight-radar flight-radar-fire \
       space space-fire ha-remote led-fluid led-fluid-fire
fi

echo
if [ ${#failures[@]} -eq 0 ]; then
  printf '\033[32mTOUT PASSE\033[0m\n'
  exit 0
fi
printf '\033[31mECHECS : %s\033[0m\n' "$(IFS=', '; echo "${failures[*]}")"
exit 1
