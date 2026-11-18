# check-all.ps1 — every automated gate this project has, in one command.
#
# Five checks existed and were run BY HAND, which means they were run when
# someone remembered. This is the single entry point, and `-Hook` installs it
# as a git pre-push so remembering stops being part of the process.
#
#   .\scripts\check-all.ps1            # everything (builds if needed)
#   .\scripts\check-all.ps1 -Fast      # skip the firmware build + A2.22
#   .\scripts\check-all.ps1 -Hook      # install the pre-push hook, run nothing
#
# Order is deliberate: the cheap textual checks first, the native tests next,
# and the firmware build last — a failure should cost as little time as
# possible before it is reported.
param(
    [switch]$Fast,
    [switch]$Hook
)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Hook) {
    $hookDir = Join-Path $root '.git\hooks'
    if (-not (Test-Path $hookDir)) {
        Write-Host "pas de .git/hooks ici" -ForegroundColor Red; exit 1
    }
    $path = Join-Path $hookDir 'pre-push'
    # POSIX sh: git for Windows runs hooks through its bundled shell.
    $body = @'
#!/bin/sh
# Installed by scripts/check-all.ps1 -Hook
exec powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-all.ps1
'@
    Set-Content -Path $path -Value $body -Encoding ASCII -NoNewline
    Write-Host "pre-push installe : $path" -ForegroundColor Green
    Write-Host "  (contourner une fois : git push --no-verify)"
    exit 0
}

$failures = @()

function Step($name, $script) {
    Write-Host ""
    Write-Host "== $name" -ForegroundColor Cyan
    # SENTINEL, because PowerShell does NOT update $LASTEXITCODE when a command
    # cannot be LAUNCHED: it keeps the previous step's value. With
    # $ErrorActionPreference = 'Continue' the error scrolls past and a gate that
    # never ran is recorded as PASSED — verified empirically on 2026-08-02, and
    # every step but the first was exposed (only the first fails safe, because
    # $LASTEXITCODE starts $null). On a machine without python on PATH the whole
    # script printed TOUT PASSE having verified nothing, as a pre-push hook.
    $global:LASTEXITCODE = $null
    & $script
    if ($null -eq $global:LASTEXITCODE) {
        $script:failures += $name
        Write-Host "   NON EXECUTE ($name) - commande introuvable ?" -ForegroundColor Red
    } elseif ($LASTEXITCODE -ne 0) {
        $script:failures += $name
        Write-Host "   ECHEC ($name)" -ForegroundColor Red
    }
}

# Fail EARLY and clearly rather than once per step.
foreach ($exe in @('python', "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe")) {
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) {
        Write-Host "outil introuvable : $exe" -ForegroundColor Red
        exit 1
    }
}

$py = 'python'

# NOT here: scripts/gates/check-comments-only.py. It proves a change touched comments
# ONLY and therefore exits 1 as soon as code changed — which is the normal case
# for every real commit. It is a tool for one kind of sweep (a translation, a
# rule renumbering), not a gate, and putting it in this list made the gate fail
# on its own first run.
Step 'parite EN/FR des docs'      { & $py scripts/gates/check-doc-parity.py }
Step 'contraste des themes'       { & $py scripts/gates/check-contrast.py }
Step 'copies vendorees'           { & $py scripts/gates/check-vendored.py }
Step 'constantes miroir'          { & $py scripts/gates/check-mirrors.py }
# Une cle de tuning sans controle dans la console est une cle que personne ne
# trouvera : servie par /api/tuning, donc elle a l air finie, et invisible pour
# qui n ouvre pas Swagger.
Step 'couverture de la console'   { & $py scripts/gates/check-console.py }
# Le symetrique cote INVITES. Un reglage y traverse quatre listes ecrites a la
# main, et la quatrieme tronque le fichier : une cle relue du yaml mais absente
# de saveConfig n est pas seulement non persistee, elle est DETRUITE a la
# premiere sauvegarde depuis /config (panne auto_bright du 08-05).
Step 'reglages des bins invites'  { & $py scripts/gates/check-guest-config.py }
# Deux documents ENUMERENT ce que le code definit : les 44 routes du routeur
# et les 30 valeurs de eEmotions. Une route ajoutee sans ligne dans API.md est
# introuvable autrement qu en lisant le C++ ; une ligne sans route derriere
# promet une API qui n existe pas.
Step 'couverture des docs'        { & $py scripts/gates/check-doc-coverage.py }
# Une ligne de rules.txt que le parseur refuse est ABSENTE, sans erreur ni
# trace : le robot ne reagit simplement pas. Tous les fichiers de la carte,
# pas seulement celui de la personnalite active.
Step 'fichiers de regles'         { & $py scripts/gates/check-rules.py }

# EXPECTED, and asserted — not merely run. `pio test` exits 0 when everything
# it RAN passed, which says nothing about what it did not run: a suite that
# fails to build, or a directory that stops being picked up, simply vanishes
# and the gate still reports success. Seen live on 2026-08-02: one run reported
# "120 test cases" where the next reported 135, both green.
# Raise these two numbers when suites or cases are added — that is the point.
$MIN_SUITES = 37
$MIN_TESTS  = 429

