> [English](WORKFLOWS.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# WORKFLOWS.md — mécanismes clés illustrés

Complète `docs/ROADMAP.md` (référence normative) et `docs/architecture/CONVENTIONS.md`
(unités/axes) avec des diagrammes pour visualiser les mécanismes qui
traversent plusieurs fichiers. Source de vérité toujours : le code.

## 1. Architecture générale — tâches FreeRTOS et primitives

Quatre tâches concurrentes (+ loop()), aucune ne partage de mémoire mutable
sans passer par une primitive dédiée. Le Renderer ne lit jamais l'état
interne du Brain, le Brain n'écrit jamais l'écran, ServoMotion ne connaît
que des cibles. La tâche Caméra (encodage JPEG logiciel ~50-300 ms/frame,
buffers en PSRAM) est en prio 1 : servos et réseau la PRÉEMPTENT — le flux
ne ralentit ni les danses ni l'API.

```mermaid
flowchart LR
    subgraph Coeur1["Cœur 1"]
        Brain["Brain\n100 Hz · prio 4\nSEUL écrivain FaceState"]
        Renderer["Renderer\n30 Hz · prio 3\nSEUL propriétaire écran"]
    end
    subgraph Coeur0["Cœur 0"]
        Servo["ServoMotion\n50 Hz · prio 3"]
        Cam["Caméra\nprio 1 · capture+JPEG\n(pause renderer pendant fb_get)"]
    end

    API["WebApi\n(callbacks AsyncTCP)"] -->|post Command| CQ[("CommandQueue\nxQueue FreeRTOS")]
    Touch["TouchGestures / Si12T\n(loop, polling)"] -->|post Command| CQ
    CQ --> Brain

    Brain -->|write| TB[("TripleBuffer&lt;FaceState&gt;\nlock-free")]
    TB -->|read| Renderer

    Brain -->|cible yaw/pitch, atomics| Servo
    Servo -->|vitesse angulaire commandée\ncopie d'efférence| Brain

    Tuning[("Tuning\nfloats simples alignés\n73 clés enregistrées")]
    API -.->|POST /api/tuning\nécrit un champ| Tuning
    Brain -.->|lit chaque tick| Tuning
    Renderer -.->|lit chaque tick| Tuning
    Servo -.->|lit chaque tick| Tuning
```

**Règles absolues** (détail ROADMAP §A2) :
- Le Brain est le **seul écrivain** de `FaceState` — ailleurs, lire via
  `brain->currentEmotion()`.
- Le Brain est la **seule source de lissage** des canaux continus (gaze,
  openL/R, squash, VOR) — Renderer et EyeRig appliquent tel quel, jamais de
  rampe/filtre côté rendu (règle A2.15 : une rampe côté rendu rendrait le
  VOR et les blinks invisibles).
- Toute mutation externe passe par la `CommandQueue` : les callbacks
  AsyncTCP ne touchent que `post()` ou un champ `Tuning` — jamais l'état
  partagé directement.

`Tuning` est le seul bloc partagé qui ne porte **aucune** primitive : 73 clés
enregistrées, des `float` nus, ni mutex ni `std::atomic`. Il repose sur une
hypothèse de plateforme explicite — sur Xtensa, l'écriture d'un float 32 bits
aligné est atomique — si bien qu'un lecteur obtient l'ancienne valeur ou la
nouvelle, jamais une valeur déchirée. Cela suffit parce que chaque paramètre a
un sens indépendamment des autres : rien ici n'exige que deux champs changent
ensemble. Ce qui l'exigerait relèverait de la `CommandQueue`.

## 2. Un tick du Brain (100 Hz)

```mermaid
flowchart TD
    Start(["tick 100 Hz"]) --> Susp{"suspendu ?\n(le launcher possède l'écran)"}
    Susp -- oui --> Drop["drainer la file et la JETER\nretour"]
    Drop --> Start
    Susp -- non --> Drain["1. Drainer CommandQueue\n(émotions API, blinks/winks,\ntuning, danses)"]
    Drain --> Exp["1bis. Override temporisé expiré ?\nrestaurer l'émotion précédente"]
    Exp --> Roulette["2. EmotionRoulette\n(verrouillée par un override OU une danse\nactive — tick quand même consommé)"]
    Roulette --> Seq["2bis. Sequencer.update()\nkeyframe prête → émotion, lidEvent,\ncible servo (gazeYBias est lu en 5b)"]
    Seq --> Idle["3. IdleBehavior\nfixation → saccade → fixation"]
    Idle --> Imu["3bis. Verrou I2C + M5.Imu.update()\ncopie d'efférence, VestibularSystem.update() (§3)\nPUIS les réflexes : secousse → Scared,\nsoulèvement → Curious, double-tap → Happy,\nface posée → Sleepy (§4, §10)"]
    Imu --> Follow["3ter. Head-follow, puis\nretour au home généralisé"]
    Follow --> Blink["4. BlinkController\npolitiques par expression + Sleepy"]
    Blink --> Squash["5. Squash & stretch procédural\n(proportionnel à l'amplitude saccade)"]
    Squash --> Gaze["5bis. Base de regard via le blender\nidle OU danse (eyes lead, head follows)"]
    Gaze --> Breath["6. Respiration, asymMirror, publication\nTripleBuffer (jamais bloquant)"]
    Breath --> Tel["7. Ligne de télémétrie à 10 Hz\n(tuning.telemetry)"]
    Tel --> Start
```

**Un réflexe ne court-circuite pas le tick.** On est tenté de lire les réflexes
comme une porte en tête de tick — ils n'en sont pas une : ils sont évalués à
l'étape 3bis, *après* que la roulette et IdleBehavior ont déjà tourné, parce
qu'ils ont besoin de la lecture IMU qui s'y fait. Ce qui les rend préemptifs,
c'est ce qu'ils FONT — poster un override `SetEmotion` temporisé, appeler
`abortDance()`, préempter le blink — pas l'endroit où ils sont assis. Les
étapes qui ont tourné avant eux dans le même tick sont simplement écrasées
avant toute publication, et l'étape 6 est le seul écrivain que le Renderer
voie jamais.

## 3. Pipeline VOR (réflexe vestibulo-oculaire)

Le VOR compense en continu la rotation de la tête pour garder le regard
stable dans le monde — comme l'œil humain.

```mermaid
flowchart TD
    Gyro["Gyro brut °/s\n(BMI270 via ImuReader,\nmapping axes écran)"] --> Bias["Soustraction du biais\n(appris tête immobile :\nEMA 0,05 les 2 premières s, puis 0,002)"]
    Bias --> Efference["Soustraction de la copie\nd'efférence servo (cmdVel)\n→ ignore le mouvement AUTO-généré"]
    Efference --> Deadband{"|vel| < 0,8 °/s\npar axe (bruit) ?"}
    Deadband -- oui --> Zero["vel = 0\n(yeux parfaitement immobiles)"]
    Deadband -- non --> Slow["PHASE LENTE\noffset -= vel × dt × vor_gain\n(AUCUN lissage supplémentaire)"]
    Zero --> Stab
    Slow --> Stab["MÊME passe, inconditionnellement :\ndérive accel gated\n(tête calme + |a| ≈ 1g)\npuis le terme magnétomètre\n(vor_mag_alpha, 0 par défaut)"]
    Stab --> Check{"offset saturé\n(> saccade_recentre × max)\nOU résidu après 300 ms d'immobilité ?"}
    Check -- oui --> Saccade["PHASE RAPIDE\nsaccade de rattrapage\nsaccade_ms (80 ms par défaut, plancher 40)\nease-out cubique"]
    Check -- non --> Out
    Saccade --> Out(["offset appliqué\nau canal gaze du FaceState"])

    Accel["Accéléromètre\n(gravité + inclinaison)"] -.-> Stab
    Shake["Magnitude gyro 3 axes\n(indépendante du mapping)"] --> ShakeCheck{"soutenue > shake_gyro_thr\n> 500 ms\nET aucun mouvement propre ?"}
    ShakeCheck -- oui --> ScaredEvt(["shakeDetected() → Brain → Scared"])
```

La dérive n'est **pas** l'alternative à une saccade. Phase lente, dérive accel
gated tête calme et terme magnétomètre tournent tous dans la même passe de
stabilisation, l'un après l'autre ; ce n'est qu'ensuite que le résultat est
testé pour la saturation. Lire la dérive comme une branche `else` laisserait
croire que les yeux cessent d'intégrer dès qu'ils ont rattrapé, ce qui est
l'inverse de ce qui se passe.

Deux choses envoient les yeux en phase rapide : l'offset qui **sature**, et un
**résidu** qui survit — la tête est immobile depuis 300 ms et le regard est
toujours à côté de sa cible. Le second existe parce qu'une dérive lente peut
garer les yeux hors du centre sans jamais atteindre la borne de saturation, et
un robot dont les yeux restent tranquillement de travers a l'air cassé plutôt
que vivant.

La détection de secousse est **inhibée par tout mouvement auto-généré** — une
danse en cours *ou* un servo qui bouge, pas les danses seules (l'efférence
2 axes ne corrige pas la magnitude 3 axes) ; l'intégration VOR, elle, reste
**active** pendant les danses (efférence validée).

Le terme magnétomètre est câblé mais neutre : `vor_mag_alpha` vaut 0 par
défaut, et le code le borne à 0,2 même s'il est réglé. Sur le K151 les aimants
des servos biaisent bien trop le champ pour que la lecture soit exploitable —
voir `CONVENTIONS.md §3`.

## 4. Réflexes préemptifs

```mermaid
stateDiagram-v2
    [*] --> Normal
    Normal --> Scared: secousse soutenue > shake_gyro_thr, > 500ms
    Normal --> Curious: soulèvement détecté (|a|-1g > 0,08g tenu 120ms,\net le gyro sous 60 °/s)
    Scared --> Normal: fin du réflexe (~2s)
    Curious --> Normal: reposé + stable 1s (couple ré-engagé)
    Scared --> Scared: nouvelle secousse (limitée à une par 500ms)
    note right of Scared
      Coupe TOUT LE RESTE : danse en cours
      (abort), roulette, wink/blink en attente.
    end note
    note right of Curious
      Servos en roue libre
      ("pieds ballants") pendant
      que le robot est porté.
      Override rafraîchi chaque tick
      tant qu'il reste soulevé.
    end note
```

Les deux réflexes passent avant tout le reste, et aucun ne passe avant l'autre :
ce sont deux overrides temporisés appliqués l'un après l'autre dans le même
tick, si bien qu'un soulèvement détecté sur le même tick qu'une secousse arrive
simplement second et gagne. Il n'y a pas d'arbitrage entre eux parce que les
deux gestes ne se produisent pas vraiment ensemble — un robot qu'on soulève
n'est pas en même temps secoué. Les réflexes venus ensuite (double-tap, face
posée), eux, vérifient explicitement les deux avant de tirer : c'est ce qui les
rend polis plutôt que préemptifs.

