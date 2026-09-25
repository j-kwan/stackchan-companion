# =============================================================================
# claude-statusline.ps1 — statusline Claude Code qui NOURRIT la bande StackChan
# =============================================================================
# Double emploi : (1) affiche une statusline Claude Code normale, (2) pousse
# des CHAMPS vers le robot -> il « vit » l'activité Claude sans Claude Desktop
# ni BLE (juste le WiFi + l'API). C'est le pont buddy pour Claude Code.
#
# Installation — dans ~/.claude/settings.json :
#   "statusLine": { "type": "command",
#     "command": "pwsh -NoProfile -File C:/.../scripts/dev/claude-statusline.ps1" }
# et définir l'IP : $env:STACKCHAN_IP = '192.168.1.50' (ou ci-dessous).
#
# Claude Code passe un JSON sur STDIN (model, workspace, cost, transcript_path).
#
# LE CONTEXTE N'Y EST PAS. Le schema de ce JSON n'est pas documente et aucun
# champ n'y annonce le remplissage de la fenetre ; ce qui EST documente, c'est
# `transcript_path`. On le lit donc : la derniere entree qui porte un
# `message.usage` donne les jetons du dernier prompt, et leur somme sur la
# fenetre est le pourcentage qu'on publie sous `ctx`.
#
# LE DENOMINATEUR EST LE POINT FAIBLE et il est explicite : $env:CLAUDE_CTX_WINDOW
# d'abord, sinon `autoCompactWindow` de ~/.claude/settings.json, sinon 200000.
# Sur un modele a fenetre de 1 M le defaut de 200 k affiche 100 % en permanence :
# poser la variable est la reponse, pas un bug a diagnostiquer.
#
# ET `claude` NE VAUT 1 QUE SI `ctx` A PU ETRE CALCULE. Une regle « ctx < 20 »
# est vraie quand le champ est ABSENT (un champ inconnu vaut 0), donc un pont
# qui annoncerait sa presence sans fournir le contexte ferait declencher
# joyeusement la regle du contexte vide. La grille `claude` signifie ici « le
# pont livre du contexte », ce dont ces regles ont exactement besoin.
# =============================================================================
$ErrorActionPreference = 'SilentlyContinue'
$Ip = $env:STACKCHAN_IP; if (-not $Ip) { $Ip = '192.168.1.50' }

# --- lire le JSON de Claude Code sur stdin ---
$raw = [Console]::In.ReadToEnd()
$j = $null; try { $j = $raw | ConvertFrom-Json } catch {}

$model = $j.model.display_name; if (-not $model) { $model = 'Claude' }
$dir   = $j.workspace.current_dir
$proj  = if ($dir) { Split-Path $dir -Leaf } else { '' }
$cost  = [double]($j.cost.total_cost_usd)          # $ session (si présent)

# --- fenetre de contexte : combien de jetons tient le modele ---
function Get-CtxWindow {
    if ($env:CLAUDE_CTX_WINDOW) {
        $v = 0; if ([int]::TryParse($env:CLAUDE_CTX_WINDOW, [ref]$v) -and $v -gt 0) { return $v }
    }
    $cfg = Join-Path $env:USERPROFILE '.claude\settings.json'
    if (Test-Path $cfg) {
        try {
            $w = (Get-Content $cfg -Raw | ConvertFrom-Json).autoCompactWindow
            if ($w -gt 0) { return [int]$w }
        } catch {}
    }
    return 200000
}

# --- contexte utilise : la DERNIERE entree du transcript qui porte un usage ---
# On lit la queue et non le fichier : un transcript de session fait des dizaines
# de milliers de lignes et cette fonction tourne a chaque rafraichissement de la
# statusline.
function Get-CtxPct($path, $win) {
    if (-not $path -or -not (Test-Path $path)) { return $null }
    try {
        foreach ($l in [System.Linq.Enumerable]::Reverse(
                        [string[]](Get-Content $path -Tail 400))) {
            if ($l -notmatch '"usage"') { continue }
            $u = ($l | ConvertFrom-Json).message.usage
            if (-not $u) { continue }
            $used = [double]$u.input_tokens + [double]$u.cache_read_input_tokens +
                    [double]$u.cache_creation_input_tokens
            if ($used -le 0) { continue }
            return [Math]::Min(100, [Math]::Round($used / $win * 100))
        }
    } catch {}
    return $null
}

$win = Get-CtxWindow
$ctx = Get-CtxPct $j.transcript_path $win

# --- statusline visible (stdout) ---
$line = "$model"
if ($proj) { $line += "  $proj" }
if ($null -ne $ctx) { $line += "  ctx $ctx%" }
if ($cost -gt 0) { $line += ("  `$" + $cost.ToString('0.00')) }
Write-Output $line

# --- pousse au robot (best-effort, non bloquant, timeout court) ---
# g0 = CONTEXTE (c'est la jauge que lisent les regles livrees), g1 = cout de la
# session plafonne a 5 $. Label = projet.
$costPct = [Math]::Min(100, [int]($cost / 5.0 * 100))
$pairs = @("claude=$(if ($null -ne $ctx) { 1 } else { 0 })")
if ($null -ne $ctx) {
  $pairs += @("ctx=$ctx", "g0=$ctx", "g0l_s=CTX",
              "g0r_s=$([Uri]::EscapeDataString($proj))")
}
$pairs += @("g1=$costPct", "g1l_s=COST")
try {
  Invoke-WebRequest -UseBasicParsing -Method POST `
    ("http://$Ip/api/field?" + ($pairs -join '&')) -TimeoutSec 2 | Out-Null
} catch {}
