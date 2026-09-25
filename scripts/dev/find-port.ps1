# find-port.ps1 - resolve a serial port by VID/PID, never by number.
#
# WHY. With the StackChan and the Fire plugged in at the same time, a
# `pio run -t upload` WITHOUT `--upload-port` picks one on its own - and the
# wrong pick reflashes the StackChan's companion with the Fire binary. A COM
# number is not an identity: it depends on plug order and changes across
# reboots. The VID/PID names the board.
#
#   .\scripts\dev\find-port.ps1 -Board cores3   -> COM6  (native Espressif USB)
#   .\scripts\dev\find-port.ps1 -Board fire     -> COMx  (CP2104 bridge)
#   .\scripts\dev\find-port.ps1 -List           -> everything recognised
#
# Exits 1 and writes NOTHING to stdout when the board is absent or ambiguous:
# a check that cannot fail is worth nothing, and here the failure must abort
# the upload command rather than let it guess.
#
# Diagnostics go to STDERR through Write-Err, and that is load-bearing. With
# Write-Host they reach the HOST's output, which a PowerShell `( )` sub-expression
# drops but a redirect does not: `powershell -File find-port.ps1 -Board fire > out`
# from cmd, bash or a Makefile captured "detected but NO COM PORT" and handed it
# to --upload-port as if it were a port name (review 08-02).
#
# ASCII ONLY, deliberately: Windows PowerShell 5.1 reads .ps1 as ANSI, so a
# UTF-8 dash or accent in a comment corrupts the parse (seen 08-02).
param(
    [ValidateSet('cores3', 'fire')]
    [string]$Board,
    [switch]$List
)

$ErrorActionPreference = 'Stop'

# STDOUT carries the port and nothing else - see the header.
function Write-Err([string]$msg) { [Console]::Error.WriteLine($msg) }

# Per-board USB identity. The CoreS3 exposes the ESP32-S3 native USB stack
# (Espressif, 303A); the Fire goes through a Silicon Labs CP2104 (10C4:EA60).
# A classic Core with a CH9102 would be 1A86 - add it here when one shows up.
$KNOWN = [ordered]@{
    cores3 = @{ Pattern = 'VID_303A&PID_1001'; Label = 'M5Stack CoreS3 (native Espressif USB)' }
    fire   = @{ Pattern = 'VID_10C4&PID_EA60'; Label = 'M5Stack Fire (CP2104 bridge)' }
}

function Get-SerialDevices {
    # Win32_PnPEntity rather than Win32_SerialPort: the latter hides devices
    # whose driver is missing, which is exactly the state we want to diagnose
    # (ConfigManagerErrorCode 28 = driver not installed).
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.DeviceID -like '*VID_*' }
}

function Resolve-Board($key) {
    $spec = $KNOWN[$key]
    # An unknown key would leave $spec null, $spec.Pattern null, and the filter
    # below `-like "**"` - i.e. MATCHING EVERY USB DEVICE, and returning the
    # first COM port found as though it were the board asked for. Unreachable
    # through [ValidateSet] today; it is also the single branch where this
    # script could hand back a WRONG port instead of nothing, which is the one
    # outcome it exists to prevent.
    if (-not $spec) { Write-Err "unknown board '$key'"; return $null }
    $hits = @(Get-SerialDevices | Where-Object { $_.DeviceID -like "*$($spec.Pattern)*" })
    if ($hits.Count -eq 0) {
        Write-Err "$($spec.Label): ABSENT ($($spec.Pattern) not found)"
        return $null
    }
    # A port exists only if a driver is loaded AND the name carries (COMn).
    $ports = @($hits | ForEach-Object {
        if ($_.Name -match '\((COM\d+)\)') { $Matches[1] }
    } | Where-Object { $_ } | Sort-Object -Unique)

    if ($ports.Count -eq 0) {
        $err = ($hits | Where-Object { $_.ConfigManagerErrorCode -ne 0 } |
                Select-Object -First 1).ConfigManagerErrorCode
        Write-Err "$($spec.Label): detected but NO COM PORT"
        if ($err -eq 28) {
            Write-Err "  ConfigManagerErrorCode 28 = driver not installed."
            if ($key -eq 'fire') {
                Write-Err "  -> install the Silicon Labs CP210x VCP driver."
            }
        } elseif ($err) {
            Write-Err "  ConfigManagerErrorCode $err"
        }
        return $null
    }
    if ($ports.Count -gt 1) {
        # Two boards of the same model: refuse rather than draw lots.
        $list = $ports -join ', '
        Write-Err "$($spec.Label): AMBIGUOUS, $($ports.Count) ports - $list"
        Write-Err "  unplug one, or pass --upload-port by hand."
        return $null
    }
    return $ports[0]
}

if ($List) {
    # Exits 1 if ANY known board failed to resolve, so `-List` can be used as a
    # precondition ("are both boards ready?") and not merely as something to
    # read. It always exited 0, including with both boards absent, while being
    # documented right above the upload command where it reads like a check.
    $missing = 0
    foreach ($k in $KNOWN.Keys) {
        $p = Resolve-Board $k
        if ($p) { Write-Err ("{0,-8} {1,-6} {2}" -f $k, $p, $KNOWN[$k].Label) }
        else    { $missing++ }
    }
    exit ($(if ($missing) { 1 } else { 0 }))
}

if (-not $Board) {
    Write-Err "usage: .\scripts\dev\find-port.ps1 -Board cores3|fire   (or -List)"
    exit 1
}

$port = Resolve-Board $Board
if (-not $port) { exit 1 }
# stdout carries ONLY the port: the output is meant to be substituted into a
# command line. Everything else goes to Write-Err (the display stream).
Write-Output $port
exit 0
