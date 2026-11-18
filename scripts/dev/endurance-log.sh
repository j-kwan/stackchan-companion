#!/usr/bin/env bash
# =============================================================================
# endurance-log.sh - P7: captures the serial heartbeat over a long run (Linux)
# =============================================================================
# Usage:  ./scripts/dev/endurance-log.sh [--port /dev/ttyACM0] [--hours 10]
#                                        [--out endurance.log]
# Output: timestamped lines; analyse afterwards with the snippets below.
#
# P7 criteria (ROADMAP §A5 T6):
#   - heapMin (INTERNAL heap) STABLE after the first hour (a decline = a leak)
#   - stkBrain/stkRend/stkServo/stkLoop above 128 words at all times
#   - uptime strictly increasing (a reset = a reboot -> find the cause first)
#   - frame avg under 26000 us; max under 45000 us - isolated ~39 ms spikes are
#     EXPECTED during SD writes (config.yaml - SPI bus shared with the LCD), one
#     late frame with no visible artefact. Measured 2026-07-11.
#
# Quick analysis after the night:
#   grep alive endurance.log | tail -5
#   grep -o 'uptime:[0-9]*s' endurance.log | grep -o '[0-9]*' |
#     awk 'NR>1 && $1<p { print "REBOOT: " p " -> " $1 } { p=$1 }'
#
# WARNING - RTS IS NEVER ASSERTED. On the S3's USB-Serial-JTAG, DTR+RTS at open
# can reproduce esptool's enter-BOOTLOADER sequence and leave the robot frozen
# (incident 2026-07-11: chip found in download mode). DTR alone is enough.
# `stty` on Linux touches neither line by default, and `-hupcl` keeps the driver
# from toggling DTR on close - which is the one thing that could reset the board
# here.
set -uo pipefail

PORT=/dev/ttyACM0
HOURS=10
OUT=endurance.log
while [ $# -gt 0 ]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --hours) HOURS="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    *) echo "usage: $0 [--port DEV] [--hours N] [--out FILE]" >&2; exit 1 ;;
  esac
done

deadline=$(awk -v h="$HOURS" 'BEGIN{ printf "%d", systime() + h*3600 }')
log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$OUT"; }

echo "Capture $PORT -> $OUT jusqu a $(date -d "@$deadline" 2>/dev/null || echo "+${HOURS}h") (Ctrl+C pour arreter)"
trap 'echo; echo "Capture terminee -> $OUT"; exit 0' INT TERM

while [ "$(date +%s)" -lt "$deadline" ]; do
  # (Re)connection - the port can vanish (reset, USB re-enumeration, flash):
  # retry every 5 s until the deadline.
  if [ ! -e "$PORT" ]; then sleep 5; continue; fi
  if ! stty -F "$PORT" 115200 cs8 -cstopb -parenb -hupcl raw -echo 2>/dev/null; then
    sleep 5; continue
  fi
  log "== capture (re)connectee sur $PORT =="
  # ONE OPEN FOR THE WHOLE RUN. `read < "$PORT"` inside the loop reopened the
  # device on every single line: everything arriving between the close and the
  # next open was dropped, silently and with no marker in the log — an
  # endurance capture whose whole purpose is noticing what happened overnight.
  # The PowerShell twin holds one SerialPort open for the same reason.
  exec 3< "$PORT" || { sleep 5; continue; }
  # `read -t 10` gives the same silence detector as the twin's ReadTimeout:
  # the heartbeat is every 5 s, so ten seconds of nothing is a fact worth
  # writing down rather than a gap to notice a month later.
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if IFS= read -r -t 10 -u 3 line; then
      [ -n "$line" ] && log "$line"
    elif [ -e "$PORT" ]; then
      log "!! SILENCE 10 s (heartbeat attendu toutes les 5 s)"
    else
      log "!! PORT PERDU - reconnexion..."
      exec 3<&-
      sleep 5
      break
    fi
  done
  exec 3<&- 2>/dev/null || true
done
echo "Capture terminee -> $OUT"
