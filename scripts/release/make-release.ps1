# make-release.ps1 — builds every firmware and assembles the files of a GitHub
# release in dist/<version>/. Uploading them is a separate, deliberate step.
#
#   .\scripts\release\make-release.ps1                 # version from library.json
#   .\scripts\release\make-release.ps1 -Version v1.1.0
#
# What the user does with each file is docs/INSTALL.md — keep the two in step:
# a file renamed here is a file that page no longer finds.
#
# Three refusals, each one a way a release could ship something it must not:
#   - a dirty working tree: the binaries would not match any commit;
#   - SCE_WIFI_SSID / SCE_WIFI_PASS set: the -fire envs COMPILE them in
#     (platformio.ini, ${sysenv.…}), so a maintainer's home WiFi would ship
#     inside a public .bin. Cleared for this process as well, belt and braces;
#   - a missing build output: never publish a partial set.
# The SD zip comes from `git archive HEAD sdcard`, never from the working
# directory: only committed template files can end up in it, whatever else
# lies on disk under sdcard/.
param([string]$Version = "")

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

if (-not $Version) {
    $Version = "v" + (Get-Content library.json -Raw | ConvertFrom-Json).version
}
if ($Version -notmatch '^v\d+\.\d+\.\d+([-.][0-9A-Za-z.]+)?$') {
    throw "Version '$Version' : attendu vX.Y.Z"
}

$dirty = git status --porcelain
if ($LASTEXITCODE -ne 0) { throw "git status en echec" }
if ($dirty) { throw "Arbre de travail modifie : committer d'abord." }
if ($env:SCE_WIFI_SSID -or $env:SCE_WIFI_PASS) {
    throw "SCE_WIFI_SSID/SCE_WIFI_PASS definies : elles seraient compilees dans les bins -fire."
}
$env:SCE_WIFI_SSID = ""; $env:SCE_WIFI_PASS = ""

# env -> kind. "app" = plain application image (SD card / OTA);
# "factory" = merged image flashed at 0x0 (bootloader + partitions + otadata + app).
$envs = [ordered]@{
    "companion"         = @("factory", "app")
    "flight-radar"      = @("app")
    "space"             = @("app")
    "ha-remote"         = @("app")
    "led-fluid"         = @("app")
    "flight-radar-fire" = @("factory")
    "space-fire"        = @("factory")
    "led-fluid-fire"    = @("factory")
}

# CLEAN first: an incremental build reuses objects compiled whenever, under
# whatever environment was set then. A release is built from this commit and
# nothing else.
foreach ($e in $envs.Keys) {
    Write-Host "== build $e" -ForegroundColor Cyan
    & $pio run -e $e -t clean | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "clean $e en echec" }
    & $pio run -e $e
    if ($LASTEXITCODE -ne 0) { throw "build $e en echec" }
}

$dist = Join-Path $root "dist\$Version"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null

foreach ($e in $envs.Keys) {
    $b = ".pio\build\$e"
    foreach ($k in $envs[$e]) {
        if ($k -eq "factory") {
            $src = "$b\firmware.factory.bin"; $dst = "$e-$Version-factory.bin"
        } else {
            $src = "$b\firmware.bin"; $dst = "$e.bin"
        }
        if (-not (Test-Path $src)) { throw "sortie absente : $src" }
        Copy-Item $src (Join-Path $dist $dst)
    }
}

# SD card: committed template + companion.bin at the root + guest apps in /bins/.
$stage = Join-Path $env:TEMP "sce-release-sd-$Version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
$tar = Join-Path $env:TEMP "sce-release-sd-$Version.tar"
git archive --format=tar -o $tar HEAD sdcard
if ($LASTEXITCODE -ne 0) { throw "git archive en echec" }
tar -xf $tar -C $stage
if ($LASTEXITCODE -ne 0) { throw "extraction du modele SD en echec" }
Remove-Item $tar
$sd = Join-Path $stage "sdcard"
Copy-Item (Join-Path $dist "companion.bin") $sd
foreach ($g in "flight-radar", "space", "ha-remote", "led-fluid") {
    Copy-Item (Join-Path $dist "$g.bin") (Join-Path $sd "bins")
}
Compress-Archive -Path (Join-Path $sd "*") -DestinationPath (Join-Path $dist "sdcard-$Version.zip")
Remove-Item -Recurse -Force $stage

# Checksums, in the format `sha256sum -c` reads.
$sums = Get-ChildItem $dist -File | Sort-Object Name | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name
}
[IO.File]::WriteAllLines((Join-Path $dist "SHA256SUMS.txt"), [string[]]$sums)

$commit = git rev-parse --short HEAD
Write-Host "`n$Version pret dans dist\$Version (commit $commit) :" -ForegroundColor Green
Get-ChildItem $dist -File | Sort-Object Name | Format-Table Name, Length -AutoSize
