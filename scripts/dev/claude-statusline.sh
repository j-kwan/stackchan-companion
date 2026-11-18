#!/usr/bin/env bash
# =============================================================================
# claude-statusline.sh - a Claude Code statusline that FEEDS the band
#                        (macOS and Linux; the Windows twin is the .ps1)
# =============================================================================
# Two jobs at once: (1) print a normal Claude Code statusline, (2) push FIELDS
# to the robot so it "lives" the Claude activity without Claude Desktop and
# without BLE - just WiFi and the API. This is the buddy bridge for Claude Code,
# and what makes the four rules shipped in rules.txt able to fire.
#
# Install - in ~/.claude/settings.json:
#   "statusLine": { "type": "command",
#     "command": "/path/to/scripts/dev/claude-statusline.sh" }
# and set the IP: export STACKCHAN_IP=192.168.1.50 (or edit below).
#
# THE CONTEXT IS NOT IN THE JSON. That JSON's schema is undocumented and no
# field in it announces how full the window is; what IS documented is
# `transcript_path`. So we read it: the last entry carrying a `message.usage`
# gives the tokens of the last prompt, and their sum over the window is the
# percentage published as `ctx`.
#
# THE DENOMINATOR IS THE WEAK POINT, and it is explicit: $CLAUDE_CTX_WINDOW
# first, else `autoCompactWindow` from ~/.claude/settings.json, else 200000. On
# a 1M-window model the 200k default reads 100% for ever - setting the variable
# is the answer, not a bug to diagnose.
#
# AND `claude` IS 1 ONLY IF `ctx` COULD BE COMPUTED. A rule like "ctx < 20" is
# TRUE when the field is absent (an unknown field reads 0), so a bridge
# announcing its presence without supplying the context would cheerfully fire
# the empty-context rule. The `claude` gate here means "the bridge is delivering
# context", which is exactly what those rules need.
#
# python3 for the JSON and nothing else: it is already a hard dependency of the
# gates, and a statusline that dies on an unexpected shape would take the prompt
# with it - hence the guards and the `|| true` on the push.
#
# WRITTEN TO BASH 3.2, deliberately: that is the bash macOS still ships, and a
# statusline is exactly the kind of script nobody debugs - it just makes the
# prompt look broken. So: arrays and printf, and NONE of mapfile, associative
# arrays or ${var,,}. On macOS `curl` is there already; `python3` comes with the
# Xcode Command Line Tools. Install: docs/reference/PLUGINS.md.
#
# IT NEEDS A REAL python3. If the interpreter is missing - or is a stub that
# prints an ad instead of running, which is what `python3` is under Windows
# git-bash unless one is installed - the parse yields nothing, the line falls
# back to just the model name and the push goes out as `claude=0`. That is a
# deliberate degradation (a statusline must never break the prompt), but it is
# also silent, so it is worth knowing: on Windows use the .ps1 twin, which has
# no such dependency.
IP="${STACKCHAN_IP:-192.168.1.50}"

