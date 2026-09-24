#!/usr/bin/env bash
# =============================================================================
# test-mag.sh - independent BMM150 magnetometer tester, over HTTP (Linux)
# =============================================================================
# Judges the SENSOR, not the field it bathes in: on a K151 the norm sits at
# 300-600 uT because the SCS0009 magnets are centimetres away (measured 08-04,
# VALIDATION.md) - that is the ENVIRONMENT's verdict, not the chip's.
# What this script can honestly decide:
#   ALIVE    - samples change at rest (sensor noise exists; frozen = not
#              sampled, all-zero = not initialised)
#   REACTIVE - a hand-delivered stimulus (magnet/steel brought close, or a
#              quarter turn) moves the field far above the resting noise
#
# HTTP and not serial: opening the serial port resets the board over native USB
# (learned 08-04, it cost half a calibration session).
#
# Usage:
#   ./scripts/dev/test-mag.sh                 # full test (phase 2 needs a hand)
#   ./scripts/dev/test-mag.sh --rest-only     # phase 1 only, no interaction
#   ./scripts/dev/test-mag.sh --ip 192.168.1.50
# Exit 0 = sensor OK, 1 = a check failed - a tester that cannot fail is
# worthless (portal discipline).
#
# The statistics live in awk rather than in a shell loop: bash has no floating
# point, and shelling out to python here would make this the THIRD place the
# same verdict is computed.
set -uo pipefail

IP="${STACKCHAN_IP:-192.168.20.143}"
REST_ONLY=0
SECONDS_CAP=8
while [ $# -gt 0 ]; do
  case "$1" in
    --ip) IP="$2"; shift 2 ;;
    --rest-only) REST_ONLY=1; shift ;;
    --seconds) SECONDS_CAP="$2"; shift 2 ;;
    *) echo "usage: $0 [--ip IP] [--rest-only] [--seconds N]" >&2; exit 1 ;;
  esac
done

# One sample per 200 ms, three numbers a line - the format both awk passes read.
capture() {
  local secs="$1" label="$2" out="$3" n=0 end
  end=$(( $(date +%s) + secs ))
  printf '  capture %s (%s s)...' "$label" "$secs"
  : > "$out"
  while [ "$(date +%s)" -lt "$end" ]; do
    if body="$(curl -s --max-time 3 "http://$IP/api/sensors")"; then
      # FIELD BY FIELD, not one contiguous pattern. The old expression required
      # mag_x, mag_y and mag_z to be adjacent AND in that order; reorder the
      # firmware's JSON — a one-line change nobody would connect to this — and
      # the capture silently yields zero samples. Worse, the PowerShell twin
      # would still parse them, so the two testers would disagree about a
      # sensor that is fine.
      printf '%s\n' "$body" \
        | sed -n 's/.*"mag_x":\([-0-9.]*\).*"mag_y":\([-0-9.]*\).*"mag_z":\([-0-9.]*\).*/\1 \2 \3/p' \
        >> "$out"
      n=$((n + 1))
    else
      printf ' !'
    fi
    sleep 0.2
  done
  printf ' %s echantillons\n' "$(wc -l < "$out")"
}

# min/max/sd per axis, mean norm, and how many DISTINCT triples were seen -
# the last one is what separates "quiet sensor" from "sensor not sampled".
stats() {
  awk '
    { x[NR]=$1; y[NR]=$2; z[NR]=$3
      k=sprintf("%.1f/%.1f/%.1f",$1,$2,$3); if(!(k in seen)){seen[k]=1; d++}
      n+=sqrt($1*$1+$2*$2+$3*$3) }
    END {
      # TWELVE fields, because that is how many both readers name. Thirteen
      # left the extra one glued to `count`, and `[ "0 0" -ge 10 ]` is a bash
      # error rather than a comparison.
      if (NR==0) { print "0 0 0 0 0 0 0 0 0 0 0 0"; exit }
      for (a=1; a<=3; a++) {
        lo=1e9; hi=-1e9; s=0
        for (i=1; i<=NR; i++) { v=(a==1?x[i]:(a==2?y[i]:z[i]))
          if(v<lo)lo=v; if(v>hi)hi=v; s+=v }
        m=s/NR; q=0
        for (i=1; i<=NR; i++) { v=(a==1?x[i]:(a==2?y[i]:z[i])); q+=(v-m)*(v-m) }
        printf "%.3f %.3f %.3f ", lo, hi, sqrt(q/(NR>1?NR-1:1))
      }
      printf "%.3f %d %d\n", n/NR, d, NR
    }' "$1"
}

