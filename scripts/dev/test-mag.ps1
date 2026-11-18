# =============================================================================
# test-mag.ps1 - independent BMM150 magnetometer tester (over HTTP, on purpose)
# =============================================================================
# Judges the SENSOR, not the field it bathes in: on a K151 the norm sits at
# 300-600 uT because the SCS0009 magnets are centimetres away (measured
# 08-04, VALIDATION.md) - that is the ENVIRONMENT's verdict, not the chip's.
# What this script can honestly decide:
#   ALIVE    - samples change at rest (sensor noise exists; frozen = not
#              sampled, all-zero = not initialised)
#   REACTIVE - a hand-delivered stimulus (magnet/steel brought close, or a
#              quarter turn) moves the field far above the resting noise
#
# HTTP and not serial: opening the serial port resets the board over native
# USB (learned 08-04, it cost half a calibration session).
#
# Usage:
#   .\scripts\dev\test-mag.ps1                 # full test (phase 2 needs a hand)
#   .\scripts\dev\test-mag.ps1 -RestOnly       # phase 1 only, no interaction
#   .\scripts\dev\test-mag.ps1 -Ip 192.168.1.50
# Exit 0 = sensor OK, 1 = a check failed - a tester that cannot fail is
# worthless (portal discipline).

param(
    [string]$Ip = $env:STACKCHAN_IP,
    [switch]$RestOnly,
    [int]$Seconds = 8
)
if (-not $Ip) { $Ip = '192.168.20.143' }

$ErrorActionPreference = 'Stop'

function Get-MagSamples([int]$secs, [string]$label) {
    $samples = @()
    $end = (Get-Date).AddSeconds($secs)
    Write-Host "  capture $label ($secs s)..." -NoNewline
    while ((Get-Date) -lt $end) {
        try {
            $s = Invoke-RestMethod "http://$Ip/api/sensors" -TimeoutSec 3
            $samples += [pscustomobject]@{ x = [double]$s.mag_x
                                           y = [double]$s.mag_y
                                           z = [double]$s.mag_z }
        } catch { Write-Host ' !' -NoNewline }
        Start-Sleep -Milliseconds 200
    }
    Write-Host " $($samples.Count) echantillons"
    return ,$samples
}

function Stats([object[]]$s, [string]$axis) {
    $v = $s | ForEach-Object { $_.$axis }
    $mean = ($v | Measure-Object -Average).Average
    $sd = [math]::Sqrt((($v | ForEach-Object { ($_ - $mean) * ($_ - $mean) } |
                        Measure-Object -Sum).Sum) / [math]::Max(1, $v.Count - 1))
    [pscustomobject]@{ min = ($v | Measure-Object -Minimum).Minimum
                       max = ($v | Measure-Object -Maximum).Maximum
                       mean = $mean; sd = $sd }
}

Write-Host "== Testeur magnetometre BMM150 (http://$Ip) =="
try { $null = Invoke-RestMethod "http://$Ip/api/status" -TimeoutSec 3 }
catch { Write-Host "ECHEC : le robot ne repond pas sur $Ip"; exit 1 }

# ---- Phase 1 : au repos, ne touchez pas au robot -----------------------------
Write-Host "`nPhase 1 - REPOS (ne touchez pas au robot)"
$rest = Get-MagSamples $Seconds 'repos'
if ($rest.Count -lt 10) { Write-Host 'ECHEC : trop peu d''echantillons'; exit 1 }

$rx = Stats $rest 'x'; $ry = Stats $rest 'y'; $rz = Stats $rest 'z'
$norm = ($rest | ForEach-Object {
    [math]::Sqrt($_.x * $_.x + $_.y * $_.y + $_.z * $_.z) } |
    Measure-Object -Average).Average
$distinct = ($rest | ForEach-Object { '{0:F1}/{1:F1}/{2:F1}' -f $_.x, $_.y, $_.z } |
             Sort-Object -Unique).Count

Write-Host ("  x: [{0,7:F1}..{1,7:F1}] sd {2:F2}" -f $rx.min, $rx.max, $rx.sd)
Write-Host ("  y: [{0,7:F1}..{1,7:F1}] sd {2:F2}" -f $ry.min, $ry.max, $ry.sd)
Write-Host ("  z: [{0,7:F1}..{1,7:F1}] sd {2:F2}" -f $rz.min, $rz.max, $rz.sd)
Write-Host ("  norme moyenne {0:F0} uT ; {1}/{2} echantillons distincts" -f
            $norm, $distinct, $rest.Count)

$fail = $false
if ($norm -lt 1.0) {
    Write-Host '  VERDICT : ZEROS - le capteur n''est pas initialise' `
               '(M5.Imu ne remplit pas imu_data_t.mag)'
    $fail = $true
} elseif ($distinct -le [math]::Max(2, $rest.Count / 10)) {
    Write-Host '  VERDICT : FIGE - les valeurs ne bougent pas, le capteur' `
               'n''est pas reellement echantillonne'
    $fail = $true
} else {
    Write-Host '  VERDICT phase 1 : VIVANT (bruit de mesure present)'
    if ($norm -gt 100) {
        Write-Host ("  note : norme {0:F0} uT >> ~50 terrestres - environnement" -f $norm) `
                   'aimante (attendu sur K151 : aimants servo sous le capteur).'
        Write-Host '  Le CAPTEUR va bien ; le CHAMP, lui, reste inutilisable' `
                   'pour un cap (VALIDATION.md).'
    }
}

if ($RestOnly -or $fail) { exit ([int]$fail) }

# ---- Phase 2 : stimulus ------------------------------------------------------
Write-Host "`nPhase 2 - STIMULUS"
Write-Host '  Approchez LENTEMENT un objet en acier ou un aimant (~2 cm du'
Write-Host '  robot), promenez-le, puis eloignez-le - pendant toute la capture.'
Read-Host  '  Pret ? Entree pour lancer la capture'
$stim = Get-MagSamples $Seconds 'stimulus'
if ($stim.Count -lt 10) { Write-Host 'ECHEC : trop peu d''echantillons'; exit 1 }

$sx = Stats $stim 'x'; $sy = Stats $stim 'y'; $sz = Stats $stim 'z'
# The stimulus must move the field far beyond the resting noise: the widest
# axis range must exceed 8x its resting sigma AND 10 uT in absolute terms
# (a fridge magnet at 2 cm swings hundreds of uT - this floor is generous).
$restSd  = [math]::Max($rx.sd, [math]::Max($ry.sd, $rz.sd))
$range   = [math]::Max($sx.max - $sx.min,
           [math]::Max($sy.max - $sy.min, $sz.max - $sz.min))
Write-Host ("  excursion max {0:F1} uT (bruit repos sd {1:F2})" -f $range, $restSd)

if ($range -gt [math]::Max(10.0, 8.0 * $restSd)) {
    Write-Host '  VERDICT phase 2 : REACTIF - le capteur voit le stimulus'
    Write-Host "`nCAPTEUR OK (vivant + reactif)."
    exit 0
} else {
    Write-Host '  VERDICT phase 2 : PAS DE REACTION - le champ n''a pas bouge'
    Write-Host '  plus que le bruit. Capteur suspect (ou stimulus trop loin :'
    Write-Host '  refaire avec un aimant franc a 1-2 cm).'
    exit 1
}
