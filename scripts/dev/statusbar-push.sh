#!/usr/bin/env bash
# =============================================================================
# statusbar-push.sh - pushes FIELDS to the StackChan status band (macOS/Linux)
# =============================================================================
# The robot only ever displays fields (the "sources -> fields -> widgets"
# contract). Any source - a script, a cron job, Home Assistant, a NAS - pushes
# its values here. Generic: it takes key=value pairs.
#
# Examples:
#   # three Claude gauges (Gauges mode: POST /api/statusbar?mode=3)
#   ./statusbar-push.sh --ip 192.168.1.50 \
#     g0=62 g0l=CTX g0r=stackch. g1=41 g1l=5H g1r=1h24 g2=88 g2l=7J g2r=2j05
#
#   # a notification
#   ./statusbar-push.sh --ip 192.168.1.50 --say 'Lave-linge termine' --say-ms 6000
#
#   # every 30 s
#   while :; do ./statusbar-push.sh --ip "$ip" $(my-gauges); sleep 30; done
#
# NUMERIC values are pushed as floats, TEXT as strings.
set -uo pipefail

IP="${STACKCHAN_IP:-}"
MODE=-1
SAY=""
SAY_MS=4000
AUTH=""
FIELDS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --ip) IP="$2"; shift 2 ;;
    --mode) MODE="$2"; shift 2 ;;
    --say) SAY="$2"; shift 2 ;;
    --say-ms) SAY_MS="$2"; shift 2 ;;
    --auth) AUTH="$2"; shift 2 ;;      # 'user:pass' if Basic Auth is on
    *) FIELDS+=("$1"); shift ;;
  esac
done
[ -n "$IP" ] || { echo "IP requise (--ip ou \$STACKCHAN_IP)" >&2; exit 1; }

CURL=(curl -s -o /dev/null --max-time 5)
[ -n "$AUTH" ] && CURL+=(-u "$AUTH")

# NO HAND-ROLLED %XX TABLE. The API wants its parameters in the QUERY string,
# not in a body, and `--get` puts `--data-urlencode` there while `-X POST` keeps
# the method - so curl's own encoder does the escaping. A label with a space, an
# accent or an `&` in it is exactly the input a home-made escaper gets wrong,
# and the failure is a truncated field on the robot with nothing in the log.
post() {
  local path="$1"; shift
  "${CURL[@]}" --get -X POST "http://$IP/$path" "$@" \
    || echo "POST $path : echec" >&2
}

[ "$MODE" -ge 0 ] && post "api/statusbar" --data-urlencode "mode=$MODE"

if [ ${#FIELDS[@]} -gt 0 ]; then
  args=()
  for f in "${FIELDS[@]}"; do
    k="${f%%=*}"; v="${f#*=}"
    [ "$k" = "$f" ] && continue          # no '=' at all
    # Force TEXT when the value is not numeric, so a label like "5H" does not
    # arrive as the number 5. The firmware decides on the key's `_s` suffix.
    case "$v" in
      ''|*[!0-9.+-]*|*.*.*) k="${k}_s" ;;
    esac
    args+=(--data-urlencode "$k=$v")
  done
  [ ${#args[@]} -gt 0 ] && post "api/field" "${args[@]}"
fi

[ -n "$SAY" ] && post "api/say" --data-urlencode "ms=$SAY_MS" \
                                --data-urlencode "text=$SAY"
exit 0
