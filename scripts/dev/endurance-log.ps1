# =============================================================================
# endurance-log.ps1 — P7 : capture le heartbeat série sur la durée (nuit)
# =============================================================================
# Usage :  .\scripts\dev\endurance-log.ps1 [-Port COM6] [-Hours 10] [-Out endurance.log]
# Sortie : lignes horodatées ; analyser ensuite avec analyze ci-dessous.
#
# Critères P7 (ROADMAP §A5 T6) :
#   - heapMin (heap INTERNE) STABLE après la 1re heure (décroissance = fuite)
#   - stkBrain/stkRend/stkServo/stkLoop > 128 mots en permanence
#   - uptime strictement croissant (reset = reboot → chercher la cause avant)
#   - frame avg < 26000 us ; max < 45000 us — des pics isolés ~39 ms sont
#     ATTENDUS lors des écritures SD (config.yaml — bus SPI partagé LCD/SD),
#     un frame en retard sans artefact visuel. Mesuré 2026-07-11.
#
# Analyse rapide après la nuit :
#   Select-String "alive" endurance.log | Select-Object -Last 5
#   (Select-String "uptime:(\d+)s" endurance.log -AllMatches).Matches |
#     ForEach-Object { [int]$_.Groups[1].Value } |
#     ForEach-Object -Begin { $p = -1 } -Process {
#       if ($_ -lt $p) { Write-Warning "REBOOT détecté (uptime $p -> $_)" }; $p = $_ }
# =============================================================================
param(
    [string]$Port  = "COM6",
    [double]$Hours = 10,
    [string]$Out   = "endurance.log"
)

# ⚠ RTS JAMAIS asserté : sur l'USB-Serial-JTAG du S3, DTR+RTS à l'ouverture
# peut reproduire la séquence esptool d'entrée en BOOTLOADER → robot figé
# (incident 2026-07-11 : chip retrouvé en mode download). DTR seul suffit.
$p = New-Object System.IO.Ports.SerialPort($Port, 115200, 'None', 8, 'One')
$p.DtrEnable = $true; $p.RtsEnable = $false
$p.ReadTimeout = 10000
$deadline = (Get-Date).AddHours($Hours)

function Log([string]$msg) {
    "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $msg" | Add-Content -Path $Out
}

Write-Host "Capture $Port -> $Out jusqu'a $deadline (Ctrl+C pour arreter)"
try {
    while ((Get-Date) -lt $deadline) {
        # (Re)connexion — le port peut disparaitre (reset, re-enumeration
        # USB, flash) : on retente toutes les 5 s jusqu'au deadline
        if (-not $p.IsOpen) {
            try { $p.Open(); Log "== capture (re)connectee sur $Port ==" }
            catch { Start-Sleep -Seconds 5; continue }
        }
        try {
            $line = $p.ReadLine()
            if ($line) { Log $line }
        } catch [System.TimeoutException] {
            Log "!! SILENCE 10 s (heartbeat attendu toutes les 5 s)"
        } catch {
            # Port arrache (ReadLine "operation was canceled", incident
            # 2026-07-12) : fermer et laisser la boucle retenter
            Log "!! PORT PERDU ($($_.Exception.Message.Trim())) - reconnexion..."
            try { $p.Close() } catch {}
            Start-Sleep -Seconds 5
        }
    }
} finally {
    if ($p.IsOpen) { $p.Close() }
    Write-Host "Capture terminee -> $Out"
}