raw="$(cat)"
IFS=$'\t' read -r model proj cost ctx <<EOF
$(printf '%s' "$raw" | CLAUDE_CTX_WINDOW="${CLAUDE_CTX_WINDOW:-}" python3 -c '
import json, os, sys

try:
    j = json.load(sys.stdin)
except Exception:
    j = {}
m = (j.get("model") or {}).get("display_name") or "Claude"
d = (j.get("workspace") or {}).get("current_dir") or ""
p = os.path.basename(d.rstrip("/")) if d else "-"
try:
    c = float((j.get("cost") or {}).get("total_cost_usd") or 0)
except (TypeError, ValueError):
    c = 0.0


def window():
    v = os.environ.get("CLAUDE_CTX_WINDOW", "")
    if v.isdigit() and int(v) > 0:
        return int(v)
    try:
        with open(os.path.expanduser("~/.claude/settings.json")) as f:
            w = json.load(f).get("autoCompactWindow")
        if isinstance(w, (int, float)) and w > 0:
            return int(w)
    except Exception:
        pass
    return 200000


def ctx_pct(path):
    # The TAIL, not the file: a session transcript runs to tens of thousands of
    # lines and this runs on every statusline refresh.
    #
    # GROWING WINDOW, because a fixed BYTE window is not a window in LINES. A
    # single entry carrying a tool result runs past 100 kB, so a flat 400 kB
    # tail can hold four lines where the PowerShell twin (`-Tail 400`) reads
    # four hundred. Finding no usage in those four, this pushed `claude=0` and
    # every shipped rule went dark — on Linux only, which is the worst kind of
    # difference to notice. Doubling from 256 kB to 8 MB costs nothing in the
    # common case (the first read almost always contains the answer) and is
    # still bounded.
    if not path or not os.path.isfile(path):
        return None
    lines = []
    try:
        size = os.path.getsize(path)
        span = 262144
        while True:
            with open(path, "rb") as f:
                f.seek(max(0, size - span))
                lines = f.read().decode("utf-8", "ignore").split("\n")
            if any("usage" in l for l in lines) or span >= 8388608 or span >= size:
                break
            span *= 4
    except Exception:
        return None
    for line in reversed(lines):
        # DOUBLE quotes, deliberately: this whole block is passed to python
        # inside a SINGLE-quoted shell argument, so one apostrophe here closes
        # it and the rest of the script is parsed as shell. It cost a debugging
        # round - the symptom was a statusline that printed the fallback and a
        # robot that never saw a field.
        if "usage" not in line:
            continue
        try:
            u = (json.loads(line).get("message") or {}).get("usage") or {}
        except Exception:
            continue
        used = (u.get("input_tokens") or 0) + (u.get("cache_read_input_tokens") or 0) \
             + (u.get("cache_creation_input_tokens") or 0)
        if used > 0:
            return min(100, round(used / window() * 100))
    return None


x = ctx_pct(j.get("transcript_path"))
# TAB-SEPARATED, and nothing is rewritten. Squashing spaces to underscores and
# restoring them in the shell turned a directory named `my_project` into
# `my project` — a lossy round trip to avoid a delimiter problem that a
# character which cannot occur in either field solves outright.
print("%s\t%s\t%.4f\t%s" % (m, p, c, "-" if x is None else x))
' 2>/dev/null)
EOF
model="${model:-Claude}"; proj="${proj:--}"; cost="${cost:-0}"; ctx="${ctx:--}"

# --- the visible statusline (stdout) ---
line="$model"
[ "$proj" != "-" ] && line="$line  $proj"
[ "$ctx" != "-" ] && line="$line  ctx ${ctx}%"
awk -v c="$cost" 'BEGIN{exit !(c>0)}' && line="$line  \$$(printf '%.2f' "$cost")"
printf '%s\n' "$line"

# --- push to the robot (best effort, non blocking, short timeout) ---
# g0 = the CONTEXT gauge (the one the shipped rules read), g1 = session cost
# capped at $5. Label = the project.
# ROUNDED, not truncated: the PowerShell twin uses [int], which rounds, so
# $0.126 gave 3 there and 2 here — the same session showing two different
# gauges depending on which machine ran the statusline.
costpct=$(awk -v c="$cost" 'BEGIN{ p=int(c/5.0*100+0.5); print (p>100?100:p) }')
args=(--data-urlencode "g1=$costpct" --data-urlencode "g1l_s=COST")
if [ "$ctx" != "-" ]; then
  args+=(--data-urlencode "claude=1" --data-urlencode "ctx=$ctx"
         --data-urlencode "g0=$ctx" --data-urlencode "g0l_s=CTX"
         --data-urlencode "g0r_s=$proj")
else
  args+=(--data-urlencode "claude=0")
fi
curl -s -o /dev/null --max-time 2 -X POST --get "http://$IP/api/field" \
  "${args[@]}" 2>/dev/null || true
exit 0