La porte du soulèvement exige aussi que le gyro reste **sous 60 °/s** :
soulever est une translation, secouer est une rotation, et ne lire que l'écart
accélérométrique ferait passer toute secousse vigoureuse pour une prise en
main. Cette borne discrimine la secousse, elle ne teste pas l'immobilité — une
main qui soulève un robot n'est jamais parfaitement stable, et l'exiger
rejetterait précisément le geste que la porte existe pour attraper.

## 5. Séquencement d'une danse

```mermaid
sequenceDiagram
    participant U as Utilisateur (swipe/API)
    participant B as Brain
    participant Seq as Sequencer
    participant DS as DanceStore
    participant Sv as ServoMotion
    participant R as Renderer

    U->>B: post(CmdType::PlayDance, id)\nOU CmdType::PlayCustom, ptr
    B->>DS: PlayDance → la table INTÉGRÉE (dances::table())\nPlayCustom → DanceStore::find(name), résolu par WebApi\n(pointeur STATIQUE, double-banque, jamais réalloué en vol)
    B->>Seq: play(keys, count)
    loop chaque tick 100 Hz
        Seq->>Seq: update() — avance la timeline
        alt keyframe prête (hold écoulé)
            Seq-->>B: keyframe à appliquer
            B->>Sv: cible yaw/pitch (offset depuis Units.h)
            B->>B: applyEmotion (si définie sur la keyframe)
            B->>B: lidEvent (blink/wink si demandé)
            B->>B: gazeYBias (NOD/SHY : les yeux plongent,\nle servo reste à l'horizon)
        end
    end
    B->>R: FaceState (eyes lead: gaze dérivé\nde la pose servo COMMANDÉE)
    Note over B,Sv: holdMs ≥ servoMs GARANTI à la LECTURE\n(effectiveHold(), compté) — une keyframe ne peut\njamais expirer avant la fin de son trajet servo
    U->>B: secousse détectée (à tout moment)
    B->>Seq: abort() — coupe la timeline IMMÉDIATEMENT
```

