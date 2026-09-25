# test-rules.ps1 — exercise the rule file the robot is ACTUALLY running
#
#   .\scripts\dev\test-rules.ps1 -Ip 192.168.20.143
#   .\scripts\dev\test-rules.ps1 -Ip 192.168.20.143 -Verbose
#
# Descended from the Haro phase-1 script, which hard-coded its 21 rules. This
# one asks the ROBOT what it is running (`GET /api/rules`) and drives that, so
# it follows a personality switch and an edited rule file without being touched.
#
# ─────────────────────────────────────────────────────────────────────────────
# WHAT IT CAN AND CANNOT TEST, and why the distinction is the whole point.
#
# loop() REPUBLISHES the sensor-backed fields about once a second
# (main.cpp: `if (now - lastFields > 1000)`). So `POST /api/field?batt=3` is
# overwritten before any rule with a sustain can observe it — measured: pushing
# batt=3 reads back 100 one second later, three times running.
#
# A script that pushed those anyway and printed PASS/FAIL would be reporting on
# the sensor, not on the rule. So they are SKIPPED, by name, with the reason
# printed. A skip that says why is worth more than a green tick that means
# nothing — and this exact shape (a check that cannot fail) has bitten this
# project before.
#
# What IS driveable: fields no firmware task owns — `ctx` and `build` (published
# by PC scripts) and anything custom a rule watches. Those get exercised for
# real: gate raised, value pushed, sustain waited out, emotion read back.
# ─────────────────────────────────────────────────────────────────────────────

param(
    [string]$Ip = "192.168.20.143",
    [int]$Margin = 1200,        # slack added to each rule's sustainMs
    [switch]$Verbose
)

$Base = "http://$Ip"

# Owned by loop() — see the header. Kept as a list of NAMES rather than derived
# from anything, because it is a fact about main.cpp that this script cannot
# observe: the robot does not report which fields it republishes.
$SensorBacked = @('batt','cam','chg','clk','dark_sleepy','ip','light','mic',
                  'micL','micR','night','rssi','tmr','tmr_pg','tmr_ph','tmr_st')

function Get-Json($path) {
    try   { return Invoke-RestMethod -Uri "$Base$path" -TimeoutSec 8 }
    catch { Write-Host "  ! $path injoignable : $_" -ForegroundColor Red; return $null }
}
function Push-Field($key, $val) {
    try { Invoke-RestMethod -Method POST -Uri "$Base/api/field?${key}=${val}" -TimeoutSec 8 | Out-Null }
    catch { }
}
function Get-Emotion { (Get-Json '/api/status').emotion }

# A value that satisfies the rule's comparison, with a margin so a float
# rounding cannot land exactly on the boundary.
function Satisfying($op, $v) {
    switch ($op) {
        'gt' { return $v + 1 }  'ge' { return $v }
        'lt' { return $v - 1 }  'le' { return $v }
        'eq' { return $v }      'ne' { return $v + 1 }
        default { return $v }
    }
}
# ...and one that does NOT, to clear the rule's armed state between runs (the
# engine re-fires only after the condition goes false again).
function Violating($op, $v) {
    switch ($op) {
        'gt' { return $v - 1 }  'ge' { return $v - 1 }
        'lt' { return $v + 1 }  'le' { return $v + 1 }
        'eq' { return $v + 1 }  'ne' { return $v }
        default { return 0 }
    }
}

Write-Host ""
Write-Host "=== regles chargees par $Ip ===" -ForegroundColor Cyan
$doc = Get-Json '/api/rules'
if (-not $doc) { exit 1 }

$tun  = Get-Json '/api/tuning'
$pIdx = if ($tun) { [int]$tun.personality } else { -1 }
$rules = @($doc.rules)
Write-Host ("personnalite {0} | {1} regles ({2} natives)" -f $pIdx, $rules.Count, $doc.builtins)
Write-Host ""

$tested = 0; $fired = 0; $missed = 0; $skipped = 0

foreach ($r in $rules) {
    $label = "{0} {1} {2}" -f $r.f, $r.op, $r.v
    if ($r.en) { $label = "[$($r.en)] $label" }

    # Built-in rules are not what this tool is for: they are compiled, covered
    # by native tests, and two of them drive the night mode — provoking that
    # over HTTP would leave the robot asleep.
    if (-not $r.sd) {
        if ($Verbose) { Write-Host ("  -  {0,-34} native, ignoree" -f $label) -ForegroundColor DarkGray }
        continue
    }

    if ($SensorBacked -contains $r.f) {
        $skipped++
        Write-Host ("  ~  {0,-34} NON TESTABLE : '{1}' est republie par loop() (~1 Hz)" -f $label, $r.f) -ForegroundColor DarkYellow
        continue
    }

    $tested++
    $want = Satisfying $r.op ([double]$r.v)
    $bad  = Violating  $r.op ([double]$r.v)

    if ($r.en) { Push-Field $r.en 1 }      # raise the gate
    Push-Field $r.f $bad                    # make sure it starts from false
    Start-Sleep -Milliseconds 300
    $before = Get-Emotion

    Push-Field $r.f $want
    Start-Sleep -Milliseconds ([int]$r.sus + $Margin)
    $after = Get-Emotion

    # `act` is the printed form, e.g. "SetEmotion Curious 15000" — the only
    # thing this script can verify from outside is the emotion it names.
    $parts = "$($r.act)".Split(' ')
    if ($parts[0] -eq 'SetEmotion' -and $parts.Count -ge 2) {
        if ($after -eq $parts[1]) {
            $fired++
            Write-Host ("  OK {0,-34} -> {1}" -f $label, $after) -ForegroundColor Green
        } else {
            $missed++
            Write-Host ("  KO {0,-34} attendu {1}, obtenu {2} (avant: {3})" -f $label, $parts[1], $after, $before) -ForegroundColor Red
        }
    } else {
        # PlayDance and the field writes leave no state this script can read
        # back without guessing. Counted as exercised, never as verified.
        Write-Host ("  ?  {0,-34} '{1}' declenchee, non verifiable d'ici" -f $label, $r.act) -ForegroundColor DarkCyan
    }

    Push-Field $r.f $bad                    # disarm for the next run
    if ($r.en) { Push-Field $r.en 0 }
    Start-Sleep -Milliseconds 300
}

Write-Host ""
Write-Host ("{0} exercee(s) : {1} verifiee(s), {2} en echec | {3} non testable(s)" -f `
            $tested, $fired, $missed, $skipped) -ForegroundColor Cyan
if ($skipped -gt 0) {
    Write-Host "Les non testables ne sont pas des echecs : leur champ appartient a" -ForegroundColor DarkYellow
    Write-Host "un capteur, et rien d'exterieur ne peut le tenir assez longtemps." -ForegroundColor DarkYellow
}
Write-Host ""
if ($missed -gt 0) { exit 1 }
