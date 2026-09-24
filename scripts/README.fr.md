> [English](README.md) · **Français**

# `scripts/` — les portails, les corvées, l'étape de build

Une commande en tête, et le reste trié par **qui l'exécute** — parce que c'est
la question qu'on se pose vraiment en ouvrant ce répertoire.

```
scripts/
  check-all.ps1     le point d'entrée unique — c'est celui-là
  gates/            ce que check-all lance, et rien d'autre
  dev/              ce que VOUS lancez à la main pendant une session
  build/            ce que PlatformIO lance tout seul
  release/          ce que le mainteneur lance pour sortir une release
```

## `check-all.ps1` — la commande unique

```powershell
.\scripts\check-all.ps1          # tout : portails, 37 suites natives, huit firmwares, A2.22
.\scripts\check-all.ps1 -Fast    # saute le build des firmwares et la passe A2.22
.\scripts\check-all.ps1 -Hook    # l'installe en hook git pre-push, ne lance rien
```

## `gates/` — lancés par `check-all`, et par rien d'autre

C'est la règle, et c'est ce qui rend le répertoire lisible : un fichier ici est
un fichier que le portail lance. Ajouter un contrôle, c'est ajouter une ligne
`Step` ; un contrôle qu'aucun `Step` n'appelle est un poids mort qui se fait
passer pour une garantie.

| Portail | Ce qu'il refuse de laisser passer |
|---|---|
| [`check-doc-parity.py`](gates/check-doc-parity.py) | un document anglais sans son jumeau `.fr.md`, ou un jumeau dont les titres ont dérivé |
| [`check-contrast.py`](gates/check-contrast.py) | une couleur de thème qui échoue au seuil de lisibilité sur la dalle — les thèmes de flight-radar et de space, et la palette **Liquid Glass** partagée par le launcher, le hall invité et ha-remote, jugée sur la CARTE où elle est dessinée. Trois fichiers affirmaient `RGAA ≥ 4,5:1` en commentaire et aucun des trois n'était mesuré |
| [`check-vendored.py`](gates/check-vendored.py) | l'une des **cinq** copies vendorées dans `SceGuest.h` (`Yaml.h`, `I18n.h`, `FirmwareInfo.h`, `Trace.h`, `Gesture.h`) qui dérive de son original dans `firmware/common/`. Le stub doit rester copiable seul dans un projet tiers, c'est la seule raison d'être de ces copies — et la raison pour laquelle il faut les surveiller |
| [`check-mirrors.py`](gates/check-mirrors.py) | un fait écrit deux fois qui a cessé de s'accorder : les bornes servo que l'éditeur de chorégraphies redit à la main, `MIN_SUITES`/`MIN_TESTS` entre les jumeaux `.ps1`/`.sh`, les VID/PID USB des cartes, le gabarit `rules.txt` face à son littéral C, chaque défaut de curseur de la console face à `Tuning.h`, et les deux `CLAUDE.md`. Il fait respecter en prime deux règles qui ne sont pas des constantes : un `main.cpp` d'invité qui appelle `xTaskCreate` **doit** brancher `guest.netGuard`, et un qui appelle `SD.begin` **doit** tenir un `sce::SdWatch` — l'arrêt coopératif est une discipline qui avait été appliquée à un bin et silencieusement oubliée dans un autre, donc elle est vérifiée plutôt que mémorisée |
| [`check-console.py`](gates/check-console.py) | une clé `Tuning` sans contrôle dans la console embarquée : servie par l'API, donc elle a l'air finie, et introuvable pour qui n'ouvre pas Swagger. Il couvre les **79** clés de `Tuning::table()`, avec exactement deux exemptions nommées (`band_mode`, piloté par `/api/statusbar` ; `cfg_version`, le numéro de schéma interne) — le plancher bouge tout seul quand une clé est ajoutée |
| [`check-guest-config.py`](gates/check-guest-config.py) | un réglage d'invité qui sort de l'une des quatre listes écrites à la main où il doit figurer : `addSetting` le déclare, `settingGet` l'affiche, `settingSet` l'accepte, `saveConfig` l'écrit. La quatrième est la dangereuse — `saveConfig` **tronque** le fichier et le régénère depuis sa propre liste, donc une clé que le yaml sait relire mais que la sauvegarde n'écrit pas n'est pas seulement non persistée, elle est DÉTRUITE à la première sauvegarde depuis `/config`, y compris lorsqu'elle avait été écrite à la main. C'est la panne `auto_bright` du 08-05, dont la leçon était écrite en commentaire et jamais outillée. Il compte aussi les champs face à `MAX_SETTINGS`, qui n'annonce son propre plafond que sur la console série. Deux exemptions nommées : `track` (une commande, pas un réglage) et `ll2_poll_min` (un alias accepté, réécrit sous son nom canonique) |
| [`check-doc-coverage.py`](gates/check-doc-coverage.py) | un document qui ÉNUMÈRE ce que le code définit et a cessé d'y correspondre : les 44 couples méthode+chemin qu'enregistre `WebApi::route()`, face à `docs/reference/API.fr.md`, et les 30 valeurs de `eEmotions` face à `docs/reference/EMOTIONS.fr.md`. Dans les deux sens — une ligne manquante cache une route, une ligne en trop promet une API qui n'existe pas |
| [`check-a222.py`](gates/check-a222.py) | une forme dessinée depuis deux points d'appel dans le binaire compilé (A2.22) |

