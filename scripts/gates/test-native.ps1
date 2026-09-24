# =============================================================================
# test-native.ps1 — StackChan-Companion : lance les tests unitaires natifs
# =============================================================================
# Usage :  .\scripts\gates\test-native.ps1            (tous les tests)
#          .\scripts\gates\test-native.ps1 test_units (un test précis, -f filter)
#
# Pourquoi ce wrapper : l'env PlatformIO `native` a besoin d'un gcc hôte.
# MinGW-w64 (WinLibs) est installé via winget mais PAS ajouté au PATH
# utilisateur — ce script l'ajoute au PATH de SA session uniquement.
# (Pour l'ajouter définitivement : Paramètres Windows > variables d'env.)
# =============================================================================

$ErrorActionPreference = 'Stop'

# gcc WinLibs (installé par : winget install BrechtSanders.WinLibs.POSIX.UCRT)
$mingw = Join-Path $env:LOCALAPPDATA `
    'Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin'
if (-not (Test-Path (Join-Path $mingw 'gcc.exe'))) {
    Write-Error "gcc introuvable ($mingw) — installer : winget install BrechtSanders.WinLibs.POSIX.UCRT"
}
$env:PATH = "$mingw;$env:PATH"

$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'

# Filtre optionnel : nom d'un dossier de test (ex: test_units)
if ($args.Count -ge 1) {
    & $pio test -e native -f $args[0]
} else {
    & $pio test -e native
}
exit $LASTEXITCODE