echo "== Testeur magnetometre BMM150 (http://$IP) =="
curl -s --max-time 3 "http://$IP/api/status" >/dev/null || {
  echo "ECHEC : le robot ne repond pas sur $IP"; exit 1; }

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

echo
echo "Phase 1 - REPOS (ne touchez pas au robot)"
capture "$SECONDS_CAP" repos "$tmp/rest"
read -r xlo xhi xsd ylo yhi ysd zlo zhi zsd norm distinct count < <(stats "$tmp/rest")
[ "$count" -ge 10 ] || { echo "ECHEC : trop peu d echantillons"; exit 1; }

printf '  x: [%7.1f..%7.1f] sd %.2f\n' "$xlo" "$xhi" "$xsd"
printf '  y: [%7.1f..%7.1f] sd %.2f\n' "$ylo" "$yhi" "$ysd"
printf '  z: [%7.1f..%7.1f] sd %.2f\n' "$zlo" "$zhi" "$zsd"
printf '  norme moyenne %.0f uT ; %s/%s echantillons distincts\n' \
       "$norm" "$distinct" "$count"

verdict=$(awk -v n="$norm" -v d="$distinct" -v c="$count" 'BEGIN{
  if (n < 1.0) print "zeros";
  else if (d <= (2 > c/10 ? 2 : c/10)) print "frozen";
  else print "alive" }')
fail=0
case "$verdict" in
  zeros)  echo "  VERDICT : ZEROS - le capteur n est pas initialise (M5.Imu ne remplit pas imu_data_t.mag)"; fail=1 ;;
  frozen) echo "  VERDICT : FIGE - les valeurs ne bougent pas, le capteur n est pas reellement echantillonne"; fail=1 ;;
  alive)
    echo "  VERDICT phase 1 : VIVANT (bruit de mesure present)"
    if awk -v n="$norm" 'BEGIN{exit !(n>100)}'; then
      printf '  note : norme %.0f uT >> ~50 terrestres - environnement aimante (attendu sur K151 : aimants servo sous le capteur).\n' "$norm"
      echo "  Le CAPTEUR va bien ; le CHAMP, lui, reste inutilisable pour un cap (VALIDATION.md)."
    fi ;;
esac

if [ "$REST_ONLY" -eq 1 ] || [ "$fail" -eq 1 ]; then exit "$fail"; fi

echo
echo "Phase 2 - STIMULUS"
echo "  Approchez LENTEMENT un objet en acier ou un aimant (~2 cm du"
echo "  robot), promenez-le, puis eloignez-le - pendant toute la capture."
read -r -p "  Pret ? Entree pour lancer la capture" _
capture "$SECONDS_CAP" stimulus "$tmp/stim"
read -r sxlo sxhi _ sylo syhi _ szlo szhi _ _ _ scount < <(stats "$tmp/stim")
[ "$scount" -ge 10 ] || { echo "ECHEC : trop peu d echantillons"; exit 1; }

# The stimulus must move the field far beyond the resting noise: the widest
# axis range must exceed 8x its resting sigma AND 10 uT in absolute terms (a
# fridge magnet at 2 cm swings hundreds of uT - this floor is generous).
read -r range restsd < <(awk -v a="$sxhi" -v b="$sxlo" -v c="$syhi" -v d="$sylo" \
                             -v e="$szhi" -v f="$szlo" \
                             -v p="$xsd" -v q="$ysd" -v r="$zsd" 'BEGIN{
  m=a-b; if (c-d>m) m=c-d; if (e-f>m) m=e-f
  s=p; if (q>s) s=q; if (r>s) s=r
  printf "%.3f %.3f\n", m, s }')
printf '  excursion max %.1f uT (bruit repos sd %.2f)\n' "$range" "$restsd"

if awk -v g="$range" -v s="$restsd" 'BEGIN{ t=(10.0 > 8.0*s ? 10.0 : 8.0*s); exit !(g>t) }'; then
  echo "  VERDICT phase 2 : REACTIF - le capteur voit le stimulus"
  echo
  echo "CAPTEUR OK (vivant + reactif)."
  exit 0
fi
echo "  VERDICT phase 2 : PAS DE REACTION - le champ n a pas bouge"
echo "  plus que le bruit. Capteur suspect (ou stimulus trop loin :"
echo "  refaire avec un aimant franc a 1-2 cm)."
exit 1