Deux fichiers ici ne sont **pas** dans cette liste, volontairement :

- [`check-comments-only.py`](gates/check-comments-only.py) prouve qu'un
  changement n'a touché *que* des commentaires, donc il sort en 1 dès que du
  code a changé — le cas normal de tout vrai commit. C'est un outil pour un
  type de passe (une traduction, une renumérotation de règles), pas un portail.
  Il a été dans la liste une fois, et faisait échouer le portail dès sa
  première exécution.
- [`test-native.ps1`](gates/test-native.ps1) lance les suites natives seules
  quand on veut aller vite ; `check-all` lance `pio test` lui-même pour pouvoir
  affirmer les NOMBRES de suites et de cas — **37 suites, 427 cas**
  (`$MIN_SUITES`/`$MIN_TESTS`) — ce que le code de sortie de `pio` ne dit pas :
  il sort 0 sur tout ce qu'il a lancé, et ne dit rien d'une suite qui a cessé
  d'être ramassée.

```powershell
.\scripts\gates\test-native.ps1                 # les 37 suites
.\scripts\gates\test-native.ps1 test_soundviz   # une seule
```

## `dev/` — à la main, contre une carte

| Script | Usage |
|---|---|
| [`find-port.ps1`](dev/find-port.ps1) | résout un port série **par VID/PID USB**, jamais par numéro. Avec deux cartes branchées, un upload sans `--upload-port` choisit tout seul et peut écraser le companion |
| [`test-rules.ps1`](dev/test-rules.ps1) | exerce le fichier de règles que le robot exécute VRAIMENT : il lit `/api/rules` au lieu de coder une liste en dur, donc il suit un changement de personnalité et un fichier modifié. Il **saute les champs portés par un capteur, nommément, en disant pourquoi** — `loop()` les republie environ chaque seconde, donc un `batt=3` poussé a disparu avant qu'un sustain puisse le voir, et un PASS sur l'un d'eux parlerait du capteur, pas de la règle |
| [`endurance-log.ps1`](dev/endurance-log.ps1) | run long : échantillonne `/api/status` et dénonce une dérive du tas ou des piles |
| [`test-mag.ps1`](dev/test-mag.ps1) | exerce le magnétomètre par HTTP — ouvrir le port série RESET la carte en USB natif, c'est ainsi que la première session a perdu la moitié de ses rotations |
| [`statusbar-push.ps1`](dev/statusbar-push.ps1) · [`.sh`](dev/statusbar-push.sh) | pousse un champ vers la bande de statut du robot — Windows, macOS et Linux |
| [`claude-statusline.ps1`](dev/claude-statusline.ps1) · [`.sh`](dev/claude-statusline.sh) | le pont statusline de Claude Code, sur les trois plateformes — le `.sh` est écrit pour bash 3.2, macOS l'exécute donc tel quel (installation : [`docs/reference/PLUGINS.fr.md`](../docs/reference/PLUGINS.fr.md)) |

```powershell
.\scripts\dev\find-port.ps1 -List              # ce qui est branché
pio run -e space-fire -t upload --upload-port (.\scripts\dev\find-port.ps1 -Board fire)
```

## `build/` — lancé par PlatformIO, pas par vous

[`gen_console_gz.py`](build/gen_console_gz.py) précompresse les pages web
embarquées et est câblé en action `pre:` dans `platformio.ini`. Il est ici
plutôt que dans `gates/` parce que rien d'une session normale ne l'appelle :
c'est le build qui le fait.

## `release/` — lancé par le mainteneur, une fois par release

[`make-release.ps1`](release/make-release.ps1) compile les huit firmwares de
zéro (jamais un build incrémental) et
rassemble les fichiers d'une release GitHub dans `dist/<version>/` (ignoré par
git) : le companion en image USB complète et en `companion.bin`, les quatre
applis invitées, les trois images M5Stack Fire, une carte SD prête à
décompresser et `SHA256SUMS.txt`. Ce que l'utilisateur fait de chaque fichier
est dans [`docs/INSTALL.fr.md`](../docs/INSTALL.fr.md) : un fichier renommé dans
le script se renomme aussi là-bas.

Il refuse un arbre de travail modifié (les binaires ne correspondraient à aucun
commit) et des `SCE_WIFI_SSID`/`SCE_WIFI_PASS` définies, parce que les
environnements `-fire` les compilent dans le binaire. Le zip SD est tiré de
`git archive` : seuls les fichiers de modèle committés peuvent s'y retrouver.
Téléverser les fichiers et poser l'étiquette sont des étapes séparées et
délibérées. Pas de jumeau `.sh` : c'est une corvée de mainteneur, pas un besoin
d'utilisateur du projet.

```powershell
.\scripts\release\make-release.ps1                  # version tirée de library.json
.\scripts\release\make-release.ps1 -Version v1.1.0
```

## Linux

Chaque script dont un utilisateur Linux a besoin a un jumeau `.sh` à côté, avec
les mêmes arguments. Les portails eux-mêmes sont en Python et ont toujours été
portables.

```bash
./scripts/check-all.sh
./scripts/gates/test-native.sh test_soundviz
./scripts/dev/find-port.sh --list
```