Step 'tests natifs' {
    # ONE RETRY, and it is announced. The native harness is intermittently
    # unreliable on Windows when it runs all suites in sequence: measured on
    # 2026-08-02, four consecutive runs gave 135 / 64 / 96 / 135 cases, with
    # 0 / 7 / 3 / 0 suites reported ERRORED. Every one of those suites PASSES
    # when run alone (`pio test -e native -f test_behavior`), so this is the
    # harness or the filesystem, not the code.
    # A gate that fails at random is a gate people learn to ignore, which is
    # worse than no gate — hence the retry. But a retry that hides the flake is
    # how a real regression gets waved through, hence the warning: the second
    # attempt is always reported.
    function Invoke-NativeTests {
        $o = & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native 2>&1
        $c = $LASTEXITCODE
        $j = $o -join "`n"
        # DEUX formes de resume, et la seconde manquait :
        #   "150 test cases: 150 succeeded"
        #   "151 test cases: 9 failed, 141 succeeded"
        # L'ancienne regex exigeait "succeeded" JUSTE apres les deux-points,
        # donc un run avec des echecs rendait Ran = 0 -- et le gate annoncait
        # "0 cas, c'est peut-etre le harnais, relancez" pour 9 assertions
        # REELLEMENT tombees (vu le 08-02). Un gate qui deguise un echec en
        # flakiness est pire qu'absent : il apprend a relancer au lieu de lire.
        # LA DERNIERE occurrence, et sans exiger « succeeded » : `Match` prend la
        # PREMIERE, et `.` n'inclut pas le saut de ligne, donc un resume sur
        # plusieurs lignes ou un second bloc de resume donnait un compte qui
        # n'etait pas celui du run. Le jumeau bash lit deja la derniere ; les
        # deux alimentent le MEME seuil MIN_TESTS, que check-mirrors.py tient —
        # les faire diverger sur la mesure vide le miroir de son sens.
        $mm = [regex]::Matches($j, '(\d+) test cases:')
        $m  = if ($mm.Count) { $mm[$mm.Count - 1] } else { $null }
        $mfa = [regex]::Matches($j, '(\d+) failed')
        $mf = if ($mfa.Count) { $mfa[$mfa.Count - 1] } else { $null }
        [pscustomobject]@{
            Out    = $o
            Code   = $c
            Suites = ([regex]::Matches($j, '(?m)^native\s+test_\S+')).Count
            Ran    = if ($m)  { [int]$m.Groups[1].Value }  else { 0 }
            Failed = if ($mf) { [int]$mf.Groups[1].Value } else { 0 }
            Errored= ([regex]::Matches($j, '(?m)^native\s+\S+\s+ERRORED')).Count
        }
    }
    $r = Invoke-NativeTests
    # Un VRAI echec d'assertion ne se retente pas : la seconde tentative
    # donnerait le meme resultat, apres avoir laisse croire a un alea.
    # Seule l'incompletude (suites manquantes, ERRORED) justifie le retry.
    if ($r.Failed -gt 0) {
        Write-Host "   $($r.Failed) test(s) en ECHEC - ce n'est pas le harnais" -ForegroundColor Red
        $r.Out | Select-String -Pattern 'FAILED' | Select-Object -First 12
    } elseif ($r.Suites -lt $MIN_SUITES -or $r.Ran -lt $MIN_TESTS -or $r.Code -ne 0) {
        Write-Host ("   incomplet ($($r.Ran) cas, $($r.Errored) suite(s) ERRORED)" +
                    " - SECONDE TENTATIVE") -ForegroundColor Yellow
        $r = Invoke-NativeTests
    }
    $r.Out | Select-Object -Last 3
    if ($r.Failed -gt 0) {
        $global:LASTEXITCODE = 1
    } elseif ($r.Suites -lt $MIN_SUITES -or $r.Ran -lt $MIN_TESTS) {
        Write-Host ("   SUITES/CAS MANQUANTS : $($r.Suites) suites (attendu >= $MIN_SUITES), " +
                    "$($r.Ran) cas (attendu >= $MIN_TESTS), $($r.Errored) ERRORED") -ForegroundColor Red
        Write-Host "   un vert avec des tests ABSENTS n'est pas un vert."
        Write-Host "   si une suite passe SEULE (-f <suite>), c'est le harnais, pas le code."
        $global:LASTEXITCODE = 1
    } else {
        Write-Host "   $($r.Suites) suites, $($r.Ran) cas"
        $global:LASTEXITCODE = $r.Code
    }
}

if (-not $Fast) {
    # SEPT cibles. Les deux variantes `-fire` compilent la MEME source que leur
    # parent avec SCE_INPUT_BUTTONS et sans companion : elles sont ici parce
    # que c'est exactement le genre de variante qui pourrit en silence —
    # personne ne la compile tant qu'il n'a pas la carte sous la main, et une
    # garde #if mal fermee ne se voit pas sur la cible principale.
    Step 'build des huit firmwares' {
        & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run `
            -e companion -e flight-radar -e ha-remote -e flight-radar-fire `
            -e space -e space-fire -e led-fluid -e led-fluid-fire 2>&1 |
            Select-Object -Last 8
    }
    # A2.22 reads the LINKED binary, so it can only run after the build.
    # LES DEUX backends : A2.22 est une regle de GENERATION DE CODE, donc la
    # verifier sur l'ESP32-S3 ne dit rien de l'ESP32 du Fire.
    Step 'A2.22 (point d appel unique, dans le binaire)' {
        # `space` joins the list on 08-04 with twelve points. Leaving an env
        # OUT is silent by construction (`EXPECT.get(env, [])` contributes
        # zero checks), which is the same omission the script's own comment
        # warns about for the companion — and it hid twelve points on the bin
        # with the most run-loop drawing in the project.
        & $py scripts/gates/check-a222.py companion flight-radar flight-radar-fire `
            space space-fire ha-remote led-fluid led-fluid-fire
    }
}

Write-Host ""
if ($failures.Count -eq 0) {
    Write-Host "TOUT PASSE" -ForegroundColor Green
    exit 0
}
Write-Host ("ECHECS : " + ($failures -join ', ')) -ForegroundColor Red
exit 1
