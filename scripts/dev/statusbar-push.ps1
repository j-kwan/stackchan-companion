# =============================================================================
# statusbar-push.ps1 — pousse des CHAMPS vers la bande de statut StackChan
# =============================================================================
# Le robot n'affiche que des champs (contrat « sources -> champs -> widgets »).
# N'importe quelle source (script, tâche planifiée, Home Assistant, NAS...)
# pousse ses valeurs ici. Générique : passe des paires clé=valeur.
#
# Exemples :
#   # 3 jauges Claude (mode Jauges : POST /api/statusbar?mode=3)
#   .\statusbar-push.ps1 -Ip 192.168.1.50 -Fields `
#     'g0=62','g0l=CTX','g0r=stackch.','g1=41','g1l=5H','g1r=1h24',`
#     'g2=88','g2l=7J','g2r=2j05'
#
#   # une notification
#   .\statusbar-push.ps1 -Ip 192.168.1.50 -Say 'Lave-linge termine' -SayMs 6000
#
#   # boucler toutes les 30 s (jauges depuis un provider quelconque)
#   while ($true) { .\statusbar-push.ps1 -Ip $ip -Fields (Get-MyGauges); Start-Sleep 30 }
#
# Les valeurs NUMERIQUES sont poussées en float ; les TEXTES en chaîne.
# =============================================================================
param(
  [string]   $Ip     = $env:STACKCHAN_IP,
  [string[]] $Fields = @(),          # 'cle=valeur' ...
  [int]      $Mode   = -1,           # -1 = ne pas changer ; 0..3 = /api/statusbar
  [string]   $Say    = $null,
  [int]      $SayMs  = 4000,
  [string]   $Auth   = $null         # 'user:pass' si Basic Auth actif
)

if (-not $Ip) { Write-Error "IP requise (-Ip ou `$env:STACKCHAN_IP)"; exit 1 }
$base = "http://$Ip"
$headers = @{}
if ($Auth) {
  $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Auth))
  $headers['Authorization'] = "Basic $b64"
}

function Post([string]$path) {
  try { Invoke-WebRequest -UseBasicParsing -Method POST "$base/$path" -Headers $headers -TimeoutSec 5 | Out-Null; $true }
  catch { Write-Warning "POST $path : $($_.Exception.Message)"; $false }
}

if ($Mode -ge 0) { Post "api/statusbar?mode=$Mode" | Out-Null }

if ($Fields.Count -gt 0) {
  # /api/field?k1=v1&k2=v2... ; les chaînes non numériques sont détectées
  # côté firmware (sinon suffixer la clé de _s pour forcer le texte).
  $pairs = @()
  foreach ($f in $Fields) {
    $i = $f.IndexOf('=')
    if ($i -lt 1) { continue }
    $k = $f.Substring(0, $i); $v = $f.Substring($i + 1)
    # force le texte si non numérique (evite qu'un label "5H" devienne 5)
    # CULTURE INVARIANTE, et c'est le point : [double]::TryParse suit la culture
    # courante, donc sur un Windows fr-FR "62.5" n'est PAS un nombre et partait
    # suffixe _s, en TEXTE, vers une jauge qui attend un float. La jauge restait
    # a zero sans que rien ne le dise.
    $isNum = [double]::TryParse($v, [Globalization.NumberStyles]::Float,
                                [Globalization.CultureInfo]::InvariantCulture,
                                [ref]([double]0))
    if (-not $isNum) { $k = "$k`_s" }
    $pairs += "$([Uri]::EscapeDataString($k))=$([Uri]::EscapeDataString($v))"
  }
  if ($pairs.Count) { Post ("api/field?" + ($pairs -join '&')) | Out-Null }
}

if ($Say) { Post ("api/say?ms=$SayMs&text=" + [Uri]::EscapeDataString($Say)) | Out-Null }