Deux commandes, un seul Sequencer. `PlayDance` porte un **indice** dans la
table intégrée compilée dans le firmware ; `PlayCustom` porte un **pointeur**
que WebApi a déjà résolu via `DanceStore::find(name)`. La séparation existe
parce que les deux sources n'ont pas la même durée de vie — une table intégrée
est immortelle, une chorégraphie SD peut être rechargée sous les pieds du
robot — et la double-banque est ce qui les réconcilie : un rechargement
remplit la banque au repos et bascule un indice, si bien que le pointeur que
tient une danse en cours reste valide jusqu'à sa dernière keyframe.

La garantie `holdMs ≥ servoMs` est appliquée quand la keyframe est **jouée**,
pas quand elle est chargée. Cela garde les tables de keyframes `const` (elles
peuvent vivre en flash plutôt qu'en RAM) et signifie qu'un CSV écrit à la main
ne peut pas faire passer en fraude une keyframe qui expire en plein trajet — le
clamp l'attrape dans les deux cas.

Il se compte aussi lui-même (`Sequencer::clampCount()`), et ce compteur n'est
lu par rien d'autre que `test_sequencer` : le faire remonter en télémétrie
était prévu et ne l'a jamais été. Il reste parce que le test épingle le clamp
au travers — un compteur avec un lecteur honnête vaut mieux qu'un compteur
dont la seule justification était un projet.

Les chorégraphies personnalisées (`/dances/*.csv` sur la carte SD) sont parsées
par `DanceStore::reload()` au boot, à la demande (`POST /api/dances/reload`) et
après un remontage de la SD — détail complet du format :
[`docs/reference/CHOREGRAPHIES.md`](../reference/CHOREGRAPHIES.fr.md).

## 6. Réseau : STA avec repli AP + portail captif

Même forme des deux côtés — tenter la station, se replier sur un point d'accès
qui sert un portail captif — mais les deux ne résolvent pas leurs identifiants
de la même façon, et cette différence est tout l'enjeu : le companion est
provisionné par la **carte**, un invité peut l'être **à la main sur
l'appareil**.

```mermaid
flowchart TD
    subgraph C["companion — WebApi::begin()"]
      CB(["boot"]) --> CQ{"config.yaml porte un\nclient_ssid non vide ?"}
      CQ -- non --> CAP["softAP(ap_ssid)\npar défaut StackChan-AP"]
      CQ -- oui --> CSTA["setHostname(hostname)\nWiFi.begin(ssid, pass)\n10 s, UNE tentative"]
      CSTA --> COK{"WL_CONNECTED ?"}
      COK -- non --> CAP
      COK -- oui --> CGOT["STA"]
      CAP --> CDNS["DNSServer :53 sur '*'\nportail captif"]
      CGOT --> CEnd
      CDNS --> CEnd["setSleep(false), TxPower 19,5 dBm\nmDNS hostname.local — les DEUX modes\nmiddleware Basic-Auth, routes,\ncheckRouteOrder(), server.begin()"]
    end

    subgraph G["invité — SceGuest::begin()"]
      GB(["boot"]) --> GSD["lit /stackchan-companion/config.yaml\n(même carte, même fichier)"]
      GSD --> GNVS{"la NVS 'sce-net'\ntient un ssid ?"}
      GNVS -- oui --> GOV["il SUPPLANTE la carte\n(quelqu'un l'a saisi ICI)"]
      GNVS -- non --> GCARD["la carte, sinon les\narguments de begin()"]
      GOV --> GSTA["WiFi.begin(...) 10 s"]
      GCARD --> GSTA
      GSTA --> GOK{"connecté ?"}
      GOK -- oui --> GTok["jeton CSRF tiré\n(radio levée = vraie entropie)"]
      GOK -- non --> GAP["AP : ap_ssid depuis la carte,\nsinon SCE-Guest / goodlife"]
      GAP --> GDNS["DNSServer :53 sur '*'\n+ onNotFound -> 302 /config"]
      GDNS --> GTok
    end
```

**Un portail captif a deux moitiés, et la redirection est celle qui l'ouvre.**
Le DNS joker garantit seulement que le nom demandé par le téléphone résout vers
la carte ; ce qui décide de l'apparition du portail, c'est la réponse à la
sonde qui suit — `/generate_204` sur Android, `/hotspot-detect.html` chez
Apple, `/connecttest.txt` sous Windows, `/canonical.html` pour Firefox. Aucune
n'est une route déclarée : elles tombaient donc sur le 404 intégré du
WebServer. Cela suffit à Android et Windows pour finir par proposer une
connexion, mais Apple *affiche* la page renvoyée dans sa feuille de portail —
l'usager lisait donc « Not found » là où le formulaire devait être.
`onNotFound` répond désormais une **302 vers `/config`**, qui n'est le corps
attendu par personne et se lit donc comme un portail chez les quatre.

Elle n'est posée qu'**en mode AP**, et cette limite porte : sur un réseau
rejoint, le même gestionnaire transformerait chaque faute de frappe et chaque
signet périmé en redirection silencieuse vers la page de réglages. Une route
absente doit rester absente. La redirection nomme l'**IP** de l'AP et non un
nom d'hôte — toute la situation étant qu'aucun nom ne résout — et c'est
l'adresse qu'imprime déjà l'écran sans réseau : ignorer la fenêtre et la taper
à la main mène au même endroit.

Quatre sources classées côté invité, la première qui répond gagne : la **NVS**
(un acte délibéré sur CETTE unité passe avant une carte qui nomme peut-être
encore l'ancien réseau), puis la carte, puis le SSID compilé dans `begin()`,
puis l'AP. « Oublier » sur `/config` efface l'entrée NVS et rend l'autorité à
la carte. C'est la seule voie par laquelle un bin autonome — un Fire sans
companion — apprend un jour un réseau.

Aucun des deux côtés ne réessaie : une tentative, dix secondes, puis l'AP. La
pompe qui répond au portail diffère, et chacune garde ce qu'elle possède
réellement : le companion fait tourner le `DNSServer` depuis `webApi->update()`
**quand il est en mode AP**, l'invité le fait tourner **quand le serveur DNS a
effectivement démarré**. Le mDNS est un service propre au companion, et il est
enregistré dans les **deux** modes — le nom répond aussi sur l'AP.

La table de routes est enregistrée sous la règle A2.19 (chemin spécifique avant
son préfixe) ; `checkRouteOrder()` relit au boot la table consignée et dénonce
les violations en série. Il ne fait que signaler — rien ne se réordonne tout
seul, donc une ligne série est tout l'avertissement.

## 7. Séquence de boot matériel (`hal/Board.h`)

L'ordre est contraint par le matériel K151 — l'inverser casse le boot.
`Board` lève les **rails et les bus**, et s'arrête là : il lève VM_EN pour que
les servos *aient du courant*, mais il n'appelle jamais `servo.begin()` —
ServoMotion s'en charge plus tard, depuis sa propre tâche, et cela ne marche
que parce que le rail est déjà levé. Garder les deux séparés est ce qui permet
à une carte sans servo de démarrer exactement de la même façon.

```mermaid
sequenceDiagram
    participant M as M5.begin()
    participant D as Display
    participant S as SPI + SD
    participant W as Wire1 (I2C corps)
    participant P as PY32 (VM_EN)
    participant X as Capteurs du corps

    M->>M: PMIC AXP2101 : alimente écran + SD
    M->>D: setBrightness + fillScreen(BLACK) immédiat\n(sinon le boot screen M5 reste visible)
    D->>S: bus LCD en ÉCRITURE réglé à 40 MHz\nAVANT SD.begin()\n(sinon deadlock spi_bus_lock)
    S->>S: SPI.begin(36, 35, 37, 4)\nSD.begin(CS 4, 15 MHz) — broches depuis SdPins.h
    S->>W: Wire1.begin(12, 11, 100 kHz)
    W->>P: detect() sonde jusqu'à 1200 ms,\npuis VM_EN lève le RAIL servo (+300 ms)
    P->>X: sonde Si12T 0x68, INA226 0x41, LTR-553 0x23\nprésence loggée, l'absence n'est pas fatale
    X->>M: M5.Power.setExtOutput(true), état IMU loggé
```

## 8. Écriture/lecture SD différée : `renderer.pause()`/`resume()`

Le bus SPI2 est **partagé** entre le LCD et la SD. Une écriture SD qui
coïncide avec un push du renderer fait échouer le protocole carte
(retries « no token received ») ET gèle l'affichage le temps des retries.

```mermaid
sequenceDiagram
    participant API as WebApi (AsyncTCP)
    participant L as loop()
    participant Rd as Renderer
    participant SD as Carte SD

    API->>L: flag consumeSaveRequest() = true\n(callback ne touche QUE le flag)
    Note over L: loop() tourne hors AsyncTCP —\nseul endroit qui touche la SD différée
    L->>L: arme l'écriture pour maintenant + 2 s\n(une rafale d'écritures tuning = UNE sauvegarde)
    L->>Rd: renderer.pause(willPaint = false)\n(attend la fin du frame, plafond 500 ms)
    L->>SD: sdConfig.save(tuning)
    L->>Rd: renderer.resume()
    Note over Rd: même garde pour reload config, reload danses,\nreload règles, le cache des bins et le Launcher
```

Le débounce est toute la raison d'être de l'armement à deux secondes : la
console écrit une clé par widget, et un utilisateur qui traîne un curseur en
poste une douzaine en une seconde. Sauvegarder à chaque fois, ce serait une
douzaine de frames en pause et une douzaine d'écritures carte pour une seule
intention. L'écriture en attente est forcée avant un redémarrage ou une
extinction, si bien qu'attendre ne perd rien.

`pause(willPaint)` dit si le pauseur va **dessiner** pendant qu'il tient
l'écran. Les sites de SD différée passent `false` — ils veulent seulement que
le bus SPI2 se taise — tandis que le Launcher passe `true` parce qu'il prend
l'écran entièrement à son compte, et que le renderer doit invalider ses caches
avant de repeindre. L'attente elle-même est plafonnée à 500 ms : si le renderer
est coincé, l'appelant poursuit plutôt que de bloquer la loop().

`pause()`/`resume()` est **refcounté sous spinlock** : deux
pauseurs concurrents coexistent (loop() pour la SD, tâche caméra autour de
`fb_get`, cœurs différents). Premier pauseur arme, dernier resume désarme ;
compteur clampé à 0. La boucle renderer **re-vérifie la demande après son
ack** et avant de dessiner, si bien qu'un ack émis juste avant l'arrivée d'un
second pauseur ne peut pas se lire comme une autorisation de dessiner.
Toujours apparier pause et resume ; jamais de resume orphelin.

## 9. Suivi du son (tête vers le bruit, `sound_track=1`)

```mermaid
sequenceDiagram
    participant Mic as M5.Mic (ES7210 stéréo)
    participant ST as SoundTracker (loop())
    participant SD as SoundDirection (pur)
    participant B as Brain
    participant Sv as ServoMotion

    loop chaque loop() (~10 ms)
        ST->>Mic: ring buffer 3×512 frames\n(non bloquant)
        ST->>SD: feed(buffer, seuil)
        SD-->>ST: event + imbalance lissé\n(canal 1 = micro DROIT)
    end
    ST->>B: post(CmdType::SoundDir, imbalance)
    alt secousse/danse/pickup en cours
        B->>B: ignoré (règle réflexe)
    else servo en mouvement (ou < 350 ms après)
        B->>B: ignoré (SOURDINE : bruit servos/engrenages\ncapté par les micros — pas d'auto-poursuite)
    else RMS > soundtrack_shock_thr
        B->>B: sursaut → danse shocked
        B->>Sv: puis virage vers la source
    else sinon
        B->>Sv: pas de yaw ∝ déséquilibre\n(vitesse modulée par l'intensité)
    end
    Note over ST,Mic: bus I2S1 PARTAGÉ avec le haut-parleur —\narbitrage isRunning(), JAMAIS begin()\navant record() (règle A2.20)
```

## 10. Gestes IMU additionnels

Le BMI270 via M5Unified n'expose aucune interruption tap matérielle —
double-tap et orientation sont dérivés du flux accel/gyro déjà lu par le VOR
(§3), pas d'un capteur/bus séparé.

```mermaid
flowchart TD
    Accel["|accel| (ImuReader,\nmême flux que le VOR)"] --> Tap{"pic bref > 1,6g\nhors réfractaire 80 ms ?"}
    Tap -- "2ᵉ pic < 500 ms" --> DoubleTap["consumeDoubleTap() = true"]
    DoubleTap --> Brain1["Brain : Happy 3 s + wink\n(sauf secousse/soulèvement en cours)"]

    AccelZ["accel.z brut\n(normale à l'écran)"] --> Face{"soutenu > 1,5 s\nau-delà de ±0,75g ?"}
    Face -- "z < -0,75g" --> FaceDown["FaceOrient::FaceDown"]
    FaceDown --> Brain2["Brain : Sleepy tenu\n(override rafraîchi tant que posé,\nlibéré au redressement)"]
```

## 11. Bande de statut & plugins (`sources → champs → {widgets, règles}`)

Contrat unique : toute source pose des **champs** dans le blackboard
(`FieldStore`) ; le Renderer les DESSINE (widgets de la bande) et le
`RuleEngine` y RÉAGIT (règles `champ → Commande`). Détail : `docs/reference/STATUSBAR.md`
(affichage) + `docs/reference/PLUGINS.md` (contrat). Aucune source ne câble de chemin
dédié vers l'écran ou le Brain.

```mermaid
flowchart TD
    subgraph Sources
      API["POST /api/field"]
      Loop["loop() : batt, rssi, cam,\nmic, night, light, ip…"]
      Script["scripts PC / statusline\n(ctx, coût → g0/g1)"]
    end
    Sources --> FS["FieldStore\n(blackboard, thread-safe)"]

    FS --> Rend["Renderer (tâche unique écran)"]
    Rend --> Dyn["zone dynamique :\nalerte > say (marquee) > mode\n(VU / jauges / off)"]
    Rend --> Icons["rangée d'icônes :\nbatt/wifi/cam/mic/nuit (masquables)\n+ infos debug émotion·ip (centré)"]

    FS --> RE["RuleEngine\nchamp → Commande (front tenu)"]
    RE -->|post whitelisté| CQ["CommandQueue → Brain\n(réflexes prioritaires A2.5)"]

    SD["/stackchan-companion/rules.txt\n(+ import/export /api/sd/*)"] -.->|hot-reload| RE
```


## 12. Lancer un `.bin` invité, et revenir

La chaîne qui échange le firmware exécuté par la puce. Chaque flèche traverse
un redémarrage, et c'est pourquoi rien ici ne peut être un appel de fonction.

```mermaid
flowchart TD
    subgraph GO["sortir"]
      A["launcher (glissement bas)<br/>ou POST /api/bins/launch"]
      B["différé vers loop()<br/>(A2.6 : jamais dans un callback AsyncTCP)"]
      C["updateFromFS(SD, /bins/x.bin)<br/>écrit l'AUTRE slot OTA"]
      D["otadata pointe dessus<br/>→ redémarrage"]
    end
    subgraph IN["l'invité tourne"]
      E["HALL de démarrage, ~2,5 s<br/>la seule sortie qui survit à un plantage"]
      F["SceGuest : WiFi, /config,<br/>POST /api/bins/stop"]
    end
    subgraph BACK["revenir"]
      G["arrêt : API, glissement bas ≥100 px,<br/>ou hall"]
      H["CoopStop gare la tâche réseau"]
      I["updateFromFS(SD, /companion.bin)"]
      J["redémarrage → companion"]
    end
    A --> B --> C --> D --> E --> F
    F --> G --> H --> I --> J
    E -->|sans action| F
```

**Le piège est la dernière flèche.** Revenir reflashe le companion **depuis la
carte SD** : un `/companion.bin` périmé écrase donc en silence un firmware que
vous venez de flasher par USB — et tous les signaux extérieurs annoncent quand
même un succès. C'est pour cela que `GET /api/firmware` publie `slot`, `sha`
et `console`, et que rafraîchir la copie SD fait partie du flash au lieu d'en
être la suite. L'arithmétique des partitions derrière tout cela — deux slots
applicatifs, `otadata`, et pourquoi le slot qui tourne ne se déduit pas du
dernier flash — est dans [`hardware/LIMITS.fr.md`](../hardware/LIMITS.fr.md).

**Pourquoi le garage compte.** `updateFromFS` lit la carte et écrit la flash
depuis `loop()`. Une tâche invitée encore en train de télécharger en TLS lui
dispute le bus SD/SPI et le tas, et un reflash de ~9 s devient des minutes de
contention avec HTTP muet — vu du réseau, impossible à distinguer d'un
plantage. `sce::CoopStop` est l'arrêt coopératif qui l'évite ; le contrat, y
compris la fenêtre d'acquittement que chaque bin choisit, est dans
[`../guests/README.fr.md`](../guests/README.fr.md).

**Pourquoi le hall existe.** C'est le seul retour qui ne dépende ni du code de
l'invité ni du réseau. Les trois sorties se dégradent dans cet ordre : l'API
est muette si le WiFi est tombé, le geste est muet si le `loop()` de l'invité
est bloqué, et le hall n'est muet que si le démarrage lui-même plante — auquel
cas on sort la carte et la copie se fait à la main.
