> [English](README.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# StackChan-Companion

[![Licence](https://img.shields.io/badge/licence-AGPL--3.0-green)](LICENSE)
[![Plateforme](https://img.shields.io/badge/plateforme-M5Stack%20StackChan%20K151-red)](https://docs.m5stack.com/en/StackChan/)

> **Projet personnel.** Développé seul, sur mon propre robot, sur mon temps
> libre — avec un usage important de l'assistance IA au code (Claude Code)
> tout au long de la conception, de l'implémentation et des docs. Ce n'est
> pas un produit officiel M5Stack ou StackChan, pas de SLA de support, pas
> d'engagement de feuille de route. À lire avec ça en tête ; les issues et
> PR restent les bienvenues.

Firmware compagnon pour **StackChan** — M5Stack CoreS3, kit K151. Il donne au
robot des animations fluides et naturelles, à l'identité franchement robotique.

La direction artistique vient de **Wall-E, Cozmo et Vector** : les yeux mènent
et la tête suit, les saccades sont sèches et les fixations tenues, les formes
se compriment et s'étirent, et les gestes mécaniques gardent une métronomie
assumée.

Le reste est dans le tableau ci-dessous — VOR gyroscopique, 30 expressions,
15 danses, une API REST avec console embarquée, un launcher `.bin` sur carte
SD, et CRT / LEDs / son activables.

<video src="https://github.com/user-attachments/assets/d958b53c-875e-495d-b876-cf4547b22917" controls muted></video>

![La console web embarquée — onglet Pilotage](docs/assets/screenshots/Webconsole%20-%20pilot.png)

<details>
<summary>Le reste de la console — Options, Fichiers, Système</summary>

![Onglet Options — écran/LEDs/son, caméra, réglages fins](docs/assets/screenshots/Webconsole%20-%20options.png)
![Onglet Fichiers — la carte SD, unifiée](docs/assets/screenshots/Webconsole%20-%20files.png)
![Onglet Système — NTP, CORS, WiFi, étalonnage VOR, mise à jour OTA, langue](docs/assets/screenshots/Webconsole%20-%20system.png)

</details>

---

## Fonctionnalités

| Fonctionnalité | Détail |
|---|---|
| **30 expressions** | 18 portées d'esp32-eyes + 12 de notre cru (Dead, Excited ✦, Blush, Squint…) ; asymétries miroitées aléatoirement, formes dynamiques (Sad « regard ciel », Curious « œil de bord ») |
| **VOR gyroscopique** | Réflexe vestibulo-oculaire : contre-rotation immédiate (gyro), saccades de rattrapage (nystagmus), tenue d'inclinaison ; mapping capteur **réglable à chaud** |
| **Idle vivant** | Fixation → saccade sèche → micro-overshoot ; micro-jitter sur fixations longues ; blinks log-uniformes, politiques par expression |
| **Sleepy « lutte »** | Droop lent → chute → réouverture laborieuse plafonnée → sursaut 1/4 |
| **15 danses** | Keyframes servo+expression+paupières+regard unifiées ; sens miroité aléatoirement ; personnalisables via CSV sur carte SD |
| **Réflexes préemptifs** | Secousse → Scared, soulèvement → Curious + « pieds ballants » (couple relâché) — coupent TOUT immédiatement |
| **Gestes IMU** | Double-tap (pic accel) → Happy + wink ; posé écran-vers-le-sol soutenu → Sleepy tenu (mise en veille) |
| **Gestes tactiles** | Écran : swipe G/D → émotion ±1, tap → blink/wink, swipe haut → danse ; caresse tête Si12T → Happy/Glee. **Interactions manuelles prioritaires** : elles coupent la danse en cours (règle A2.5). Swipe G/D **dans la bande de statut** → fait défiler les modes (persisté) |
| **Batterie + capteurs** | Jauge PMIC (%, charge) + jauge INA226 (tension/courant) + volume nocturne (coucher→lever réels du soleil, NTP + `lat`/`lon`) + **luminosité écran auto** et **mode nuit** (capteur LTR-553 : dans le noir, Sleepy domine la roulette — le robot somnole ; réveil au retour de la lumière) + cap magnétique (BMM150) — télémétrie `GET /api/sensors` |
| **Bus I2C sûr** | Verrou FreeRTOS court sur le bus 11/12 partagé (IMU/AXP/SCCB/LEDs/touch) : le VOR reste vivant pendant le flux caméra (seule la rafale de config SCCB à l'allumage est exclusive, ~0,5 s) |
| **Caméra (option)** | GC0308 pour Home Assistant / Frigate : vue **live légère** (320×240 compressé — commandes réactives même en streaming) + instantané **VGA pleine qualité** `?full=1` + **flux MJPEG** ; tâche dédiée, off par défaut, auto-extinction, réglages à chaud ([`docs/integrations/HOMEASSISTANT.md`](docs/integrations/HOMEASSISTANT.fr.md)) |
| **Servos débrayables** | Option `servos` : couple relâché (tête molle, manipulable), les yeux continuent — reprise douce |
| **Bande de statut & plugins** | Bas d'écran piloté par des **champs** (`/api/field`) : un **visualiseur son** (un oscilloscope déclenché sur les deux micros, en trois habillages), modes jauges/minuteur/pomodoro + **icônes masquables** + infos debug en option + notifications défilantes (`/api/say`) ; **moteur de règles** `champ→émotion/danse` chargeable depuis la SD (`rules.txt`, hot-reload ; `GET /api/rules` liste la table **telle que chargée**, et la console a une section Règles à elle) — plugins réactifs communautaires sans recompiler. Pont **Claude Code** via statusline ([`docs/reference/PLUGINS.md`](docs/reference/PLUGINS.fr.md), [`docs/reference/STATUSBAR.md`](docs/reference/STATUSBAR.fr.md)) |
| **Import / Export SD** | Télécharger/remplacer/supprimer `config.yaml`, règles, chorégraphies et binaires via l'API (`/api/sd/*`) et la console (chemins whitelistés) |
| **Effets overlay** | Blush (joues rosées de style anime, suivent le regard), sparkles ✦, goutte de sueur — par émotion |
| **Effet CRT** | Scanlines + rémanence phosphore + halo + flicker (LUT, activable) |
| **LEDs + son** | Emphase LED émotionnelle (WS2812×12) + chirps par valence (off par défaut) |
| **Tête vers le bruit** | Option `sound_track` (2 micros ES7210) : virage ∝ déséquilibre G/D, sursaut `shocked` sur choc fort ; **sourdine pendant le mouvement servo** (le bruit d'engrenages ne relance pas de virage) — off par défaut |
| **Console web embarquée** | `http://<ip>/` — zéro CDN (fonctionne en AP), design « Liquid Glass » homogène. **Quatre onglets** — Pilotage (émotions, danses, tête, bande de statut, règles), Options (interrupteurs groupés, caméra live, réglages fins), Fichiers (**gestionnaire SD unifié**), Système (NTP, **accès cross-origin**, WiFi, VOR, Basic Auth, **mise à jour OTA**, langue) — au-dessus d'une bande de télémétrie vivante pleine largeur (chips d'état + graphe heap/frame + les pastilles d'icônes du bandeau) que tous les onglets partagent. L'onglet est dans le hash de l'URL : un rechargement retombe où on était |
| **REST API + Swagger** | `/api/*` + OpenAPI ; **tuning à chaud persisté SD** (**73 clés**, migration de schéma automatique). Un portail prouve que chacune est atteignable depuis la console : une clé que l'API sert mais que la console n'affiche jamais a l'air finie, alors qu'en pratique on ne la trouve qu'en ouvrant Swagger |
| **Identité du build** | `GET /api/firmware` dit quelle partition OTA a réellement démarré (`slot`), signe **ce build précis** (`sha` = sha256 de l'ELF applicatif, `console` = empreinte de la console embarquée — toutes deux reproductibles depuis une copie de travail, par `sha256sum` et `gen_console_gz.py --check`), et pourquoi la carte a démarré la dernière fois (`reset` : `panic`/`task_wdt`/`brownout` nomment un plantage). Un téléversement USB écrit UN slot et ne touche jamais `otadata` : un flash qui annonce la réussite ne prouve pas que la carte le démarre |
| **Trace debug à chaud** | Narration série étiquetée (`net`/`cfg`/`http`/`sd`/`ui`/`task`, lignes `[dbg][tag] +millis …`) activable **sans reflasher** — dans tous les firmwares du dépôt : le companion (clé de tuning `debug`, persistée, appliquée en une frame) et les quatre bins invités (case Debug sur leur `/config`, gardée en NVS). Les moments qui méritent une trace sont ceux où un reflash détruirait la preuve. Aucun secret : les URL sont coupées à la query string, les jetons rapportés présents/absents seulement |
| **Launcher .bin** | Swipe haut→bas → menu SD `/bins/` ; gestion aussi par API (upload/delete/launch/stop) ; binaires tiers arrêtables à distance — **lobby de boot** (sortie de secours indépendante du code invité et du réseau) et **page de configuration web** fournis par le stub `SceGuest` ([`docs/guests/README.md`](docs/guests/README.fr.md)) |
| **Outil PC** | Éditeur de **chorégraphies** : timeline, simulateur d'yeux (géométrie du firmware, presets réels), cube d'orientation, lecture aux vraies durées, export CSV ([`tools/choregraphies/`](tools/choregraphies/)) |
| **FreeRTOS** | brain 100 Hz (cœur 1 prio 4) · renderer 30 Hz (cœur 1 prio 3) · servo 50 Hz (cœur 0 prio 3) ; TripleBuffer lock-free + CommandQueue |

![L'éditeur de chorégraphies — timeline, simulateur d'yeux, cube d'orientation, CSV](docs/assets/screenshots/Companion%20-%20Choregraphies.png)

## Bins invités

Un bin invité n'est **pas** un mode du firmware ci-dessus : c'est un programme
séparé qui prend le robot. On le dépose sur la carte SD, on le lance depuis le
lanceur (swipe haut→bas) ou par l'API, et il tourne à la place du companion
jusqu'à ce qu'on le renvoie. Quatre sont fournis dans ce dépôt, et ce qui les
rend peu coûteux à écrire est le contrat [`SceGuest`](docs/guests/README.fr.md)
— il offre à n'importe quel binaire tiers un chemin de retour vers le companion,
un hall de démarrage qui fonctionne même quand l'invité plante, et une page de
réglages web qu'il n'a pas à écrire.

| Bin | Ce qu'il fait | Seconde carte |
|---|---|---|
| [**`flight-radar`**](docs/guests/FLIGHT-RADAR.fr.md) | Radar ADS-B temps réel : traque d'un vol (route, villes, ETA, progression), symboles par catégorie OACI, vecteurs de vitesse, 4 thèmes jour/nuit appairés, unités aéro ⇄ métrique, zoom à la pincée, luminosité et thème automatiques, decks METAR/TAF/NOTAM, tête qui pointe l'avion suivi | M5Stack Fire, autonome |
| [**`space`**](docs/guests/SPACE.fr.md) | Instrument spatial de bureau : l'ISS en direct (TLE Celestrak + SGP4 calculé à bord), passages visibles déterminés sur place, la Lune (phase, schéma orbital, éclipses ombrales), les planètes à l'œil nu, les prochains lancements | M5Stack Fire, autonome |
| [**`ha-remote`**](docs/guests/HA-REMOTE.fr.md) | Télécommande Home Assistant : écran d'accueil par catégories (volets, lumières, prises, caméras), une entité à la fois au swipe ←/→, **position d'un volet et couleur / température / puissance d'une lampe**, retour d'état quasi temps réel, vignettes caméra | — |
| [**`led-fluid`**](docs/guests/LED-FLUID.fr.md) | Du liquide dans une boîte : un fluide à particules dont la gravité EST l'inclinaison du robot, peint en grille de pastilles (la densité donne la couleur, la vitesse donne la lumière), secouer éclabousse, taper repousse. Panneau de physique au swipe droite — viscosité, gravité, rebond, traînée, cinq presets —, rectangle teinte/saturation au swipe gauche, et les douze WS2812 peuvent répéter le fluide | — |

Les deux marqués **autonome** se construisent pour un M5Stack Fire depuis la
*même source*, les différences matérielles étant déclarées en drapeaux de
capacité plutôt que forkées — sans companion, sans K151, trois boutons au lieu
d'une dalle tactile.

## Démarrage rapide

**Installer une release sans rien compiler** : téléchargez les fichiers de la
[dernière release](https://github.com/j-kwan/stackchan-companion/releases/latest)
et suivez [`docs/INSTALL.fr.md`](docs/INSTALL.fr.md) (flash depuis le
navigateur, carte SD, WiFi, mises à jour).

**Depuis les sources** :

```powershell
# Tests natifs (sans hardware — MinGW)
.\scripts\gates\test-native.ps1

# Build + flash du firmware principal (CoreS3 sur COM6)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e companion -t upload --upload-port COM6

# Un bin invité — déposé sur la SD, lancé depuis le launcher. Les mêmes deux
# lignes pour flight-radar, ha-remote et led-fluid : les quatre partagent un contrat.
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e space
curl -X POST "http://<ip>/api/bins" -F "file=@.pio/build/space/firmware.bin;filename=space.bin"
```

Au boot : WiFi **STA** si des credentials sont configurés (carte SD ou
`POST /api/wifi`), repli hotspot **`StackChan-AP`** / `goodlife` sinon.
Console : `http://<ip>/` (ou `http://stackchan.local/` en STA) — le portail
captif y redirige automatiquement en mode AP.

### Carte SD (optionnelle)

Modèle prêt à copier dans [`sdcard/`](sdcard/README.fr.md) :

```
/companion.bin               restauration SD-Updater ([SauverFW] du launcher)
/bins/*.bin                  binaires lançables (launcher + API)
/dances/*.csv                chorégraphies personnalisées
/stackchan-companion/config.yaml  wifi + tuning persistés (schéma docs/reference/CONFIG.md)
```

## Architecture

```
src/
├── engine/     rendu + primitives PURES : EyeRig (30 émotions), Renderer (tâche),
│               FaceState/TripleBuffer, EyeGeometry (invariants anti-débordement),
│               CrtEffect, EyeEffects (overlays), Blender, Tuning, Units, Clock/Rng
├── behavior/   comportement PUR (Clock/Rng injectés, testé en natif) :
│               Brain (tâche 100 Hz, SEUL écrivain FaceState, CommandQueue),
│               EmotionRoulette, IdleBehavior, BlinkController, VestibularSystem
│               (VOR), PickupDetector, Sequencer/Dances, ServoMotion (tâche 50 Hz)
├── hal/        Board (init K151 ordonnée), Py32Expander (VM_EN+LEDs),
│               ImuReader (mapping axes), Si12T (touch tête), ArduinoClock
├── interact/   TouchGestures (swipes/taps écran)
├── app/        WebApi (REST+mDNS+portail), WebConsole (pages embarquées),
│               EmotionLeds, SoundFx, Launcher, SdConfig (YAML)
└── guest/      SceGuest.h — stub à embarquer dans vos .bin (stop distant,
                détail : docs/guests/README.md)

firmware/common/  les 13 en-têtes que le companion ET les bins invités
                partagent, pour qu'une règle vive à UN seul endroit : Yaml (le
                seul décodeur de ligne), I18n (T(en,fr)), Trace (debug à
                chaud), FirmwareInfo (quel build tourne), Gesture (seuils de
                swipe), ButtonFsm, SunClock, Ltr553, PsJson, CellText, CfgBool,
                SdPins, SdWatch. Cinq sont vendorés dans SceGuest.h pour que le
                stub reste copiable seul — un portail prouve que les copies ne
                divergent pas

scripts/        gates/ (ce que check-all lance, et rien d'autre), dev/ (à la
                main contre une carte), build/ (lancé par PlatformIO)
                — détail : scripts/README.md
tools/          côté PC, jamais sur le robot : choregraphies/ (éditeur de
                danses), generators/, probes/ — détail : tools/README.md
```

Deux règles structurantes, parmi les 26 de [`docs/ROADMAP.md`](docs/ROADMAP.fr.md) §A2.
Le **Brain est le seul écrivain** de l'état facial : tout le reste passe par la
CommandQueue. Et le **Brain est la seule source de lissage** : le Renderer
dessine ce qu'il reçoit, sans le retoucher — une rampe ajoutée côté rendu
écraserait les clignements et le VOR.

Pour une vue d'ensemble des mécanismes clés avec des diagrammes (boucle
FreeRTOS, pipeline VOR, séquencement des danses, portail réseau…), voir
[`docs/architecture/WORKFLOWS.md`](docs/architecture/WORKFLOWS.fr.md).

## Environnements PlatformIO

| Environnement | Usage |
|---|---|
| **`companion`** | **firmware principal** (`firmware/companion/`) |
| `flight-radar` | bin invité : radar ADS-B temps réel (`firmware/flight-radar/`) |
| `flight-radar-fire` | la même source radar, autonome sur M5Stack Fire (boutons, sans K151 ni companion) |
| `ha-remote` | bin invité : télécommande Home Assistant (`firmware/ha-remote/`) |
| `space` | bin invité : instrument spatial de bureau (`firmware/space/`) — ISS, passages, Lune, planètes, lancements |
| `space-fire` | la même source space, autonome sur M5Stack Fire (boutons, sans K151 ni companion) |
| `led-fluid` | bin invité : un fluide à particules incliné par l'IMU (`firmware/led-fluid/`) |
| `led-fluid-fire` | la même source fluide, autonome sur M5Stack Fire (boutons, sans K151 ni companion) |
| `native` | **427 tests unitaires en 37 suites** sur PC (`scripts/gates/test-native.ps1`) — `check-all` affirme les deux comptes, car `pio test` sort 0 sur ce qu'il a lancé et ne dit rien de ce qu'il a sauté |

Avec deux cartes branchées, résoudre le port d'upload par identifiant USB
plutôt que par numéro de COM — `.\scripts\dev\find-port.ps1 -Board fire` — sinon
un `-t upload` nu peut écraser le companion du StackChan.

## Documentation

Carte complète : [`docs/README.md`](docs/README.fr.md). Le document de
pilotage est [`docs/ROADMAP.md`](docs/ROADMAP.fr.md).

| Rubrique | Contenu |
|---|---|
| [`docs/architecture/`](docs/architecture/) | comment le firmware fonctionne — [mécanismes en diagrammes](docs/architecture/WORKFLOWS.fr.md), [conventions](docs/architecture/CONVENTIONS.fr.md) |
| [`docs/hardware/`](docs/hardware/) | la machine qu'il pilote : [inventaire et ordre de boot](docs/hardware/README.fr.md), [les bus partagés](docs/hardware/BUSES.fr.md), [pièce par pièce](docs/hardware/PERIPHERALS.fr.md), [budgets et impasses](docs/hardware/LIMITS.fr.md) |
| [`docs/reference/`](docs/reference/) | [l'API REST](docs/reference/API.fr.md), [les 30 expressions](docs/reference/EMOTIONS.fr.md), [sécurité](docs/reference/SECURITY.fr.md), [les yeux](docs/reference/EYES.fr.md), [config.yaml](docs/reference/CONFIG.fr.md), [bande de statut](docs/reference/STATUSBAR.fr.md), [danses](docs/reference/CHOREGRAPHIES.fr.md), [plugins](docs/reference/PLUGINS.fr.md) |
| [`docs/guests/`](docs/guests/) | les `.bin` invités : [contrat SceGuest](docs/guests/README.fr.md), [flight-radar](docs/guests/FLIGHT-RADAR.fr.md), [ha-remote](docs/guests/HA-REMOTE.fr.md), [space](docs/guests/SPACE.fr.md), [led-fluid](docs/guests/LED-FLUID.fr.md) |
| [`docs/integrations/`](docs/integrations/) | [Home Assistant pilote le robot](docs/integrations/HOMEASSISTANT.fr.md) |
| [`docs/validation/`](docs/validation/) | [playbook matériel](docs/validation/PLAYBOOK-HW.fr.md), [tableau de bord](docs/validation/VALIDATION.fr.md) |
| [`CONTRIBUTING.fr.md`](CONTRIBUTING.fr.md) | compiler, les huit portails et ce que chacun vous dira, les règles qui piègent |
| [`CHANGELOG.fr.md`](CHANGELOG.fr.md) | état courant du firmware (version unique) |

## Matériel — StackChan SKU:K151

- **M5Stack CoreS3** : ESP32-S3, écran IPS 320×240 tactile (FT6336U), IMU 9-axis
  **BMI270 + BMM150** (magnétomètre), lumière ambiante **LTR-553**
- **Servos SCS0009** bus série (yaw 166±130°, pitch **19..99°** = spec officielle
  M5Stack 5-85°, home 93°) — alimentation via **PY32 IO Expander** (I2C 0x6F,
  VM_EN GPIO0) + LEDs WS2812C×12
- **Touch tête Si12T** (0x68) + **jauge INA226** (0x41), bus `Wire1` G12/G11
  (⚠ mêmes broches physiques que le bus interne — verrou `hal/I2cBus.h`)
- **Contraintes mesurées sur ce hardware** : bus LCD **40 MHz** (80 =
  artefacts panel), **SD 15 MHz** (25 = pertes de tokens en écriture → gel
  d'affichage), reconfiguration LCD AVANT `SD.begin()` (deadlock spi_bus_lock),
  série **DTR seul** (DTR+RTS = bootloader), jamais de `setBrightness()` par
  frame (I2C PMIC).

**[`docs/hardware/`](docs/hardware/) en est le compte rendu complet** : chaque
pièce avec son adresse et son pilote, l'ordre de démarrage et pourquoi c'en est
un, ce que coûte le partage de chaque bus, les étalonnages qui ne doivent pas
dériver — et un tableau de ce qui a été tenté sur cette carte et a **échoué**,
avec la mesure qui a clos le dossier.

## Remerciements

Ce projet s'appuie sur le travail et les idées de plusieurs communautés :

- **[esp32-eyes](https://github.com/playfultechnology/esp32-eyes)** (Alastair
  Aitchison, Playful Technology) et **[ESP32_Faces](https://github.com/luisllamasbinaburo/ESP32_Faces)**
  (Luis Llamas) — le style d'yeux rectangulaires arrondis et le moteur de
  transition/dessin d'origine, portés et étendus ici (`engine/EyeRig.h`,
  `EyeDrawer.h`, `Transitions.h`, `Animations.h`, presets).
- **[RoboEyes](https://github.com/FluxGarage/RoboEyes)** (FluxGarage) —
  inspiration conceptuelle pour le comportement du regard au repos.
- **[StackChan](https://github.com/meganetaaan/stack-chan)** (meganetaaan) et
  l'écosystème M5Stack-Avatar / stackchan-arduino — la plateforme matérielle
  K151 elle-même, ainsi que les danses/gestes de référence et le driver
  Si12T.
- **Anki Cozmo/Vector** — la référence artistique qui a guidé toute la
  direction d'animation de ce firmware (eyes-lead-head-follows, saccades
  sèches, blink en ligne fine).
- **Disney/Pixar : *Inside Out* (2015) et *Inside Out 2* (2024)** — pas pour les
  personnages, mais pour le système de combinaison des couleurs d'émotion que
  les chartes marketing des films posent elles-mêmes : quelques familles plus une
  grille directionnelle de ce à quoi ressemblent deux émotions mélangées. La
  palette des 30 émotions dans `src/engine/Emotions.h` s'appuie directement
  sur cette charte, créditée dans le code
  (`docs/assets/insideout - combination.png`).
- **Mobile Suit Gundam** (Sunrise/Yoshiyuki Tomino) — le thème « Gundam » de
  la console compagnon, et la propre palette « Gundam » / « Gundam nuit » du
  bin invité `flight-radar` (son défaut, même sur une carte nue), sont tous
  deux la vraie palette du RX-78-2 (bleu, or, rouge, blanc). Haro — l'un des
  deux personnages livrés compilés dans le firmware — emprunte son nom au
  propre robot compagnon rond de la franchise.

- **[autorouter.aero](https://www.autorouter.aero/)** — la source NOTAM du bin
  invité `flight-radar`, tirée de l'EAD d'EUROCONTROL. Gratuite, et le seul
  service à avoir répondu pour cet aérodrome après plusieurs autres restés
  muets ou exigeant une clé commerciale. Son API OAuth 2.0 est documentée,
  franche sur ses limites, et son support répond.
- **[aviationweather.gov](https://aviationweather.gov/)** (NOAA) — METAR, TAF
  et catégorie de vol, gratuits et sans clé.
- **[airplanes.live](https://airplanes.live/)**, **[adsb.lol](https://adsb.lol/)**,
  **[adsb.fi](https://adsb.fi/)** — flux ADS-B communautaires, et
  **[hexdb.io](https://hexdb.io/)** / **[adsbdb.com](https://www.adsbdb.com/)**
  pour les routes et les aérodromes.
- **[OurAirports](https://ourairports.com/)** — la base de pistes du domaine
  public derrière la rose du METAR.

Merci à ces projets et à leurs auteurs. Une mention particulière pour les
services de données aéronautiques ci-dessus : ils publient de vraies données
opérationnelles, gratuitement, pour quiconque — y compris un projet de loisir
qui les affiche sur un bureau. Ce projet n'existerait pas sans eux.

## Licences

Ce projet est distribué sous **AGPL-3.0** (voir [`LICENSE`](LICENSE)).

Certains fichiers portés depuis des sources tierces conservent leur mention
de licence d'origine en en-tête ; comme ils sont compilés dans le même
binaire, la distribution du firmware complet est régie par les termes de
l'AGPL-3.0 :

| Source | Licence | Fichiers concernés |
|---|---|---|
| [esp32-eyes](https://github.com/playfultechnology/esp32-eyes) (Aitchison) / [ESP32_Faces](https://github.com/luisllamasbinaburo/ESP32_Faces) (Llamas) | AGPL-3.0 | `EyeRig`, `EyeConfig`, `EyeDrawer`, `Transitions`, `Animations`, `EyePresetsM5`, `EmotionRoulette` |
| StackChan firmware (M5Stack / meganetaaan) | Apache-2.0 | keyframes danses, driver Si12T (référence) |
