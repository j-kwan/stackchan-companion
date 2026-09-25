#!/usr/bin/env bash
# find-port.sh - resolve a serial port by VID/PID, never by name (Linux).
#
# WHY. With the StackChan and the Fire plugged in at the same time, a
# `pio run -t upload` WITHOUT `--upload-port` picks one on its own - and the
# wrong pick reflashes the StackChan's companion with the Fire binary. On Linux
# /dev/ttyACM0 and /dev/ttyUSB0 are no more an identity than COM6 is: they
# depend on plug order and change across reboots. The VID/PID names the board.
#
#   ./scripts/dev/find-port.sh --board cores3   -> /dev/ttyACM0  (native USB)
#   ./scripts/dev/find-port.sh --board fire     -> /dev/ttyUSB0  (CP2104)
#   ./scripts/dev/find-port.sh --list           -> everything recognised
#
# Exits 1 and writes NOTHING to stdout when the board is absent or ambiguous: a
# check that cannot fail is worth nothing, and here the failure must abort the
# upload command rather than let it guess.
#
# Diagnostics go to STDERR, and that is load-bearing: stdout is substituted into
# a command line, so a message reaching it would be handed to --upload-port as
# though it were a port name.
#
# NO udevadm, NO python: the identity is read straight out of sysfs by walking
# up from the tty to the USB device that owns it. One dependency fewer than the
# machine has to have installed, and it works in a container.
#
# The two patterns below are a hand copy of find-port.ps1's table and are
# registered in scripts/gates/check-mirrors.py, so a board added on one platform
# cannot silently stay unknown on the other.
set -uo pipefail

err() { printf '%s\n' "$*" >&2; }

boards='cores3 fire'
vid_cores3=303a; pid_cores3=1001
vid_fire=10c4;   pid_fire=ea60
label_cores3='M5Stack CoreS3 (native Espressif USB)'
label_fire='M5Stack Fire (CP2104 bridge)'

# The USB device that owns a tty: walk up from /sys/class/tty/<n>/device until a
# directory carries idVendor. A CDC device sits two levels up, a USB-serial
# bridge three - counting the hops instead of walking is how this breaks on the
# next board.
usb_ids() {
  local d; d="$(readlink -f "/sys/class/tty/$1/device" 2>/dev/null)" || return 1
  while [ -n "$d" ] && [ "$d" != "/" ]; do
    if [ -r "$d/idVendor" ] && [ -r "$d/idProduct" ]; then
      printf '%s:%s\n' "$(cat "$d/idVendor")" "$(cat "$d/idProduct")"
      return 0
    fi
    d="$(dirname "$d")"
  done
  return 1
}

resolve() {
  local key="$1" vid pid label found=() ids
  eval "vid=\${vid_$key:-}"; eval "pid=\${pid_$key:-}"; eval "label=\${label_$key:-}"
  # An unknown key would leave the patterns empty and the comparison below would
  # match nothing rather than everything - the one outcome this script exists to
  # prevent is handing back a WRONG port instead of none.
  if [ -z "$vid" ]; then err "unknown board '$key'"; return 1; fi

  local t n
  for t in /sys/class/tty/*; do
    n="$(basename "$t")"
    case "$n" in ttyACM*|ttyUSB*) ;; *) continue ;; esac
    ids="$(usb_ids "$n")" || continue
    [ "$ids" = "$vid:$pid" ] && found+=("/dev/$n")
  done

  if [ ${#found[@]} -eq 0 ]; then
    err "$label: ABSENT ($vid:$pid not found)"
    return 1
  fi
  if [ ${#found[@]} -gt 1 ]; then
    # Two boards of the same model: refuse rather than draw lots.
    err "$label: AMBIGUOUS, ${#found[@]} ports - ${found[*]}"
    err "  unplug one, or pass --upload-port by hand."
    return 1
  fi
  if [ ! -r "${found[0]}" ] || [ ! -w "${found[0]}" ]; then
    # The Linux equivalent of the Windows "driver not installed" case: the node
    # exists and you cannot open it. Naming the group beats "permission denied"
    # surfacing from inside esptool.
    err "$label: ${found[0]} is not writable"
    err "  -> add yourself to the 'dialout' group (or 'uucp'), then log back in."
    return 1
  fi
  printf '%s\n' "${found[0]}"
}

case "${1:-}" in
  --list|-l)
    # Exits 1 if ANY known board failed to resolve, so --list can be used as a
    # precondition ("are both boards ready?") and not merely as something to
    # read.
    missing=0
    for k in $boards; do
      if p="$(resolve "$k")"; then
        eval "l=\$label_$k"
        err "$(printf '%-8s %-14s %s' "$k" "$p" "$l")"
      else
        missing=$((missing + 1))
      fi
    done
    exit $((missing ? 1 : 0)) ;;
  --board|-b)
    [ $# -ge 2 ] || { err "usage: $0 --board cores3|fire"; exit 1; }
    p="$(resolve "$2")" || exit 1
    # stdout carries ONLY the port: it is meant to be substituted into a
    # command line. Everything else went to stderr.
    printf '%s\n' "$p"
    exit 0 ;;
  *)
    err "usage: $0 --board cores3|fire   (or --list)"
    exit 1 ;;
esac
