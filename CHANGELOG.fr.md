> [English](CHANGELOG.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# Changelog — StackChan-Companion

État courant du firmware compagnon StackChan (M5Stack CoreS3, kit K151).
Document à version **unique** (pas de numéro ni de date) : il décrit ce que
le firmware fait aujourd'hui, par catégorie. Firmware principal :
`firmware/companion/` ; moteur/comportement dans
`src/engine|behavior|hal|interact|app|guest` ; pilotage : `docs/ROADMAP.md`.

---

### Les bins invités reçoivent les réglages radio du companion (09-25)

Mesuré sur le même robot, au même endroit, RSSI autour de −75 dBm : des pings de 71 à 295 ms sous un bin invité, 2 à 8 ms sous le companion. Le companion coupe la veille du modem et pousse sa puissance d'émission au maximum depuis le 07-20 ; `SceGuest`, qui rejoint le réseau pour les quatre invités, ne faisait ni l'un ni l'autre. Il monte désormais la puissance d'émission à 19,5 dBm **avant** la jonction, puisqu'un lien faible est justement le moment où l'association en a besoin, et coupe la veille du modem une fois connecté, ce qui compte surtout pour les bins qui relèvent en TLS toute la journée. Mesuré après la modification, sous flight-radar : 2 à 12 ms. Coût : environ 40 à 60 mA pendant qu'un invité tourne.

### La tête suit le satellite dans le ciel (09-25)

`space` gagne l'option pour laquelle il avait toujours été spécifié : avec `servo` activé, une fois par seconde le bin propage le satellite suivi et pointe la tête vers lui tant qu'il est au-dessus de l'horizon, puis revient au repos et relâche le couple quand il se couche, quand le jeu d'éléments a plus de 14 jours ou quand il n'y a pas d'horloge. Désactivé par défaut, comme toute automatisation d'invité qui fait bouger quelque chose. `servo_az` dit où regarde le visage du robot, puisqu'un azimut ne veut rien dire pour un servo sans lui. La pose est `firmware/common/headtrack.h`, pur et épinglé par deux nouveaux cas de `test_astro` (429 tests natifs) : lacet selon la convention centrée sur l'observateur, borné à ±130°, tangage d'un degré de servo par degré d'élévation jusqu'à la pose haute sûre. Ses quatre copies de constantes d'`Units.h` sont tenues par `check-mirrors`. Le code servo que le radar gardait pour lui (alimentation par le PY32, écritures SCS non bloquantes, relâchement du couple) passe dans `firmware/common/HeadServo.h`, désormais partagé par les deux bins ; `space-fire` se construit sans servos (`SCE_HAS_SERVO=0`) et ne lie pas la bibliothèque. Le sens a été vérifié sur le robot : satellite placé à 90° sur la gauche du robot, la tête a tourné vers la gauche du robot (PLAYBOOK 4j.2).

**flight-radar tournait la tête à l'opposé du vol.** Sa formule à lui, `166 + cap`, avait le signe inversé par rapport à la même convention, et personne ne l'avait regardée sur le robot. Il prend désormais son lacet dans le même `headtrack.h` que space, et sa copie privée de `YAW_REL_MAX` a disparu. Le radar suppose toujours que le robot regarde le nord : il n'a pas de `servo_az`.

### Lancer un invité depuis la console ou l'API ne faisait rien (09-25)

`POST /api/bins/launch` (et le bouton 🚀 de la console, qui l'appelle) répondait 202 « flash + redémarrage dans ~2 s » et le robot continuait simplement à faire tourner le companion. Seul le commentaire qui annonçait l'étape avait survécu dans `loop()` : le code qui consommait la demande et flashait l'invité avait disparu, et git n'en garde aucune trace, puisqu'il manquait déjà au commit racine recréé lors du reset de l'historique le 09-19. Le lanceur par glissement vers le bas n'était pas touché. Réécrit pas à pas sur `Launcher::launch()` : les trois mêmes refus avant tout geste irréversible (pas de `/companion.bin` pour revenir, pas de partition OTA, un fichier absent, vide ou plus gros que la partition), le même écran de flash, le couple des servos relâché d'abord et le rail coupé en dernier, et à tout refus ou flash raté tout est rendu comme à la fermeture du menu. Vérifié sur le robot : `space.bin` lancé par l'API tourne dix secondes plus tard.

### Chaque fichier de règles de la carte est vérifié avant d'y arriver (09-25)

Une ligne de `rules.txt` que le parseur refuse est simplement absente — pas d'erreur, pas de trace, seulement un robot qui ne réagit pas — et `GET /api/rules` ne peut le montrer que pour le fichier chargé, jamais pour le jeu d'une autre personnalité. `scripts/gates/check-rules.py`, le neuvième portail, lit chaque `rules*.txt` livré dans `sdcard/` et refuse : une ligne que le parseur jetterait (plus de 127 octets, moins de sept champs, op, action ou émotion inconnue, `set` sans clé), un `PlayDance` vers une danse ni compilée ni sur la carte, un champ chaîne (`ip`, `clk`, `tmr`) comparé comme un nombre, une règle sans porte dont la condition est vraie à 0 sur un champ que le firmware ne publie jamais (elle part donc toute seule), un seuil hors de la plage du champ, plus de règles que le moteur n'en garde, un champ qui ne diffère d'un champ publié que par la casse, et une personnalité qui nomme un fichier de règles absent de la carte. Émotions, danses, champs publiés et plafonds sont lus dans la source plutôt que recopiés. Les fichiers livrés passent. `PLUGINS.md` a aussi cessé de dire qu'un nom de danse inconnu fait échouer l'analyse d'une ligne : ce n'est plus vrai depuis que les danses sont résolues au déclenchement.

### Le ROADMAP garde le pilotage et cesse de porter ce qui vit ailleurs (09-25)

`docs/ROADMAP.md` passe de 1 574 à 1 138 lignes sans perdre un fait. Le journal des lots T0-T9 devient un glossaire d'une ligne par label, qui renvoie à l'endroit où chaque sujet est documenté aujourd'hui (le code et `validation/` citent encore ces labels) ; les deux choses qu'il était seul à porter, les effets overlay et le mouvement des yeux propre à chaque émotion, passent dans `reference/EYES.md`. Les spécifications des deux bins livrés sont devenues leur documentation : §5 (space) et §9 (led-fluid) sont désormais des renvois, les pièges numérotés que cite le code de space vivent dans `guests/SPACE.md` § Les dix pièges, et les choix de conception et questions ouvertes de led-fluid dans `guests/LED-FLUID.md` § Notes de conception. Chaque commentaire de code citant `ROADMAP §5.x`/`§9` a été redirigé, ainsi que trois renvois morts (`ROADMAP-v2`, un `§2.0` inexistant, l'ancien nom `TESTS-SONNET` de `PLAYBOOK-HW`). Retirés aussi : la mention des fichiers d'avant fusion `PLAN-V2.md`/`planv2.md` et les notes disant qu'un document était écrit pour un modèle d'IA donné.

### Releases publiées : des binaires prêts à flasher et un guide d'installation (09-25)

Plus besoin de PlatformIO pour faire tourner le projet. Chaque release GitHub porte le companion en image USB complète (bootloader + partitions + otadata + application, flashée à `0x0`, y compris depuis le navigateur avec esptool-js) et en `companion.bin` pour l'OTA et la carte SD, les quatre applis invitées, les trois images M5Stack Fire, une carte SD prête à décompresser et `SHA256SUMS.txt`. L'image factory remet `otadata` à zéro, donc elle démarre ce qu'elle vient d'écrire — le piège « flash réussi, ancien slot démarré » de `hardware/LIMITS.md` ne s'applique pas à ce chemin. `docs/INSTALL.md` est le guide unique, valable pour toutes les releases : flash, carte SD, premier WiFi, configuration, applis invitées, mise à jour OTA (dont l'étape `/companion.bin` qu'on oublie), Fire, dépannage. `scripts/release/make-release.ps1` fabrique l'ensemble ; il refuse un arbre modifié et des `SCE_WIFI_SSID`/`SCE_WIFI_PASS` définies, que les environnements `-fire` compileraient dans un binaire public.

### Changer la langue de la console en laissait la moitié en arrière (09-22)

Trouvé grâce à une capture d'écran : une console en anglais affichant « 23 règles · 23 actives » dans l'en-tête de la section Règles, chaque autre étiquette correctement en anglais. Cause racine : `applyLang()` ne balaie QUE les éléments marqués `data-i18n` — tout ce que le JS construit en appelant `t()` directement et écrit dans le DOM (le tableau des règles et son compteur, la liste des fichiers, le texte de repli du menu déroulant des danses, le tableau de réglages) n'était jamais câblé pour se re-rendre au changement de langue. `setLang()` (le bouton EN/FR) retraduisait chaque étiquette statique immédiatement, mais ne relançait jamais les quatre chargeurs produisant ce texte dynamique — ce qu'ils avaient rendu en dernier restait affiché, dans la langue active À CE MOMENT-LÀ, jusqu'à ce qu'on presse à la main le contrôle de rafraîchissement correspondant.

Même faille, version plus discrète, au DÉMARRAGE : `loadRules(null)` s'exécutait une ligne AVANT `applyLang()`, donc le tout premier rendu du tableau des règles utilisait toujours le défaut anglais codé en dur plutôt que la langue configurée sur la carte, ne se corrigeant que si l'utilisateur retouchait les règles plus tard. `loadDances()`/`loadSdList()`/`refreshTun()` avaient le même problème d'ordre. Corrigé une fois pour toutes : les quatre tournent maintenant depuis un seul `refreshDynamicText()`, appelé à la fois après `applyLang()` au démarrage et depuis les chemins de succès et d'échec de `setLang()` — le même correctif sert les deux bugs, parce que c'était le même bug à deux moments différents.

Par ailleurs, quatre chaînes utilisées seulement par la ligne d'état du micro (`snd_mact`/`snd_moff`/`snd_mstby`/`snd_mwarm`) n'avaient aucune entrée anglaise et aucun élément `data-i18n` pour en capturer une — une console en anglais affichait le nom littéral de la clé au lieu d'une phrase. Texte anglais manquant ajouté. Deux autres clés (`l_rules`, `perso_def`) se sont révélées mortes : déclarées en français, lues par rien. Retirées.

### Une table de personnages pleine refuse un 9e fichier à voix haute, et ne perd jamais la trace de qui tourne (09-19)

Trois bugs trouvés en revue, aucun vu sur le matériel pour l'instant. `PersonalityStore::loadAll()` faisait un `break` hors de TOUT le balayage du répertoire dès qu'un fichier de création trouvait la table pleine — un 9e fichier ne faisait donc pas qu'échouer à créer un personnage, il arrêtait silencieusement la lecture de tous les fichiers SUIVANTS dans l'ordre du répertoire, y compris de simples modifications de personnages déjà chargés. La boucle ne saute maintenant que le fichier qui ne peut pas obtenir de slot, le journalise (`[perso] '<id>' ignoree : table pleine`, compté dans le total de problèmes du rechargement), et continue de lire le reste du répertoire.

Par ailleurs, les slots de la table sont réassignés par l'ordre du répertoire à chaque rechargement, donc créer ou supprimer un personnage peut décaler l'index d'un autre, sans rapport — et le gestionnaire créer/modifier/supprimer de la console réappliquait aveuglément l'index brut encore tenu par `tuning.personality`, ce qui pouvait faire basculer silencieusement le personnage ACTIF vers un autre. Il porte maintenant le nom du personnage actif à travers le rechargement et en résout l'index par nom ensuite (repli sur le défaut compilé seulement si ce personnage était celui qu'on venait de supprimer).

Également : le commentaire à côté du `cmd.i` stocké de `PlayDance` prétendait que c'était « la valeur sur laquelle le puits se replie si le nom ne se résout jamais » — ça n'a jamais été le cas ; `rulePost` (main.cpp) a toujours résolu par nom seul, et un index stocké qui résoudrait quoi que ce soit réintroduirait exactement le bug d'index périmé que la résolution au déclenchement existe pour éviter. `cmd.i` reste (sa valeur index-compilé-ou--1 est fixée par `test_rulestore`), le commentaire dit maintenant ce qu'il est réellement : informationnel seulement.

### Le geste de zoom pincé pouvait rester muet sur le vrai matériel, et ne le peut plus (09-19)

Le zoom à deux doigts de `flight-radar` lisait `M5.Touch.getDetail(0)`/`getDetail(1)`, gardé par `isPressed()` — mais les slots de `getDetail()` sont indexés par l'id tactile MATÉRIEL du FT6336, qui tourne entre les contacts et n'est pas compacté, exactement le défaut que le « Piège 2 » de `TouchGestures.h` documente et corrige déjà pour chaque autre geste de ce robot. Deux doigts réellement posés pouvaient atterrir dans les slots 0 et 2, laissant `getDetail(1)` périmé ou vide et le pincement ne s'enregistrant jamais silencieusement, même avec `getCount() == 2`. Passé à `getTouchPointRaw(0)`/`getTouchPointRaw(1)` — compacté par la lecture du panneau, donc les index 0/1 sont simplement les deux premiers points rapportés cette passe, quel que soit l'id matériel qu'ils portent — le même correctif déjà utilisé partout ailleurs où le tactile est lu sur ce projet.

### Le projet s'appelle StackChan-Companion maintenant, sur la carte comme dans les docs (09-19)

Renomme depuis StackChan-Eyes : les yeux n'ont jamais ete qu'une piece d'un
compagnon qui bouge aussi la tete, danse, ecoute et heberge des applications
invitees, et l'ancien nom sous-vendait cela. Renomme partout ou le nom
apparait -- docs, `library.json`, le titre de la console elle-meme, l'en-tete
LICENSE, le remote git -- avec une exception deliberee : `SD_ROOT_OLD` dans
`firmware/common/SdRoot.h` garde l'ancien chemin, c'est precisement ce qui
permet a une carte deja deployee de s'auto-reparer.

**Le repertoire de configuration SD bouge aussi, et il bouge TOUT SEUL.**
Chaque firmware qui monte la carte -- le companion et les quatre binaires
invites, y compris les variantes `*-fire` qui tournent sans aucun companion --
appelle `sce::migrateSdRoot()` une fois la carte confirmee montee :
`/stackchan-eyes` devient `/stackchan-companion` par un simple renommage FAT
(une seule reecriture d'entree de repertoire ; les fichiers et le
sous-repertoire `personalities/` en dessous ne bougent jamais), et chaque
chemin du firmware pointe deja vers le nouveau nom. Une carte qui n'a jamais
vu le nouveau firmware reste intacte jusqu'a ce qu'elle le voie ; une carte
deja migree est laissee tranquille (l'existence du nouveau chemin est ce qui
arrete la migration, verifiee avant meme d'interroger l'ancien chemin).
`test_sdroot` fixe la decision a trois branches -- rien a faire sur une carte
neuve, rien a faire une fois migree, deplacer sinon -- comme une fonction
pure, le meme raisonnement qui a place le calcul de borne des cadrans de
personnalite dans `Personalities.h` plutot qu'en ligne : un renommage de
systeme de fichiers n'est pas lui-meme testable en natif, mais la decision de
SAVOIR s'il faut le faire l'est.

**Le bug que ce renommage a expedie, trouve sur le robot physique et nulle
part ailleurs.** `WebApi::sdPathAllowed()` filtre chaque lecture, ecriture et
suppression SD via une liste blanche qui, pour trois controles, localisait le
nom de fichier en cherchant un `/` a partir d'un decalage code en dur dans le
chemin (16, 16, et un `strlen(PERSO)` de 30). Renommer les CHAINES de chemin a
deplace la frontiere que ces nombres decrivaient sans deplacer les nombres :
`/stackchan-companion/` est cinq caracteres plus long que `/stackchan-eyes/`,
donc les trois recherches tombaient desormais sur le slash final de la racine
elle-meme au lieu d'un vrai separateur de sous-repertoire, et chacun de
`config.yaml`, `rules.txt`, chaque yaml invite et chaque fichier de
personnalite revenait en `403 chemin interdit` -- un chemin correctement
ecrit, correctement existant, refuse par un decalage qui ne decrivait plus
rien. Rien dans les 427 tests natifs n'aurait pu l'attraper : la fonction vit
dans `WebApi.h`, qui a besoin du `String` d'Arduino et n'est pas testable
nativement, et chaque litteral renomme SEMBLAIT coherent a l'inspection --
trois points d'appel, une constante de chaine, et trois entiers bruts
n'etaient jamais lies entre eux dans la source, donc un renommage global a
touche le premier et est passe a cote du second. Trouve seulement parce que
le robot reel signalait encore un ancien chemin sur `/api/personalities`
apres que la migration ait reussi, ce qui a mene la piste jusqu'a la seule
route capable de produire cela : une lecture refusee en silence. Corrige en
derivant les deux decalages de la meme chaine sur laquelle les controles de
chemin s'appuient deja (`strlen(SD_ROOT)`, `strlen(PERSO)`) plutot qu'en
recopiant leur longueur a la main une seconde fois -- le prochain renommage
de racine ne peut plus rouvrir exactement cette faille, puisqu'il ne reste
aucun second nombre a oublier.

Une consequence a nommer pour qui met a jour une carte a la main plutot que
de laisser le robot la migrer : un fichier de personnalite ENREGISTRE depuis
la console *avant* cette mise a jour porte son champ `rules:` ecrit comme un
chemin absolu litteral, et un renommage de repertoire n'atteint pas le texte
assis a l'interieur d'un fichier -- seule l'entree de repertoire bouge. Une
personnalite dont le fichier de regles n'a jamais ete re-enregistre apres la
mise a jour tracera `regles ABSENTES … repli sur defaut` en serie et retombera
sur les regles de la personnalite par defaut (deja le comportement existant
et teste du projet pour tout fichier de regles absent, pas un nouveau mode
d'echec) -- re-enregistrer cette personnalite une seule fois depuis l'onglet
Caracteres reecrit le chemin sous la nouvelle racine et regle la question
pour de bon.

### Une personnalite peut regler son propre rythme, pas seulement choisir un fichier de regles (09-19)

Deux cadrans de RESSENTI rejoignent la table de personnalite : `transition_scale`
(0.3-3.0, a quelle vitesse le robot passe d'une humeur a l'autre) et
`pitch_bias_scale` (0.0-2.0, a quel point sa posture penche dans une humeur --
zero garde la tete droite quelle que soit l'humeur). Les deux sont des ECHELLES
sur la centaine de durees et d'inclinaisons de tete compilees par emotion dans
`transitionFor()`/`pitchBiasFor()`, jamais des remplacements, donc un
personnage plus vif ou plus stoique reste reconnaissablement le meme
personnage. Console : deux nouveaux curseurs dans l'onglet Caracteres ; API :
`GET`/`POST /api/personalities` gagnent `tscale`/`pbscale` et
`transition_scale`/`pitch_bias_scale`.

Le calcul de la borne vit dans `Personalities.h` en fonctions pures plutot
qu'en ligne dans `Brain::setTransitionScale`/`setPitchBiasScale`, parce que
`Brain` ne peut pas etre teste nativement du tout -- son constructeur ouvre une
file FreeRTOS et epingle une tache. `test_personality_feel` balaie les deux
echelles et la duree qui en resulte sur une large plage d'entrees, NaN et
infini compris, plutot que de faire confiance a une poignee de points
d'echantillon -- et ce balayage a attrape un vrai bug : le `clampVal` partage
du depot (`src/engine/Units.h`) laissait passer NaN sans le borner, parce que
les deux comparaisons de NaN sont fausses. Toute borne materielle construite
sur `clampVal` -- lacet servo, regard -- partageait la meme faille ; corrigee
une seule fois, a la racine, plutot que contournee localement pour les deux
nouveaux cadrans.

### La console a aussi un visage clair, jour ou nuit, quel que soit le theme (09-15)

Chaque theme de la console declare desormais une face SOMBRE et une face
CLAIRE, choisies par le reglage propre du navigateur
(`prefers-color-scheme`) -- aucun interrupteur a chercher, car le robot n'a pas
d'avis sur la piece ou l'on se trouve, et interroger le systeme une fois est
plus honnete qu'un bouton que personne ne remarque. Un theme redessine
desormais son propre fond, son encre et son verre, pas seulement ses accents :
une page qui ne ferait que changer la couleur d'un lien au nom d'un caractere
n'aurait pas vraiment l'air d'une autre console.

**Le theme Gundam est devenu la vraie palette du RX-78-2.** Il partait sur une
estimation generique vert/or ; corrige en bleu, or, rouge et blanc/bleu -- le
mode sombre est un blindage marine avec le blanc en accent interactif et l'or
en filet, le mode clair est un blindage blanc casse ou le bleu Gundam prend le
role que le blanc tenait la nuit (celui des deux qui se fondrait dans son PROPRE
fond est exactement celui dont l'autre mode a besoin). L'or reste proche du
meme hexadecimal dans les deux modes -- il se lit sur l'une ou l'autre surface
-- approfondi une fois pour rester visuellement distinct du blanc plutot que de
se lire comme un citron pale a cote de lui dans la rangee d'icones.

**Les boutons ont cesse de melanger deux teintes sans rapport sur une seule
forme.** `--acc-grad` faisait filer l'accent principal droit vers le
secondaire -- cyan vers violet par defaut, bleu vers or sous Gundam -- ce qui se
lit comme un drapeau sur un petit controle plein plutot que comme un reflet.
C'est desormais un degrade MONOTONE (`--acc` vers une teinte plus claire de
lui-meme, `--acc-lt`), et l'accent secondaire est reserve au texte plat
seulement : libelles de section, puces, filets. Trois declarations brutes a
deux tons (le logotype d'en-tete, l'insigne du logo, le remplissage de la barre
de progression) pointaient vers l'ancien couple directement plutot que par le
jeton partage, et demandaient le meme correctif a la main.

**`check-contrast.py` resout desormais la vraie cascade, pas un seul bloc.**
Lire la declaration sombre d'un theme et sa declaration claire separement
aurait manque exactement le bug livre en premier : `[data-theme=gundam]{...}`
apparait deux fois dans le fichier, une fois nu et une fois dans la requete
media claire, et une regex sans notion d'« a l'interieur d'un bloc » laissait la
SECONDE occurrence ecraser silencieusement la premiere comme « sombre » -- la
variante Gundam sombre etait donc jugee contre des couleurs qui n'etaient
jamais destinees a ce fond. Corrige en calculant d'abord l'etendue de chaque
bloc `@media` et en excluant les correspondances a l'interieur de la collecte
sombre ; attrape immediatement en repointant le portail sur lui-meme (`gundam`
annoncait 2,28:1 sur un canal qui aurait du lire 15:1). Le portail resout
desormais QUATRE couches dans l'ordre ou le navigateur les applique vraiment --
base sombre, base claire, theme sombre, theme clair -- et produit quatre
variantes mesurees : default, default (clair), gundam, gundam (clair). Les
quatre sont conformes.

**L'en-tete a cesse d'etre le seul element sans theme.** Son fond translucide
etait un rgba sombre fixe quel que soit le mode -- invisible sur une aurore
sombre, un bandeau sombre plat flottant sur une aurore claire. Un jeton de plus
(`--head-glass`), le meme correctif que partout ailleurs : declare par mode, lu
plutot que fige en dur.

**Les champs de l'onglet Caracteres n'etaient jamais vraiment casses -- ils
n'etaient pas qualifies.** Deux champs n'avaient aucun attribut `type` :
`input[type=text]` ne peut pas correspondre a un champ qui n'en porte aucun,
defaut HTML ou non, donc ils portaient l'apparence brute du navigateur au lieu
de la peau propre de la console, ce qui etait tout le sens de « differe
visuellement » ici. Les quatre boutons d'action utilisaient une classe `.btn`
inventee a cote de celles que le reste de la console possede deja -- `.b-acc`
pour l'unique action principale qu'un panneau offre, `button` nu pour les
secondaires, `.ko` pour supprimer -- ils correspondent desormais a tout autre
bouton de la console au lieu d'un systeme parallele que personne d'autre
n'utilise. Le nuancier de couleur a la taille des autres controles plutot que
d'etre etire en barre par une regle prevue pour des champs de texte. Et le
panneau portait un niveau de titre qu'aucun autre endroit de la console n'a
(`<h3>`, stylee nulle part) -- scinde en sa propre carte a la place, un titre
par section comme partout ailleurs.

**Les yeux du logo sont desormais asymetriques**, reprenant la seule signature
visuelle que ce robot porte reellement sur son propre ecran (A2.17 : centres
equidistants, jamais de taille egale) -- deux pastilles identiques se lisent
comme un glyphe de robot generique, pas comme celui-ci.

Trois fichiers egares des propres tests de cette session -- `default.yaml` et
`haro.yaml` avec leurs themes inverses, plus un `zaku.yaml` oublie -- ont ete
retires de la carte ; les defauts compiles sont ce qu'une carte neuve execute.

---

### La console sait fabriquer un caractere (09-15)

`http://<ip>/#perso` — un onglet Caracteres qui cree, modifie, duplique et
supprime, en ecrivant `/stackchan-companion/personalities/*.yaml` par
`POST`/`DELETE /api/personalities`. Le caractere voyage d'un bloc : le robot ne
fusionne jamais deux demi-mises a jour, et un champ omis garde sa valeur —
envoyer `color` seul change la couleur et rien d'autre.

**La console porte la peau du caractere actif.** `theme:` est appose sur la
racine de la page et la palette suit le robot. Seuls les ACCENTS bougent : les
encres lisibles sont fixes, car une peau capable de les deplacer pourrait rendre
la page illisible — la seule panne qu'on ne peut pas reparer depuis la page.

**La palette de la console est donc verifiee en contraste**, pour la premiere
fois. Elle etait hors du portail tant qu'il y avait une palette que personne ne
modifiait ; une personnalite qui choisit une peau multiplie une surface non
verifiee, donc `check-contrast.py` a recu une passe sur `WebConsole.h` — en
couleur vraie et non en RGB565, jugee sur le panneau de verre et non sur la page,
le noir flattant chaque encre. Les deux palettes sont conformes. Prouve qu'il
mord : un vert volontairement terne echoue a 2,22:1 et le portail sort en 1.

**Trois choses que la premiere capture a montrees, et qu'aucun test n'aurait
vues.** Le nuancier affichait un vert franc pour le caractere par defaut, qui n'a
aucune couleur — grise desormais, pilote par la case « palette des emotions ».
Supprimer etait actif sur le caractere non supprimable — desactive plutot que
simplement refuse, car un bouton qui gronde est une plus mauvaise facon
d'apprendre une regle qu'un bouton jamais offert. Et trente curseurs a zero se
lisent « ne tire rien » alors que le caractere par defaut laisse en fait la table
du firmware tranquille — dit en toutes lettres a la place.

Le selecteur et l'interrupteur des humeurs ont QUITTE Options, en y laissant une
note. Deux controles pour une meme cle de tuning, c'est la derive que la course
du selecteur de danses a deja coutee une fois.

---

### Un caractere peut desormais naitre sur la carte SD (09-14)

Poser `zaku.yaml` dans `/stackchan-companion/personalities/` et le robot a un nouveau
caractere au demarrage suivant : sa couleur, son theme de console, sa cadence de
roulette et ses expressions de repos, sans recompiler. Prouve sur la carte — une
personnalite qui n'existe nulle part dans le firmware n'a tire que ses quatre
emotions.

**Le nom du fichier est l'identifiant, et il decide modifier ou creer.**
`haro.yaml` correspond au Haro compile et se superpose A LUI, ce qui fait
survivre au redemarrage la modification d'un caractere livre ; un nom que
personne ne porte prend le premier slot libre. La premiere version du chargeur
ajoutait aveuglement a partir du slot 1, et le premier fichier de la carte a
REMPLACE Haro en silence — visible seulement parce que `/api/personalities`
avait ete ajoute pour voir la table de l'exterieur, la ligne serie n'etant pas
ouvrable sans redemarrer la carte.

**Il dit des differences, pas une declaration.** Une cle absente garde la valeur
du dessous : un fichier a moitie ecrit degrade vers « presque le defaut » et non
vers un caractere troue. Seule exception, `weights:` : declarer la section
remplace la table, car une fusion ne pourrait jamais exprimer « ce caractere ne
fait pas Sad ».

**Le chargement est idempotent** — la table revient aux caracteres du firmware
avant de relire le repertoire — donc un fichier supprime retire vraiment son
caractere et une modification n'est jamais appliquee deux fois.

**Les themes sont compiles et choisis par leur nom.** Une palette ecrite sur la
carte ne pourrait pas etre verifiee en contraste a la compilation, et une
console illisible est la seule chose qu'on ne peut pas reparer depuis la
console.

### L'accesseur de lecture n'etait pas celui d'ecriture (09-14)

`at()` rabat un index inconnu sur le slot 0 pour qu'un lecteur obtienne toujours
un robot qui marche. Le chargeur de la carte s'en servait pour ECRIRE : remplir
le slot 2 d'une table qui en comptait 2 ecrivait donc dans la personnalite 0 —
puis la boucle de nettoyage, passant par le meme accesseur, effacait son nom.
Symptome sur le robot : `personality=2` relu 0, et les emotions par defaut qui
continuaient.

Deux accesseurs desormais, pour deux devoirs opposes : `at()` protege le
LECTEUR, `slot()` protege la TABLE en refusant un index hors plage au lieu de le
rediriger. Le bug etait purement logique et ne demandait aucune carte SD pour se
reproduire : il est donc epingle par un test natif — regarde echouer d'abord,
`Expected 3 Was 2`, la table refusant de grandir exactement comme sur la carte.

---

### Une regle peut jouer n'importe quelle danse, et caresser le robot est une donnee (09-14)

Deux deblocages qui ne valent qu'ensemble : le robot peut reagir a ce qu'on le
touche, et repondre par une choregraphie ecrite sur la carte plutot que compilee.

**`PlayDance` transporte desormais le NOM, resolu au DECLENCHEMENT.** Il portait
un index cherche au parse : une regle nommant une choregraphie de la carte etait
donc **refusee d'emblee et en silence** — le fichier avait une ligne, le moteur
n'avait pas de regle, et rien ne le disait. Deux faits distincts imposaient la
resolution tardive plutot que de la rendre preferable : au demarrage les regles
sont lues AVANT `danceStore.reload()`, donc la banque est vide pendant l'analyse
et aucune recherche a cet instant ne pouvait trouver une danse de la carte ; et
`DanceStore` est double-banque et se recharge a chaud, donc un index memorise au
parse designe une autre danse apres un televersement. Le nom est interne dans
l'arene de `RuleStore` — un pointeur stable, comme A2.17 l'exige de tout ce qui
traverse la CommandQueue — et le puits applicatif le resout a chaque
declenchement contre la liste fusionnee. Un nom qui ne correspond a rien est
maintenant dit a voix haute sur la trace au lieu de ne rien faire en silence, ce
qui de l'exterieur ne se distingue pas d'une regle qui ne se declenche jamais.

Cela debloque TOUTE choregraphie SD pour les regles, pas seulement les quatre de
Haro.

**La caresse de la tete publie un champ.** Elle allait droit a la CommandQueue et
ne laissait aucune trace la ou une regle puisse la voir : le seul geste vraiment
intime de ce robot etait donc le seul auquel `rules.txt` ne pouvait pas reagir —
et la reponse ne pouvait pas differer d'un caractere a l'autre, ce qui est
precisement l'objet d'une personnalite. C'est un ETAT (1 tant qu'une main est la,
0 quand elle part) et non un evenement : `sustainMs` exprime alors tout seul
« tenu un moment », et le retour a 0 re-arme la regle, donc une caresse
declenche une fois quelle que soit sa duree.

Verifies ensemble sur le robot, roulette ETEINTE pour qu'aucune autre source ne
puisse produire le resultat : une caresse a donne Happy (la reponse directe),
puis Glee — qui sans roulette ne peut etre que la keyframe 6 de `haro_call.csv`.
Capteur de tete -> champ -> regle -> une danse de la carte.

Les regles Haro recuperent leurs propres danses : `haro_call` sur une
conversation qui dure et sur la sonnerie, `haro_float` au reveil et a la charge,
`haro_roll` sur une compilation reussie, plus la nouvelle regle de caresse.
Seize regles, 19/24.

`test_rulestore` recoit le contrat : un nom SD est accepte et voyage comme nom, et
le pointeur survit a l'analyse de la ligne suivante par-dessus le meme tampon.
Regarde echouer d'abord — retablir le refus au parse casse les deux.

---

### Le robot a un caractere, et on peut choisir lequel (09-13)

`POST /api/tuning?personality=1` et le robot devient un Haro : vert, reposant
dans six expressions gaies au lieu de dix-sept, obeissant a son propre fichier de
regles. `personality=0` rend le robot que cette documentation a toujours decrit.

**L'existant est la personnalite 0, pas « l'absence de costume ».** C'est un
caractere a part entiere, et c'est pourquoi la cle est un INDEX et non un booleen
`haro_mode` — un booleen serait a renommer le jour ou un troisieme caractere
apparait, et sur ce projet renommer une cle EST une migration. Le nom est paye
maintenant, tant qu'il ne coute rien.

**Une personnalite possede trois choses et rien d'autre** : son fichier de
regles, sa roulette (poids + cadence) et sa couleur d'identite. Elle n'ecrit
jamais une autre cle de tuning : luminosite, servos, son et seuils survivent au
changement dans les deux sens, et la question « quelle couche a ecrit cette
valeur » ne se pose jamais.

**La ligne qu'elle ne franchit pas** est ecrite comme une regle et affirmee comme
un test : une personnalite change ce que le robot RESSENT et combien de temps il
le MONTRE ; elle ne change jamais ce que le materiel TOLERE. Butees servo, budget
de frame et cadences de bus sont donc absents de la table par construction. Une
« personnalite » capable de garer un servo contre sa butee serait un mecanisme de
dommage deguise en costume.

**La personnalite 0 decline de declarer le moindre poids**, et c'est la
conception, pas un manque. Elle ne recopie pas la table historique, elle laisse
celle d'`EmotionRoulette` tranquille — ce qui rend « une carte sans donnees de
personnalite se comporte exactement comme avant » vrai par construction plutot
que par recopie soigneuse. Le test l'affirme en comparant une roulette neuve a
une roulette a qui l'on a applique la personnalite 0, sur 4000 tirages a graine
identique : il n'a ainsi jamais a connaitre un seul poids. Regarde echouer
d'abord — donner une table a la personnalite 0 le casse immediatement.

**Un fichier par personnalite dissout le plafond de regles.** Le moteur tient 24
regles, dont 3 natives, et refuse les suivantes SILENCIEUSEMENT — le jeu par
defaut etait a 23/24. Gater les regles sur le caractere actif aurait fait
partager ces 21 places a toutes les personnalites ; un fichier chacune signifie
qu'une seule est chargee a la fois, donc chacune dispose des 21 — et les regles
n'ont besoin d'aucun gate, puisque LE FICHIER EST LE GATE. Relever `MAX_RULES`
a cesse d'etre necessaire.

### Les humeurs aleatoires s'eteignent (09-13)

`POST /api/tuning?roulette=0`. Roulette allumee, le robot tire une emotion toutes
les 6-12 s : c'est ce qui le fait paraitre vivant quand rien ne se passe, et
aussi ce qui empeche toute expression de SIGNIFIER quelque chose, puisque le
tirage suivant ecrase ce qu'une regle vient de dire.

Ce n'est pas un gel, et la distinction porte : les emotions minutees reviennent
au repos (`SetEmotion` porte une duree par defaut et le Brain restaure
`_preOverride`), et clignements, saccades, respiration et regard gyroscopique
sont pilotes ailleurs et continuent. Ce qui s'arrete, c'est le CHOIX aleatoire.

Le tick est consomme meme eteinte : au rallumage la roulette ne tire donc pas
instantanement — la cadence reste mesuree sur l'horloge et non sur la liberation,
sans quoi le robot reagirait au fait d'avoir ete libere au lieu de vivre a son
rythme.

### Les regles Haro, corrigees contre le code (09-13)

Les jeux de regles ecrits pour ce caractere ont ete relus ligne a ligne contre
l'analyseur plutot que contre la documentation : six defauts en sont sortis,
dont quatre silencieux.

`build le 0` FIRAIT A CHAQUE DEMARRAGE. `RuleEngine` lit `getF(field, 0.0f)` sans
test d'existence : sans script PC le champ n'existe pas, vaut 0, et `0 <= 0` est
vrai — le robot fetait une compilation qui n'avait jamais eu lieu. Toute regle
`lt`/`le` avec un seuil > 0 sur un champ optionnel a cette forme. Reecrite en
codes non nuls avec `eq`.

LES REGLES DU MINUTEUR LISAIENT LE MAUVAIS CHAMP. `tmr_st` est un code COULEUR,
pas un etat ordonne, et il n'est pas monotone : `Idle=0 Run=1 Paused=2 Ring=3
Work=1 Break=4 Hydrate=4 Done=4`. `tmr_st ge 3` attrapait donc la sonnerie mais
aussi chaque pause et chaque rappel d'hydratation, et `ge 1` tout sauf Idle —
posant « Focused » pendant les pauses, ce qui est l'inverse du sens. Le bon champ
est `tmr_ph`, publie a cote precisement parce que `tmr_st` confond des phases.

`light gt 80` ETAIT INATTEIGNABLE, et c'est de l'arithmetique, pas un avis.
`light_pct = 100·ln(v+1)/ln(4096)` : 80 % exige 775 counts bruts, alors que
l'etalonnage mesure de `Ltr553.h` dit qu'un bureau eclaire lit 50-100 derriere la
coque K151. La regle demandait 8 a 15 fois un bureau eclaire.

Aussi : `night` n'est pas le crepuscule solaire (celui-la ne vit que dans
`/api/sensors`) mais le mode nuit du capteur de lumiere, pendant lequel la
roulette met DEJA Sleepy en dominante — la regle qui le doublait ne faisait
qu'epingler Sleepy sur un robot deja assoupi. Et le jeu etait a exactement 24/24,
la ou `add()` refuse en silence. Desormais 23/24, une place libre, verifie par
script et non a la main : la premiere reecriture etait revenue a 21 regles et
reproduisait le defaut.

### `haro_float.csv` ne savait pas flotter (09-13)

`holdMs` est la duree TOTALE de la keyframe, pas un supplement
(`Sequencer.h:44`) : `servo 300 / hold 600` signifie « bouger 300 ms, puis rester
immobile 300 ms ». La moitie de la danse etait figee — une suite de hochements
plutot qu'une derive. Reecrite avec `hold == servo` : le temps mort passe de
~50 % a 2 %. Les trois autres passent telles quelles, et leur temps mort est
delibere — `haro_scan` a 51 % fait regarder-tenir-regarder, ce qui est de la
bonne choregraphie.

---

### Deux doigts, une reponse -- un geste qui marchait un nombre pair de fois (08-25)

Maintenir deux doigts sur le visage trois secondes bascule la rangee de debug
(`emotion . ip`). Cela ne marchait pas, et la raison merite d'etre gardee :
chaque piece prise separement se comportait bien.

Mesure sur le panneau pendant UN maintien delibere de 8,9 s -- les compteurs ont
ete publies par `/api/sensors` et non par la ligne serie, car ouvrir COM6
redemarre la carte en USB natif et aurait relance l'etat meme qu'on mesurait.
La condition « >= 2 points » a demarre CINQ fois, la vitre a lu completement
vide entre-temps, et le geste s'est verrouille deux fois et a repondu deux fois.
Comme c'est une BASCULE, la seconde reponse a defait la premiere. Le constat
etait « ca ne marche pas » ; la verite etait « ca marche un nombre pair de
fois ».

**Le defaut etait un anti-rebond asymetrique.** L'armement attendait deja 150 ms
avant de croire un second doigt -- deliberement, car le panneau signale un doigt
qui se leve comme deux pendant quelques mises a jour, et se verrouiller sur ce
talon avait deja avale tous les gestes du bandeau qui suivaient. Le RELACHEMENT
n'attendait rien : une seule passe lisant zero terminait le geste. Le decrochage
meme que l'armement etait fait pour survivre terminait donc le contact, et la
passe suivante demarrait un second geste a l'interieur du premier.

Les deux bords sont desormais anti-rebondis, et la tolerance est accordee a un
geste ETABLI, jamais a un candidat -- cette asymetrie est ce qui empeche le
piege du talon de rentrer par le correctif. Le minutage a quitte `TouchGestures`
pour `src/interact/TwoFingerLatch.h`, sans M5Unified dedans et teste nativement
(`test_twofinger`, 8 cas). Le test de non-regression a d'abord ete regarde
echouer contre l'ancien comportement : `Expected 1 Was 2`, exactement le
symptome mesure.

**Une premiere lecture a accuse la mauvaise piece.** Doigts rapproches, le
controleur ne signalait qu'un seul point pendant toute la tentative, et la
lecture honnete de cela -- le panneau ne sait pas offrir de second point --
aurait mene a refaire le geste autour d'un autre capteur. Une lecture directe du
`TD_STATUS` du FT6336U, sous toute la pile M5GFX, a dit 2 des que les doigts
etaient ecartes. Le panneau fusionne les contacts proches : c'est une exigence
d'ecartement, pas une capacite absente.

### La rangee de debug dessine ou sont vos doigts (08-25)

Tant que `band_debug` est active, chaque contact recoit une verticale et une
horizontale qui se croisent sous lui, une couleur par point. Cela existe a cause
du paragraphe ci-dessus : tant qu'on ne peut pas VOIR une seule croix la ou on a
pose deux doigts, « ecartez-les » est un conseil invisible.

Trace dans `pushEyeZone()` -- le point de sortie unique de tous les chemins de
rendu -- pour ne pas disparaitre a un clignement ni sur l'ecran « ... », et
apres le post-traitement CRT, puisqu'un instrument lui-meme macule ne mesure
rien. Les quatre segments possibles sont une table parcourue par une seule
boucle : ecrits en quatre appels `fillRect` dans un meme corps, ils sont
exactement le groupe que le backend Xtensa peut amincir, et le symptome serait
une croix privee d'un bras en permanence. Le portail compte le point d'appel
DANS le binaire lie -- et a pris l'entree en defaut du premier coup, la
signature mangled ayant ete ecrite de memoire avec le mauvais prefixe de
longueur.

---

### Le pomodoro, c'est deux cibles qu'on voit, pas une direction a retenir (08-25)

Le swipe vertical qui faisait defiler la forme d'une session aura vecu moins
d'un jour : il se visait mal sur une bande de quarante pixels, ce qui est un
verdict juste pour un geste sans affordance a l'ecran. Il est remplace par un
TAP SUR CE QUI EST DEJA DESSINE -- l'icone de phase fait defiler la forme, les
chiffres demarrent et mettent en pause. Une commande qu'on peut montrer du
doigt vaut mieux qu'une direction qu'il faut se rappeler, et aucune des deux
n'a demande un pixel de plus : la bande les affichait deja.

**La limite entre les deux est celle de la mise en page elle-meme.** Les
chiffres sont CENTRES : eux et l'icone glissent donc avec le texte, et
`1/4 25:00` et `1/4 120:00` posent la vignette a douze pixels d'ecart. Une
zone de touche ecrite en dur serait juste pour un texte et fausse en silence
pour les autres ; le peintre et le doigt lisent desormais le meme
`units::bandTextX0()` (Units.h gagne une section 7, et la copie de la formule
qui vivait dans le renderer disparait). Tout ce qui est a gauche du premier
chiffre EST l'icone, ce qui fait de toute la marge gauche la cible plutot que
de la vignette de 18 px -- sur la dalle, c'est la difference entre une commande
qu'on atteint et une commande qu'on vise.

Toujours seulement entre deux sessions, et pendant une session TOUTE la bande
est depart/pause : le verrou d'edition du compte a rebours (08-04) reste, mais
l'icone ne devient plus morte dessous. Un tap qui ne fait rien est un trou dans
la bande.

Epingle par deux tests, et on les a d'abord vus ECHOUER : passer le decalage de
l'icone de 30 a 22 donne `Expected 12 Was 4` -- les douze pixels d'air sur
lesquels la mise en page est batie. Ce qu'ils prouvent n'est pas le nombre mais
l'ACCORD, puisque les deux memes fonctions decident maintenant ou la vignette
est peinte et ce qu'un tap voulait dire.

### Plus un seul geste du bandeau ne repondait, et le compte mentait (08-25)

Le minuteur ne se reglait plus en maintenant et glissant, le bandeau de son ne
faisait plus defiler ses habillages, les taps sur la bande ne faisaient rien.
Une seule cause pour les trois, et c'etait le maintien a deux doigts de la
veille.

**`getCount()` ne compte pas des doigts.** Il rend le nombre de CASES de detail
dont l'etat n'est pas `none`, et M5Unified indexe ces cases par l'identifiant
MATERIEL du point -- le quartet haut de P1_YH sur le FT6336, qui TOURNE d'un
contact au suivant. Une case survit en outre deux passes au lever : une pour
devenir `touch_end`, une pour s'effacer. Mis ensemble, un doigt qui se pose sur
l'id 1 moins de ~60 ms apres un lever depuis l'id 0 fait repondre DEUX au
compte, avec un seul doigt pose. Ce n'est pas un cas limite : c'est ce que fait
un glissement lent chaque fois que la dalle perd puis retrouve le contact,
c'est-a-dire exactement ce dont le reglage du minuteur est fait. La branche a
deux doigts s'enclenchait, et elle s'enclenche jusqu'a ce que la dalle soit
libre : tout le contact etait avale.

`isPressed()` pose la question qu'on voulait poser : vrai seulement pour une
case que la dalle a rapportee dans CETTE passe. L'appui et le relachement
viennent maintenant des fronts de ce compte, et non de
`getDetail(0).wasPressed()/wasReleased()`, et la position vient du tableau brut
TASSE -- ce qui ferme du meme coup un second piege, plus ancien :
`getDetail(0)` est la case zero, et pour un contact que la dalle a numerote 1
c'est la donnee perimee de quelqu'un d'autre, inatteignable a travers
l'accesseur de M5Unified qui borne son indice. Un contact a un doigt interrompu
emet aussi desormais sa fin de glissement, faute de quoi le drapeau
"ce glissement a deja compte" du consommateur s'enclenche et mange le tap
suivant -- la regression du 08-04, atteinte par l'autre bord.

**L'analyseur de son saccadait parce qu'un bloc refuse etait compte quand
meme.** `M5.Mic.record()` rend faux quand le pilote ne peut pas prendre le
tampon, et ce retour etait jete. Le compteur de blocs en vol disait alors deux
la ou le pilote en tenait un, et ce mensonge ne se repare jamais : chaque passe
suivante trouvait plus de blocs en vol que d'enregistrements, tendait au
visualiseur un tampon QUE PERSONNE N'AVAIT REMPLI, et poussait la queue au-dela
de la tete. L'oscilloscope rejouait et sautait au lieu de defiler. Un bloc
refuse n'est plus compte ; la file est courte d'un bloc le temps d'une passe, et
la passe suivante la recomplete.

**Un swipe vertical sur le pomodoro fait defiler la forme d'une session** --
25/5x4, 50/10x3, 90/20x2, 15/3x4. Le mode 5 etait le dernier bandeau a ne rien
repondre a ce geste. Ce qu'il fait defiler est la FORME et non un nombre isole,
parce que c'est le reglage qui vaut un doigt, et parce que la bande dit deja la
reponse : a l'arret elle affiche "1/N MM:00", donc les deux nombres qui bougent
SONT l'etiquette. Seulement entre deux sessions -- un pomodoro en course n'est
pas reforme par un frolement, c'est le meme verrou d'edition que le compte a
rebours depuis le 08-04. Un reglage qui ne correspond a aucune forme tombe sur
la premiere forme du sens demande : un 30/7 saisi dans la console n'est pas
"le classique", et le traiter comme tel ferait repondre au premier swipe en ne
changeant rien de visible. La table et son enroulement sont purs et testes
nativement ; le cablage tactile ne fait qu'appliquer le resultat.

**La danse de fin resout desormais la liste entiere.** `timer_dance` indexe
`/api/dances`, c'est-a-dire les danses internes suivies des choregraphies SD --
mais il partait droit dans `PlayDance`, qui ne connait que le prefixe interne.
Choisir un `.csv` de la carte et le dernier bloc d'un pomodoro finissait en
silence, sans erreur nulle part, l'indice etant borne et ne faisant simplement
rien. Les deux moities se resolvent ici, dans l'ordre ou l'endpoint les
concatene : la promesse de la console -- sa liste ne peut pas diverger de celle
du robot -- est vraie au lieu d'etre seulement ecrite.

**La console montrait un curseur d'hydratation pose au milieu du navigateur**
tandis que son propre affichage disait 1 : pas d'attribut value initial, et
absent de la liste que `refreshTun` synchronise -- la seule chose contre
laquelle le commentaire pose a cote de cette liste met en garde. Le selecteur de
danse avait le defaut miroir : deux fetches en course l'ecrivaient et le dernier
arrive gagnait, si bien qu'un choix enregistre s'affichait "aucune" pendant que
le robot dansait quand meme. Une seule fonction possede cette valeur, et elle
est appelee des deux bords.

### Le build n'etait plus en C++17, et un portail ne pouvait plus passer (08-25)

`build_unflags = -std=gnu++11` etait ecrit pour le core Arduino 2.x, qui
ajoutait ce drapeau APRES `build_flags`. Le core 3.x ajoute `-std=gnu++2b` puis
`-std=gnu++2a` : l'unflag ne retirait plus rien, notre `-std=gnu++17` etait
supplante, et le firmware compilait en C++20 pendant que ce depot, les tests
natifs (gnu++17) et toute la documentation disaient C++17. Le meme piege que la
premiere fois, avec une autre valeur -- d'ou les trois normes listees plutot que
la seule qui casse aujourd'hui. Retour au C++17 : c'est la norme sous laquelle
ce code a ete ecrit, teste et relu, et passer en C++20 est une decision qui se
prend seule, avec l'env `native` aligne dans le meme geste, pas un effet de bord
d'une migration de plateforme. Verifie comme il aurait fallu le faire la
premiere fois : en lisant les drapeaux reellement passes au compilateur.

`check-a222.py` venait d'apprendre a ECHOUER quand objdump manque, au lieu
d'annoncer un saut et de rendre succes. Il codait `.exe` en dur, ce qui rendait
cet echec definitif sur Linux et macOS -- le meme defaut, le signe inverse. Les
deux orthographes et les deux dispositions de chaine d'outils sont desormais
cherchees, `PATH` en dernier. Et `check-all.sh` portait un antislash-n litteral
dans une continuation de ligne non protegee : `pio run` recevait un argument `n`
parasite et le lanceur POSIX ne pouvait pas construire du tout.

**Le framework ne journalise plus que les erreurs** (`CORE_DEBUG_LEVEL` 3 -> 1),
et ce n'est pas du menage. Depuis le core 3.x `Serial` EST l'USB CDC, et
`HWCDC::write` prend son verrou avec un timeout de 100 ms puis peut enchainer
plusieurs de ces attentes quand l'hote cesse de vider -- un cable branche avec
le moniteur ferme est exactement cet etat. loop() tourne sur le coeur 1 a cote
du renderer : une seule ligne INFO de la pile WiFi peut donc affamer le polling
tactile comme le ferait une frame hors budget (A2.22). Nos propres
`Serial.printf` et `sce::trace` ne passent pas par ces macros et restent
intacts ; ce qui disparait, c'est le bavardage du framework. 17 Ko de flash au
passage. A remonter a 3 le temps d'une session de debug reseau.

**Et l'avoir baisse a coute un appel de dessin, ce qui est la preuve que le
portail sert.** A2.22 a echoue sur `flight-radar-fire` des que le niveau est
tombe : `drawMetar` est passe de trois points d'appel `drawString` a deux. Le
panneau d'echec METAR dessinait sa raison puis, dessous, le code HTTP qui nomme
la panne -- deux appels dans un meme corps ne differant que par une chaine, une
couleur et dix-huit pixels de y, c'est-a-dire exactement la paire que GCC a le
droit d'elaguer. Au niveau 3 le budget d'inlining gardait les deux ; au niveau 1
le backend ESP32 n'en a emis qu'un, et le numero HTTP avait disparu du binaire.
Rien d'autre ne l'aurait dit : l'ecran dessine toujours, s'explique toujours, et
omet simplement le nombre -- sur la carte dont la panne habituelle EST un fetch
rate.

La causalite a ete VERIFIEE et non supposee : l'env a ete reconstruit au niveau
3 (trois points d'appel) puis au niveau 1 (deux), rien d'autre n'ayant bouge. Le
correctif est celui que prescrit A2.22, et non un repli sur l'ancien drapeau :
les deux lignes sont une table de deux entrees et une boucle, il n'y a donc plus
de paire a elaguer. Verifie dans le binaire lie sur les DEUX backends, qui
s'accordent desormais a deux, et le desassemblage montre le point d'appel
survivant chargeant sa chaine, sa couleur et son y depuis la table plutot que
depuis des constantes. Une forme qui ne tient qu'a un seul niveau
d'optimisation n'est pas un correctif ; celle-ci survit au drapeau qui l'a
revelee.

Le compte attendu par le portail pour `drawMetar` BAISSE donc, de trois a deux,
et cela merite d'etre dit franchement : une attente qui descend signifie
d'ordinaire qu'on a supprime une fonction pour faire passer un controle. Ici
cela signifie que deux points d'appel sont devenus une boucle -- le sens meme
dans lequel la regle pousse depuis toujours.

**La pile affichage / tactile / audio est epinglee au cran exact.** `^0.2.3`
couvre toute la ligne 0.2.x, et la reinstallation des dependances qu'a
entrainee le changement de plateforme a fait glisser M5Unified 0.2.16 -> 0.2.20
et M5GFX 0.2.22 -> 0.2.28 sans que rien ne le demande. La 0.2.28 apporte un
`Bus_I2C` reecrit et un `getSpiClockFrequency()` tout neuf -- le bus partage
G11/G12 et l'horloge SPI du LCD, les deux contentions que ce projet tient a la
main (A2.16, regle 15). M5GFX est desormais NOMME alors qu'il n'arrivait qu'en
transitif par M5Unified : une dependance qu'on ne nomme pas est une dependance
que personne ne relit, exactement la lecon d'ESP32Servo une migration plus tot.
A dire clairement : cette derive-la n'etait PAS la cause de la panne tactile --
les sources du tactile sont identiques octet pour octet de part et d'autre des
deux sauts, et cela a ete verifie avant d'epingler quoi que ce soit. On epingle
parce qu'une pile d'affichage qui bouge toute seule est une regression qui
attend une semaine calme.

`-Wno-error=return-type` est aussi porte par les trois environnements `-fire`.
Ils n'en ont pas besoin aujourd'hui -- aucun n'inclut `stackchan-arduino` --
mais `extends` REMPLACE `build_flags` au lieu de le fusionner, donc la copie du
parent ne les atteignait pas, et le jour ou l'un d'eux touchera `M5.Log`
l'erreur tombera dans un environnement dont les drapeaux ne disent rien du
correctif applique partout ailleurs.

### Le bandeau gagne un pomodoro qui rappelle de boire, et six icones (08-24)

**Hydratation.** `pomo_hydra_min` (1 min, 0 = arret) glisse une invite a boire
entre un bloc de travail et sa pause. Le placement EST la fonctionnalite : une
invite au DEBUT de la pause est une invite qu on suit en se levant ; fondue dans
la pause elle n est qu une etiquette, placee apres elle coupe le retour au
travail. C est un PREFIXE de la pause et jamais une tranche prise dessus -- la
pause qui suit est la pause reglee entiere, sinon activer le rappel ferait payer
le fait de boire. Le dernier bloc n en a pas. `0` restitue exactement l ancienne
machine, et le test qui decrit la chaine classique met desormais la valeur a
zero explicitement au lieu de s appuyer sur un defaut : le jour ou ce defaut
bouge, c est le test d hydratation qui casse et non celui qui parle d autre
chose.

**Six icones de phase**, vignettes pixel-art 18x24 a cote des chiffres : sablier
pour un compte a rebours, cerveau pour le travail, goutte d eau pour
l hydratation, tasse pour la pause, cloche pour la sonnerie, drapeau pour la
fin. LES SIX SORTENT D UN SEUL POINT D APPEL DE DESSIN (A2.22) : les glyphes
sont des donnees et une unique boucle imbriquee tamponne la table que la phase
designe. La forme evidente -- une chaine de `if` avec une petite boucle chacune
-- donnerait six corps fillRect semblables dans une meme fonction, que GCC 8.4
Xtensa a le droit d elaguer, et le symptome serait une phase dont l icone n
apparait jamais, en silence. `check-a222.py` epingle desormais `drawBandClock` a
exactement deux points d appel fillRect.

L icone lit un NOUVEAU champ, `tmr_ph`, publie a cote de `tmr_st` et non deduit
de lui : `tmr_st` est un code de couleur ou pause, hydratation et fini partagent
le vert ; en deduire l icone donnerait le meme dessin a l invite a boire et a la
pause, ce que les icones existent precisement pour eviter.

**Une danse a la sonnerie.** `timer_dance` (0 = aucune, sinon l indice 1-based
dans `/api/dances`) se declenche sur Ring et AllDone seulement, jamais a un
changement de phase : ceux-la reviennent toutes les quelques minutes, et un
robot qui se leve aussi souvent est un robot qu on debranche. La console remplit
son selecteur depuis le meme fetch qui dessine les boutons de danse : la liste
ne peut pas diverger de celle du robot.

**Le point d acces force l IP a l ecran.** Un robot sur son AP de repli est un
robot que personne ne peut joindre : aucun nom ne resout, la console est le seul
moyen de lui donner un reseau, et cette adresse n existe qu a deux endroits --
la ligne serie, qui demande un cable, et cette rangee de pixels. Cela SUPPLANTE
au lieu d ecrire : `band_debug` n est pas touche et l affichage revient au choix
de l usager des qu un vrai reseau est rejoint.

**Deux doigts tenus trois secondes sur le visage** basculent cette meme rangee.
Rien d autre sur ce robot ne demande deux doigts, donc on ne peut pas y arriver
par accident -- l exigence d une commande sans affordance a l ecran. Le geste
supprime entierement le chemin a un doigt tant qu il dure : tout
`TouchGestures` lit le doigt zero, qui appuie et relache toujours sous un
maintien a deux doigts, et sans cela le geste declencherait AUSSI un tap ou un
swipe.

**Un glissement vertical sur le bandeau de son fait defiler ses trois
habillages.** Meme geste, meme endroit, sens pris du mode deja affiche ; il y
etait inerte, et un geste inerte dont tous les voisins repondent se lit comme
une panne.

### La migration de plateforme, debloquee (08-24)

Le passage a la plateforme pioarduino (core Arduino 3.x) laissait le firmware
incompilable, et voici ce qu il a fallu. `ESP32Servo` est desormais EPINGLE en
3.0.x dans les deux environnements qui tirent `stackchan-arduino` -- companion
et flight-radar, les deux seuls -- parce que la 0.13 transitive appelle encore
`ledcAttachPin`, `ledcSetup` et `dacWrite`, tous retires du nouveau core. Les
autres bins invites n en dependaient pas et compilent tels quels. Le companion
porte en plus `-Wno-error=return-type` : le nouveau core en fait une erreur et
M5Unified 0.2.20 a un chemin sans return explicite, ce qui est un defaut de la
LIB et non du projet -- on degrade donc l erreur plutot que de patcher un
fichier de dependance qu un `pio pkg update` reecrirait.

**Et A2.22 s etait tu.** La plateforme classique livre une chaine d outils par
puce (`toolchain-xtensa-esp32s3`), la nouvelle une chaine unifiee
(`toolchain-xtensa-esp-elf`). Le chemin objdump code en dur a cesse d exister,
et le portail imprimait une ligne de prose parmi des centaines puis renvoyait
SUCCES -- tout le controle A2.22 inerte avec `check-all` au vert, exactement la
panne « ca passe parce que rien n a tourne » que l en-tete du fichier decrit
pour un env non liste. Les deux dispositions sont cherchees, et un objdump
absent est un ECHEC : un controle qui ne peut pas tourner n est pas passe.

### `/api/rules` échappé de bout en bout (08-23)

Trois des quatre chaînes émises par ce point d'entrée viennent de `rules.txt` et
étaient écrites en `%s` brut ; seule la description passait par `jsonSafe()`. Le
découpage se fait sur `|` puis trim, rien ne retire un guillemet, et `RuleStore`
interne les jetons tels quels — un seul `"` dans un nom de champ fermait donc la
chaîne trop tôt et rendait le document ENTIER illisible. La table des règles de
la console s'affichait alors vide : une faute de frappe dans une règle les
effaçait toutes en silence, la cause loin du symptôme. C'est exactement la panne
que décrit le commentaire de `jsonSafe()` pour les caractères de contrôle ; la
garde n'avait été posée que sur un membre d'un ensemble qu'elle devait couvrir
en entier. `act` compte aussi : il embarque `setKey`, interné de la même façon.

Échappé plutôt que validé, délibérément : les noms de champs se résolvent à
l'EXÉCUTION contre `FieldStore`, les sources s'enregistrent au fil de l'eau, et
une règle peut légitimement nommer un champ publié plus tard. Une liste de noms
« valides » vérifiée à l'analyse serait une seconde liste dérivant des vraies
sources — le jumeau supposé qu'interdit A2.23. Un sérialiseur ne peut pas
s'appuyer sur un invariant amont qu'il ne voit pas ; il peut toujours rendre sa
propre sortie bien formée.

Quatre tampons distincts et non un scratch partagé : l'ordre d'évaluation des
arguments n'est pas spécifié en C++, les quatre appels s'écraseraient donc dans
l'ordre choisi par le compilateur. La somme de `tmp[448]` a aussi été refaite,
l'échappement pouvant DOUBLER une longueur alors que l'ancien calcul en comptait
deux brutes — le pire cas vaut désormais 433 octets, et chaque terme est une
taille de tampon écrite à la ligne suivante.

### Le portail captif de l'AP invité, complété (08-23)

Le `DNSServer` joker était là et la documentation décrivait le portail comme
fonctionnel ; la moitié qui l'ouvre ne l'était pas. Chaque système sonde une URL
qui lui est propre — `/generate_204`, `/hotspot-detect.html`,
`/connecttest.txt`, `/canonical.html` — dont aucune n'est une route de
`SceGuest` : toutes les quatre tombaient sur le 404 intégré du WebServer.
Android et Windows finissent par proposer une connexion là-dessus, mais Apple
AFFICHE la page renvoyée dans sa feuille de portail : l'usager lisait « Not
found » là où le formulaire devait être. `onNotFound` répond desormais une 302
vers `/config`, qui n'est le corps attendu par personne et se lit donc comme un
portail chez les quatre.

Posé UNIQUEMENT en mode AP. Sur un réseau rejoint, le même gestionnaire
transformerait chaque faute de frappe et chaque signet périmé en redirection
silencieuse vers la page de réglages, et une route absente doit rester absente.

### `led-fluid` arrive sur le Fire, et une passe de revue sur le travail d'hier (08-23)

**`led-fluid-fire`** — la même source, en application autonome sur M5Stack Fire.
Ce bin était annoncé non portable parce que son interface est bâtie sur le
glissement ; c'était vrai de l'interface, pas du bin. Le portage redit ce que
chaque geste VOULAIT dire et le place sur trois boutons :
`firmware/led-fluid/input.h` porte le vocabulaire, pur et testé nativement
(`test_fluidinput`, 7 cas), et `applyEvent()` en est l'unique consommateur. Une
seule chose diffère vraiment, et le vocabulaire le dit au lieu de le masquer —
éclabousser le liquide demande un point, donc une tape éclabousse là où le doigt
s'est posé et un bouton éclabousse le centre. `SCE_HAS_PY32` est un drapeau et
non une sonde, contrairement au capteur de lumière : sonder l'anneau impose
`Wire1.begin(12, 11)` d'abord, or sur un ESP32 classique les GPIO 6-11 sont la
flash SPI.

**Les barres LED étaient aveugles à la moitié des rotations.**
`ledbars::displacement()` figeait `frontIsZero` et renvoyait donc un zéro plat
pour toute vitesse de lacet négative. `EmotionLeds` se sert de cette valeur, et
de rien d'autre sur la distribution, pour décider si les barres valent la peine
d'être renvoyées — la suppression d'écriture tenait donc l'image précédente
pendant toute rotation dans un sens, et l'extrémité lointaine ne s'assombrissait
jamais à moins que la couleur ou une hauteur d'œil ne change au même moment. Elle
est signée désormais, et la suite qui donnait raison au bug (elle balayait la
vitesse depuis zéro vers le haut sans jamais passer de négative) a deux tests
pour la moitié signée.

**La calibration de démarrage pouvait ne jamais finir.** Ne compter que les ticks
CALMES retirait la garantie d'achèvement que la version inconditionnelle avait
gratuitement : `quiet` exige |accel| à 0,05 g de un, et une carte dont l'échelle
est décalée de quelques pourcents ne l'a jamais satisfait — laissant `_tilt` à
zéro toute la session sans rien dans la trace. L'attente est bornée (20 s), le
repli est consigné plutôt que caché, et `/api/sensors` publie `imu_cal` (0 en
cours / 1 fait / 2 fait sur l'échéance).

**Inverser un défaut n'est pas une migration.** `saveConfig` réécrit toutes les
clés, donc une carte déjà posée sur un robot portait `tilt_inv_x = 1` — la valeur
qu'on avait dit de mettre quand le mapping vivait dans un interrupteur au lieu de
la base. Sous les anciens noms, la mise à jour aurait ré-inversé X en silence sur
exactement les robots réglés correctement. Les clés sont renommées `imu_*` →
`tilt_*`, et ce renommage EST la migration : `loadConfig` ignore les noms qu'il ne
connaît pas et `saveConfig` tronque.

**Le fluide démarrait sur le sol.** `reset()` dimensionnait son réseau en
divisant la boîte par un pas : le nombre de colonnes devait donc être tronqué, et
les lignes alors nécessaires pouvaient dépasser la hauteur — la borne soudait la
dernière ligne à `y = WORLD_H` dans la première image de l'animation. Des
comptes entiers ne peuvent pas dépasser.

**Trois commentaires décrivaient du code qui avait été annulé**, chacun citant
une mesure qui ne tient pas. Re-mesuré sur un fluide de 400 grains au repos, la
paire la plus proche en fraction du diamètre de grain : porter la position
précédente à travers une correction de contact donne 5 % contre les 62 % livrés
au pas 8 (et une vitesse moyenne de 49 px/s contre 7) — la projection a besoin du
couplage avec la vitesse ; balayer les cellules les plus profondes d'abord gagne
le pas 8 (72 %) et le paie au pas 12 (75 % contre 88 %), et n'est « le plus
profond » que tant que la gravité pointe vers le bas d'un écran qu'un IMU
incline ; visiter chaque paire par ses deux bouts dépense la borne d'examen deux
fois plus vite et divise par deux l'écart au repos. Le code avait raison, la
prose non. Également mesuré, et bon à savoir : `MAX_TOUCH` 48 contre 96 ne change
rien à un chiffre affiché près — c'est une garde, pas un réglage — et le nombre
de passes n'est pas monotone au pas 8 (62 / 51 / 69 % pour 4 / 8 / 16), donc ce
tassement est une oscillation et non une pénurie : des passes de plus n'achètent
rien.

### Le réflexe d'inclinaison, corrigé sur trois points — et les barres gagnent une profondeur (08-22)

Mesurer la direction physique des axes accéléromètre dans le plan avec
`led-fluid` a rendu lisibles trois fautes d'`ImuReader` qui étaient invisibles
tant qu'il n'y avait rien à quoi les comparer.

**La ligne de base mangeait les vraies inclinaisons.** `_baseY` suivait à
0,0002 par tick dès que le robot était « calme » — et un robot TENU IMMOBILE sur
une pente est calme : gyro à zéro, un g d'accélération. À 100 Hz cela fait une
constante de temps de cinquante secondes : une inclinaison soutenue était absorbée
dans sa propre ligne de base, 63 % partie après cinquante secondes et les yeux
recentrés en moins de deux minutes. `VestibularSystem` affirme le contraire mot
pour mot (« au repos sur une pente les yeux TIENNENT cette cible — pas zéro ») et
le « inclinaison statique tenue » de VALIDATION.md a été vérifié sur quelques
secondes, la seule échelle de temps où les deux étaient vrais. Le commentaire
parlait de dérive thermique, et c'est le correctif : la dérive thermique erre
AUTOUR de la pose de repos, donc le suivi est désormais conditionné à y rester
proche, et assez lent (~8 min) pour être ce qu'il prétend.

**Les deux axes se contredisaient.** `_tilt.x` envoyait le regard vers le côté
haut — contre-rotation, ce que fait un otolithe — tandis que `_tilt.y` l'envoyait
vers le haut quand le robot penchait en arrière, soit la convention inverse. Les
deux contre-tournent maintenant.

**X n'avait aucune ligne de base.** Seul Y était calibré, parce qu'au repos
debout `accel.x` vaut nominalement zéro — ce qui est précisément pourquoi son
absence était invisible : tout biais de montage passait directement dans le
regard, en décentrage permanent. Les deux axes sont calibrés, et la calibration
de démarrage compte des ticks CALMES au lieu des cent premiers venus : démarrer
dans une main ne calibre plus la main.

**Les barres gagnent une seconde dimension (§10 P1+P2).** Les douze WS2812 sont
deux barres de six PERPENDICULAIRES à l'écran, et le firmware n'avait jamais
écrit qu'un nombre par barre. La luminosité déjà calculée reste l'amplitude ; un
nouvel en-tête pur (`engine/LedBars.h`, `test_ledbars`) ne décide que de la
RÉPARTITION de cette amplitude le long de la profondeur. Quand la tête tourne,
l'extrémité arrière s'assombrit. La répartition soustrait et n'ajoute jamais —
une barre peut légitimement être à 255, donc un poids supérieur à un écrêterait
et le total s'effondrerait dans le cas même où l'effet compte le plus. Désactivé
par défaut (`led_depth`), avec `led_depth_front` pour le câblage de profondeur que
personne n'a encore mesuré, et la suppression d'écriture surveille désormais aussi
la répartition. 360 cas en 31 suites.

### `led-fluid` : le fluide n'avait ni gravité ni volume (08-22)

Trois fautes, rapportées depuis le robot en une phrase — « les particules font
une fine ligne sur le bord, et c'est le bord opposé au sol ».

**Personne ne lisait l'IMU.** `M5.Imu.getImuData()` ne fait que CONVERTIR le
tampon brut du pilote ; c'est `M5.Imu.update()` qui lit le capteur, et le seul
appelant du dépôt était le Brain du companion. Un invité n'a pas de Brain : le
vecteur gravité restait donc figé à la valeur de naissance de la structure — pas
d'erreur, pas de zéros, juste un liquide qu'on aurait dit vu de dessus. La règle
a maintenant sa section dans le contrat invité, dans les deux langues.

**La pression était dans les mauvaises unités.** Les raideurs étaient celles de
l'article — un monde large de quelques unités — posées dans un monde large de
320 pixels, où elles valaient environ 13 px/s² face à une gravité de 900. Le
fluide n'avait aucun moyen de se tenir debout et tombait en une rangée de
pastilles. Toute longueur du solveur est désormais un multiple du pas et toute
raideur une accélération en px/s², comparable à la gravité qu'elle doit porter.
Le rayon d'interaction suit le GRAIN et non le pas de rendu : lié au pas, il
laissait le curseur de taille des pastilles retoucher la physique, et il faisait
croître le voisinage avec le curseur de particules jusqu'à ce que 400 grains
soient à `MAX_NEIGH` AU REPOS — le plafond de coût tronquait alors du fluide
ordinaire, qui bouillait au lieu de se poser.

**Deux grains ne partagent pas une pastille.** Le diamètre d'un grain est le pas
de la grille, et une passe de non-pénétration tient les centres écartés. Elle
s'exécute APRÈS les parois, ce qui fait toute la différence entre une contrainte
et une suggestion : avant elles, le clamp remettait chaque grain que la
projection avait poussé à travers le sol dans le voisin qu'il venait de quitter,
une fois par pas et pour toujours. La passe a sa propre grille de voisinage, à
sa propre échelle — en empruntant celle de la pression, elle dépensait toute sa
borne d'examen sur des grains qui ne se touchaient pas — et le nombre de grains
est plafonné par ce que le pas peut réellement tenir écarté (400 devient 153 à
20 px). Le splat est devenu bilinéaire au passage : un grain partage sa lumière
avec les quatre pastilles qui l'entourent, si bien qu'un déplacement entre deux
pastilles se fond au lieu de sauter, et que le corps du fluide gagne un bord
doux.

**Le mapping d'axes est mesuré, et les interrupteurs le disent.**
`gx = -accel.x`, `gy = +accel.y`, vérifié sur le robot debout — porté par la
baseline qui lit le vecteur et non par un défaut d'`imu_inv_x`, si bien qu'un
robot correct n'ouvre plus son paramétrage en annonçant qu'un axe a été inversé.
Un interrupteur doit vouloir dire *s'écarter de ce qui est juste*. Les trois sont
passés d'aucun panneau à l'**onglet RENDU** : ils ne doublent aucun autre widget,
et celui qui voit de quel côté part le fluide tient le robot, il ne lit pas
`/config` sur un portable. L'onglet caché ne garde que ce qui a déjà un widget —
teinte, saturation, luminosité.

Quatre tests natifs ont été ajoutés, tous vérifiés en échec sur le code qu'ils
épinglent : le fluide posé doit être un corps épais de plusieurs pastilles,
aucune paire ne doit descendre sous les trois quarts d'un diamètre, le nombre de
grains doit répondre ce qu'il peut tenir écarté, et toute rangée de l'onglet
RENDU doit être un interrupteur (un curseur posé là serait dessiné en
interrupteur et aplati à 0 ou 1 par le premier doigt). 353 cas en 30 suites.

### `led-fluid` : un quatrième bin invité, et deux pannes trouvées par le matériel (08-18)

**Ce que c'est.** Du liquide dans une boîte : un fluide à particules dont la
gravité EST l'inclinaison du robot, peint en grille de pastilles rondes — la
densité donne sa couleur à une pastille, la vitesse donne sa lumière. Secouer
éclabousse, taper repousse. Panneau de physique au swipe droite (viscosité,
gravité, rebond, traînée, cinq presets), rectangle teinte/saturation au swipe
gauche, et les douze WS2812 du K151 peuvent répéter le fluide. Documentation :
[`guests/LED-FLUID.md`](docs/guests/LED-FLUID.fr.md), spécification `ROADMAP §9`.

**Une table, quatre consommateurs.** Le panneau qui dessine un réglage, le
hit-test qui le trouve, le yaml qui le persiste et l'`addSetting()` qui le
publie sur `/config` lisent tous `ui::params()`. `check-guest-config.py` a été
écrit après qu'une clé lue par `loadConfig` et non écrite par `saveConfig` eut
été détruite à la première sauvegarde depuis la page web ; ce bin ne peut pas
avoir ce bug. Le portail cherchait des `addSetting("cle"` littéraux : y
inscrire ce bin l'aurait fait **passer en ne vérifiant rien** — le silence même
qu'il existe pour casser. Il sait désormais lire une table, et vérifie ce qui
reste faillible : les deux chaînes de `strcmp` écrites à la main qui relient la
table à l'état vivant.

**Le chien de garde, et pourquoi un rendez-vous doit être inconditionnel.** Le
bin bouclait au redémarrage sur le matériel à 20,5 s, `CPU 0: fluid-sim`.
`vTaskDelayUntil` ne bloque que tant que son échéance est devant : le premier
pas qui a débordé en est revenu aussitôt et, l'échéance continuant d'avancer
dans un passé déjà révolu, il n'a plus jamais bloqué. La tâche tournait à vide,
la tâche inactive de son cœur n'était plus élue, et le chien de garde a abattu
la puce. Un débordement resynchronise désormais l'échéance et sert un plancher
d'un tick.

**Une grille de voisinage n'est pas un plafond de coût.** Elle ne borne le
nombre de paires que tant que les particules sont étalées, et la gravité passe
sa vie à faire l'inverse — dès qu'un tas tient dans une seule cellule, toutes
les paires redeviennent voisines et le pas redevient quadratique. C'est cela qui
faisait déborder le pas. Les voisins par particule sont maintenant plafonnés, et
`test_fluid` tasse 400 particules dans un coin pour vérifier que le plafond
tient.

**Mesuré, pas estimé.** Au pas le plus fin (8 px, 1200 cellules), gravité
pilotée en cercle pour que le fluide ne se pose jamais : peinture pire
16-18 ms, boucle pire 26 ms avec une pointe occasionnelle à 38 ms venue de la
pile réseau, 50 frames peintes par seconde. L'estimation arithmétique de ~26 ms
pour la peinture, écrite dans la spécification, a été remplacée par le nombre.

**Douze LED bloquées en blanc, signalées depuis le robot.** Une WS2812 mémorise
sa dernière couleur et la garde à travers un redémarrage comme à travers un
reflash : trois moments distincts laissaient donc l'anneau allumé. Décocher
l'option d'écho se contentait d'arrêter d'écrire ; rendre la main au companion
laissait la dernière image brûler sur un robot dont le companion ne pilote ces
LED que si sa propre option est active ; et — le blanc lui-même — **sonder
l'expandeur au démarrage** configure la ligne de données avec un pull-up et
annonce douze LED, ce qui suffit à faire latcher du bruit à une chaîne jamais
écrite. Les trois passent désormais par la fonction unique qui parle à la RAM
couleur pour l'éteindre.

**Cinq défauts trouvés par une relecture indépendante, et aucun n'était visible
du dehors.** Ce qui écrit du code et ce qui le relit partagent leurs angles
morts : le changement a donc été relu selon quatre lentilles distinctes avant
d'atterrir. Les paramètres que le panneau modifie étaient un simple struct que
seul l'autre cœur lisait — rien dans la tâche de simulation ne l'écrit, donc un
compilateur avait le droit de sortir les lectures de la boucle infinie et un
curseur n'aurait jamais atteint le fluide, en silence et en build optimisé
seulement ; ils passent désormais par un compteur atomique de génération, et le
solveur annonce en série les valeurs qu'il adopte. Les coordonnées du tap
formaient une paire déchirable (la tâche teste X, donc se réveiller entre les
deux écritures éclaboussait au nouveau X et à l'ancien Y). Deux tampons ne
suffisaient pas : le producteur alterne, donc après deux bascules il écrit celui
que le peintre lit encore — ce n'est pas un cas limite, revenir d'un panneau
force une repeinte complète qui dure plus de deux périodes ; la frame est
maintenant copiée. Cette repeinte complète n'était pas bornée non plus, ~112 ms,
un tiers de seconde de tactile figé et de HTTP muet, désormais étalée sur
plusieurs frames par un budget de pastilles. Et le plafond de voisins n'était
chargé qu'à la particule extérieure de la paire : c'était donc un plafond sur
les PETITS INDICES — mesuré à 24 paires pour la particule 0 contre 59 pour la
59 dans un même tas ; l'indice n'ayant rien à voir avec la position, deux tas
identiques s'amortissaient différemment selon qui était numéroté en premier. Le
premier test écrit pour cette équité ne pouvait pas échouer (il mesurait le
budget consommé, borné par construction) ; il mesure la participation, et il
échoue bien sans le correctif.

**Partagé plutôt que dupliqué.** La charge utile des LED PY32 — l'arrondi
RGB565 et l'ordre petit-boutiste des douze entrées — passe dans
`firmware/common/Py32Leds.h`, inclus par le companion et par le bin ; seule la
transaction `Wire` reste de chaque côté, avec le verrou de bus là où il y a une
seconde tâche et sans lui là où il n'y en a pas. Le companion y gagne
l'adressage par LED : sa RAM couleur portait déjà douze entrées, seule l'API
prétendait le contraire.

Les tests natifs passent de 27 suites / 324 cas à **30 / 349** ;
`check-all.ps1` construit sept firmwares et vérifie A2.22 dans les sept
binaires.

### Quatre documents qui manquaient au corpus, et un portail pour que ses listes cessent de pourrir (08-17)

Trouvés en auditant le corpus plutôt qu'en le supposant.

**[`reference/API.fr.md`](docs/reference/API.fr.md).** Le firmware sert **44 couples
méthode+chemin** et la seule description complète était l'OpenAPI que le robot
génère depuis son propre code — apprendre l'API exigeait donc un robot allumé.
La page ne recopie pas ce document (une copie Markdown dériverait) : elle donne
les routes par famille et, surtout, les quatre conventions qu'une liste ne peut
pas transmettre — tout passe en paramètre de requête, les erreurs sont
bilingues et codées, tout ce qui est lourd est différé (**un `202` veut dire
accepté, jamais fait**), et l'ordre des routes est une contrainte réelle que le
routeur vérifie au démarrage.

**[`reference/EMOTIONS.fr.md`](docs/reference/EMOTIONS.fr.md).** Les 15 danses ont
leur table depuis longtemps ; les 30 expressions étaient nommées dans huit
documents et listées dans aucun, si bien qu'écrire une règle demandait d'ouvrir
`Emotions.h`. Deux comportements à connaître les accompagnent : un nom inconnu
est refusé par un `404`, jamais ramené en silence à `Normal`, et une émotion
explicite **interrompt une danse en cours** — sans quoi la keyframe suivante
l'écraserait une fraction de seconde plus tard.

**[`reference/SECURITY.fr.md`](docs/reference/SECURITY.fr.md).** Les morceaux
existaient dans trois fichiers ; rien ne disait en un seul endroit ce que le
robot expose **sans mot de passe** : la carte SD en lecture *et* en écriture, le
flash, la caméra, l'extinction. Le document ne prétend à aucun durcissement — il
n'y a pas de modèle de menace au-delà de « un LAN de confiance », et prétendre
le contraire serait pire que de le dire — mais il dit que le point d'accès de
repli est livré avec des identifiants connus, et que `cors=1` laisse n'importe
quelle page visitée appeler votre robot.

**[`CONTRIBUTING.fr.md`](CONTRIBUTING.fr.md).** Ce projet est tenu par huit
portails aux avis tranchés, et un nouveau venu les découvrait en les faisant
échouer. `CLAUDE.md` jouait ce rôle, mais il est écrit pour un agent et non pour
une personne.

**`check-doc-coverage.py`** tient les documents qui ÉNUMÈRENT ce que le code
définit : les 44 routes de `WebApi::route()`, les 30 valeurs d'`eEmotions`, et
les 15 danses de `dances::table()` — cette dernière table existait déjà et
n'était surveillée par rien. Les **deux sens** sont vérifiés, parce qu'ils se
trompent différemment : une ligne manquante cache une route, une ligne en trop
promet une API qui n'existe pas. Falsifié dans les deux sens et sur deux listes
avant d'être gardé. Les trois tables se sont révélées exactes du premier coup,
ce qui n'était pas acquis.

Chaque nouveau document est aussi lié **depuis là où la question se pose**, et
pas seulement depuis les cartes : le bloc `api:` de `CONFIG.fr.md` pointe
désormais vers la page de sécurité, les actions de règle de `PLUGINS.fr.md`
vers les deux catalogues de noms qu'elles acceptent, et la colonne `emotion`
d'une keyframe vers la liste où elle puise.

---

### La console nomme ce qui bouge la tête, et les docs gagnent une rubrique matérielle (08-17)

**Deux interrupteurs, une seule chose.** `Head-follow` et `Suivi du son (tête)`
se trouvaient côte à côte dans la console — les deux réglages qui font bouger
la tête toute seule — et aucun des deux ne disait ce qui la *pilotait*. Ils
nomment désormais leur déclencheur : **La tête suit les yeux** et **La tête se
tourne vers le bruit**. Deux fautes plus petites sont apparues au passage :
`Head-follow` ne portait aucun attribut `data-i18n`, c'était donc le seul
libellé de ce bloc à rester en anglais dans la console française ; et la
catégorie de tuning portait la même chaîne que l'interrupteur, pour un groupe
qui contient les seuils du suivi.

**[`docs/hardware/`](docs/hardware/README.fr.md), quatre paires EN/FR.**
L'essentiel du savoir durement acquis sur ce projet ne porte pas sur le code —
il porte sur une carte où deux bus logiques partagent une paire de fils, où une
butée de servo détruit le servo, et où le capteur de lumière ambiante est
derrière un mur. Ce savoir était dispersé dans les en-têtes de `hal/`, dans
`CLAUDE.md`, et dans trois sections de `CONVENTIONS.md` qui n'y avaient rien à
faire. La rubrique couvre l'inventaire complet des pièces avec leurs bus,
l'ordre de démarrage et pourquoi c'en *est* un, ce que coûte le partage de
chaque bus, chaque pièce avec son étalonnage et son **mode de panne**, et un
tableau de ce qui a été tenté sur cette carte et a **échoué**, chacun avec la
mesure qui a clos le dossier. Les trois sections mal placées — table des axes
IMU, topologie I2C, table des partitions — ont été **déplacées**, pas copiées.

Elle répond à une question qui revenait : **pourquoi ±130° de lacet alors que
la tête fait un tour complet à la main.** Rien de mécanique ne l'arrête.
`writeDeg` mappe une plage 0–300° sur un registre 10 bits et borne là ; avec le
centre à 166, l'enveloppe atteignable est +134/−166, soit ±134 symétrique,
moins quatre degrés de marge. Aller plus loin demande le mode multi-tours du
servo, pas une constante plus grande.

**[`reference/EYES.fr.md`](docs/reference/EYES.fr.md)** — le visage, d'une décision
à un pixel allumé : géométrie des yeux, chaîne d'animation et pourquoi son
étage de transformation ne porte aucune rampe, la paupière comme canal séparé
de l'échelle, transitions, clignement, roulette (dont le mode nuit, où `Normal`
tombe à zéro et `Sleepy` prend deux tiers des tirages), synchronisation des
LED, et l'arithmétique de la poussée par bandes sales — 20,5 ms de temps de fil
pur contre 3,5 ms de dessin.

**Une carte qui promettait ce qu'elle n'avait pas.** `docs/README.md` annonçait
que `WORKFLOWS.md` couvrait *le flash*, et aucune section de ce genre
n'existait. [`WORKFLOWS.fr.md §12`](docs/architecture/WORKFLOWS.fr.md) dessine
maintenant la chaîne qui échange le firmware exécuté — lancement, hall, arrêt
coopératif, retour — et nomme le piège de sa dernière flèche : revenir reflashe
le companion **depuis la carte SD**, si bien qu'une copie périmée écrase en
silence un firmware tout juste flashé par USB.

Également : l'éditeur de chorégraphies est annoncé **en tête** de
`CHOREGRAPHIES.fr.md` plutôt qu'enterré au §6 — écrire une danse à la main n'a
jamais été le chemin prévu ; `SPACE.fr.md` décrivait encore la série de
latitude lunaire à quatre termes, avec le signe que le code portait avant sa
correction ; et `sdcard/` a gagné l'entrée `space.yaml` qui manquait, tout en
perdant une clé fantôme `charge_led` et une commande de build citant un
environnement qui n'existe plus.

---

### Deux audits, et ce qu'ils ont trouvé (08-16)

**La latitude de la Lune était fausse au point de mal classer les éclipses.**
`moonInfo` portait quatre termes de la série de Meeus, soit une erreur de 0,12°
mesurée contre son propre exemple résolu (47.a) — et le classement par l'ombre
distingue totale de partielle sur une grandeur dont toute l'étendue vaut un
quart de degré. Jugée contre le canon NASA, la série livrée se trompait sur
**cinq des huit éclipses par l'ombre de 2023-2028** : trois manquées, une
partielle annoncée totale, une totale annoncée partielle. Le terme `2D − F`
portait en prime le signe opposé à celui de la table 47.B. À huit termes, les
huit sont justes. Le test censé couvrir cela épinglait la *mauvaise* réponse :
il acceptait « au moins partielle » pour une éclipse totale et le disait, en
s'excusant d'une série tronquée — le portail restait donc vert par-dessus le
bug. Il exige désormais le grade exact dans les deux sens, et un nouveau test
épingle la latitude elle-même contre la référence, là où le défaut vivait.

**Un seul classificateur de balayage au lieu de sept.** La même question — est-ce
un balayage, et dans quel sens ? — était résolue à sept endroits dans quatre
firmwares, avec cinq seuils et trois règles d'égalité. Un seuil n'est pas ici
une préférence mais un contrat avec le geste au-dessus : SceGuest possède le
glissement bas au-delà de 100 px, les gestes d'un bin vivent donc dans la bande
en dessous, et quand les deux nombres sont dans des fichiers différents personne
ne les compare. Cet écart, c'est l'endroit où un vrai glissement était lu comme
*un appui à l'origine du doigt*. `firmware/common/Gesture.h` tient les deux
distances côte à côte, une seule règle d'égalité (elle va au vertical, où vit la
navigation principale) et le budget d'appui ; une suite native exige que la
bande existe. Une exception subsiste, désormais exprimée plutôt que subie : les
balayages partant du panneau du radar gardent 40 px, la sortie y étant
désactivée et la bande n'existant donc pas.

**Une carte insérée après le démarrage n'était jamais vue.** Sur `space` et
`ha-remote`, le montage du boot valait pour la session entière : les réglages
cessaient de persister en silence pendant que `/config` proposait toujours de
les sauvegarder, et une carte retirée n'était jamais remarquée. Le radar avait
la sonde ; `firmware/common/SdWatch.h` la tient maintenant pour les trois, radar
compris — extraire un mécanisme en laissant l'original, c'est passer de trois
copies à quatre.

**Des écritures bloquantes sur les mauvaises tâches.** La trace de tuning était
un `Serial.printf` bloquant dans un callback AsyncTCP — quarante lignes sous le
commentaire qui l'interdit — et cinq traces de ha-remote s'exécutaient avec le
mutex que `netTask` attend. Une file différée dans `Trace.h`, vidée par
`loop()`.

**ha-remote rejoint les deux autres** sur la luminosité auto (livrée **à
l'arrêt**, parce que c'est nouveau sur un écran tenu en main), sur le
`loadCompanionLang` partagé, et sur la règle « pas de carte n'est pas un échec
d'écriture passager » — que son propre bloc roster, huit lignes plus bas,
appliquait déjà.

**De nouveaux portails, chacun falsifié avant d'être cru.**
`check-guest-config.py` tient un réglage d'invité aux quatre listes écrites à la
main où il doit figurer ; la quatrième est la dangereuse, puisque `saveConfig`
**tronque** et qu'une clé que le yaml sait relire mais que la sauvegarde n'écrit
pas est *détruite* à la première sauvegarde depuis `/config` — la panne
`auto_bright` du 08-05, dont la leçon était écrite en commentaire et jamais
outillée. `check-mirrors` a gagné la palette « Liquid Glass » dans ses trois
copies C++ (dont une annotée « synced BY HAND »), chaque plancher de tests
natifs cité, et la règle qu'un bin appelant `SD.begin` doit tenir un `SdWatch`.
`check-contrast` mesure désormais cette palette au lieu d'en croire trois
commentaires. Et la table A2.22 de `flight-radar-fire`, dont le commentaire
affirmait qu'elle était *dérivée*, était une liste à la main de trois entrées
contre six — elle est dérivée pour de bon, et le portail est passé de 87 points
épinglés à 97.

---

### Chaque firmware sait se raconter (08-15)

Une **trace debug** runtime existe désormais partout : `firmware/common/Trace.h`
porte l'implémentation unique (`sce::trace::log("tag", …)`, style printf,
conditionnée par un seul booléen — éteinte, chaque site coûte un test),
vendorée dans `SceGuest.h` comme sa quatrième copie tenue. Les étiquettes
nomment le sous-système : `net`, `cfg`, `http`, `sd`, `ui`, `task` ; les
lignes se lisent `[dbg][tag] +millis message` ; les secrets n'apparaissent
jamais — URL coupées à la query string, jetons rapportés présent/absent.

L'activation est à chaud, à dessein : les moments qui exigent une trace — un
robot qui ne joint pas, une chaîne de fetch qui échoue en usage — sont ceux
où un reflash est indisponible ou détruirait la preuve. Côté companion,
c'est la clé de tuning `debug` (persistée, appliquée dans la frame,
synchronisée avant `api.begin()` pour que le drapeau persisté raconte le
tout premier join WiFi), avec deux interrupteurs console — l'un à côté de la
télémétrie, l'autre dans Système. Côté invités, c'est une case **Debug**
cadre sur `/config`, rangée en NVS comme l'override réseau et pour la même
raison, appliquée immédiatement. Le contrat (`docs/guests/README.md`)
documente la case et l'appel `sce::trace::log` par lequel tout bin rejoint
la narration.

Les trois invités sont instrumentés de bout en bout — l'assistant HTTP
canonique de chaque bin trace chaque tentative avec hôte+chemin, code, durée
et taille, plus ses sorties muettes ; les chargements de config rapportent
clés reconnues et inconnues ; les écritures SD rapportent leurs échecs
silencieux et leurs succès ; les actions UI se nomment. Le companion raconte
sa séquence WiFi et chaque changement de tuning venu d'un client distant.

### L'overlay debug A+C arrive sur space, et la banque de boutons devient une (08-15)

L'overlay diagnostic plein écran tenu sous **A+C** n'existait que sur
flight-radar — l'enquête sur « le mode debug de space a disparu » a montré
qu'il n'y avait jamais existé, et pire : les trois FSM mono-bouton
indépendantes de space tiraient LES DEUX actions longues sur l'accord, et
n'avaient pas l'amorçage — un Fire démarré bouton tenu tirait l'action
longue de ce bouton sans qu'on lui demande.

La banque multi-boutons du radar (amorçage, anti-rebond, un-évènement-par-
appel, `chord()`) déménage dans **`firmware/common/ButtonFsm.h`** — une
implémentation, deux bins, plus de jumeaux — l'API du radar inchangée (ses
tests passent intacts). Space adopte la banque, gagne l'accord, et reçoit
son propre overlay : réseau, synchro d'horloge, âge et source du TLE,
diagnostic des lancements, passages, capteur de lumière, tas, uptime et
l'identité du build, rendu à l'instant où l'accord se relâche. Trois
nouveaux cas natifs épinglent les correctifs : un bouton tenu au boot reste
muet jusqu'au relâchement, un accord ne tire aucune action individuelle, et
les boutons reparlent après.

---

### Le schéma de la Lune dit la vérité, éclipses comprises (08-15)

Deux corrections à la figure Soleil-Terre-Lune, nées toutes deux d'une
lecture contre une planche de manuel.

**La Lune sur l'orbite est désormais à moitié éclairée, face au Soleil** —
exactement comme la Terre à côté d'elle. Le rendu précédent dessinait la
phase vue de la Terre (un disque gibbeux à 67 % posé sur l'orbite), raccord
avec le pourcentage imprimé à côté mais en liberté avec la seule chose que
la figure affirme : l'angle, et l'éclairage que cet angle impose.
L'apparence garde ses deux foyers, la pellicule de phases en bas et le grand
pourcentage ; leur moteur (`drawMoonDisc`, terminateur vertical) les couvre
déjà, si bien que la généralisation à terminateur incliné (`drawPhaseBody`)
a perdu son unique appelant et disparaît.

**Pendant une vraie éclipse ombrale, la Lune passe cuivre.** La version
naïve — assombrir la Lune quand elle est derrière la Terre — se
déclencherait à chaque pleine lune, à tort douze fois sur treize :
l'inclinaison orbitale de 5° qui rend les éclipses rares est précisément ce
qu'une figure vue de dessus ne peut pas montrer. Le vrai test utilise la
latitude écliptique que `moonInfo` calculait déjà et jetait : séparation
angulaire au point anti-solaire contre les rayons d'ombre de Meeus, totale
quand le disque entier tient dedans, partielle quand il ne fait que mordre.
La figure garde ses points d'appel (A2.22) : seules les couleurs changent de
main, et une ligne `eclipse` ouvre la colonne des faits tant que ça dure.
Validé nativement contre le canon : la totale du 2025-09-07 se lit totale,
la tout-juste-totale du 2026-03-03 se lit ombrale, et la pleine lune
suivante — éclairée à 98 % et intacte — se lit claire.

Les éclipses solaires ne sont volontairement pas signalées : c'est un couloir
étroit au sol, et une figure géocentrique qui en revendiquerait une pour cet
observateur se tromperait le plus souvent.

---

### La luminosité auto du radar se réaffirme (08-15)

Un enregistrement de réglages appliquait la luminosité manuelle sans
condition, et la boucle capteur ne re-corrigeait que si l'ambiante bougeait
de plus de 6 crans contre une valeur d'hystérésis périmée — sous auto,
l'écran restait au niveau manuel des minutes, voire des heures. Le correctif
que `space` portait déjà (un drapeau de réarmement levé par les deux chemins
d'application, consommé par la boucle capteur) manquait ici ; la luminosité
manuelle ne s'applique plus que si l'auto est coupée, et la source qui
gouverne se réaffirme en moins d'une seconde.

---

### Un seul arrêt coopératif au lieu de trois copies à la main (08-11)

La paire `netStop`/`netParked` qui gare la tâche réseau d'un invité avant un
reflash était écrite à la main dans les trois bins, et les copies avaient
divergé de quatre façons : la fenêtre d'ACK (légitime, propre à chaque bin),
l'avertissement quand la tâche ne se gare pas (deux sur trois), la remise à
zéro de l'ACK (deux sur trois) — et la reprise après un reflash RATÉ, que
seul le radar avait, sous forme d'un minuteur de 15 secondes : ha-remote et
space laissaient leur tâche garée *pour toujours* si le reflash échouait, un
invité à moitié mort, sans réseau.

`sce::CoopStop` dans `SceGuest.h` est désormais l'implémentation unique : le
bin déclare la garde, gare sa tâche avec `shouldPark()`, refuse les nouvelles
requêtes avec `stopping()`, règle sa fenêtre et confie la garde à
`guest.netGuard`. `SceGuest` la gare avant `updateFromFS` et **la libère sur
le chemin d'échec** — déterministe, là où le minuteur du radar devinait. La
fenêtre d'ACK par bin (3 s radar, 20 s ha-remote, 25 s space) reste la seule
variation légitime, chacune dimensionnée sur la plus longue requête isolée du
bin.

La récidive est la matière même de l'incident — une discipline appliquée à un
bin et silencieusement absente d'un autre — alors la règle est désormais
vérifiée, pas mémorisée : `check-mirrors.py` fait échouer tout `main.cpp`
d'invité qui appelle `xTaskCreate` sans brancher `guest.netGuard`, et la
section contrat de `docs/guests/README.md` enseigne la version en trois
lignes.

L'audit derrière la question « tout ce qui est lié à un bin est-il bien
déchargé à la sortie ? » se referme proprement : quitter un bin se termine
par `ESP.restart`, le déchargement ultime — la seule fenêtre qui compte est
stop→reboot, et dans celle-ci les invités détiennent de façon prouvée une
tâche chacun (désormais gardée), aucun minuteur et aucun descripteur de
fichier long-vécu, tandis que le côté companion mettait déjà tout au repos
systématiquement avant de flasher un invité (caméra inhibée et drainée,
renderer en pause, rail servo coupé, et A2.6 tenant par construction les
callbacks AsyncTCP loin de la SD).

---

### La route de retour vers le companion ne rampe plus (08-11)

`space` créait une tâche réseau et ne la garait jamais — le seul invité des
trois à qui manquait la discipline du radar. Pendant `/api/bins/stop`, cette
tâche continuait ses fetchs TLS et ses écritures de cache pendant
qu'`updateFromFS` lisait `/companion.bin` sur la carte SD et écrivait la
flash : deux prétendants sur un seul bus SD/SPI, et le reflash qui est la
seule route de retour vers le companion est passé d'environ 9 secondes à
**plus de dix minutes** — HTTP muet tout du long, ping vivant, indiscernable
d'un plantage vu du réseau.

Le correctif est l'arrêt coopératif du radar, appliqué à l'identique : un
drapeau `netStop` que la tâche acquitte en se garant (jamais `vTaskSuspend`,
qui peut figer un détenteur de mutex), une attente bornée dimensionnée sur la
plus longue requête isolée, et le même drapeau refusant toute nouvelle requête
dans `httpGet` pour qu'une chaîne de fetchs ne survive pas à la fenêtre.
Mesuré après coup : tâches garées en **141 ms**, 1,4 Mo reflashé en
**8,5 s**, companion répondant **14 s** après le stop.

`stopToCompanion` se raconte désormais sur le port série — arrêt des tâches,
durée de l'arrêt, taille du fichier, « HTTP muet jusqu'au redémarrage »
explicite — parce que cette fenêtre n'a pas d'autre narrateur : le serveur web
synchrone est mort par construction pendant le flash. Et `onBeforeStop`, qui
existait comme crochet que deux bins utilisaient et qu'un troisième oubliait,
est désormais une **exigence documentée** du contrat invité
(`docs/guests/README.md`) pour tout bin qui crée une tâche, règles de
dimensionnement comprises.

---

### Le robot dit quel build il exécute (08-10)

`GET /api/firmware` publie cinq valeurs, et la console les affiche dans
**Système › Firmware**, au-dessus du bouton de flash — l'état de ce qui tourne
a sa place avant le bouton qui le remplace.

| champ | ce qu'il tranche |
|---|---|
| `slot` | la partition OTA réellement démarrée (`app0`/`app1`) |
| `sha` | 8 premiers hex du sha256 de l'ELF — ce build précis |
| `console` | empreinte de `WebConsole.h`, celle qu'imprime `gen_console_gz.py --check` |
| `reset` | pourquoi la carte a démarré (`poweron`, `sw`, `panic`, `task_wdt`, `brownout`…) |

`slot` est le champ décisif. Un flash USB écrit **un** slot OTA et ne touche
jamais `otadata`, qui décide du slot démarré : un flash peut donc annoncer la
réussite, vérifier son propre hash, ramener le robot sur le WiFi, et laisser
tourner un autre firmware. Tous les signaux extérieurs disaient que la mise à
jour était passée ; seul un dump de la flash disait le contraire.

Les deux empreintes sont **reproductibles depuis une copie de travail**, ce qui
les rend utiles et pas seulement uniques : `sha` vaut `sha256sum firmware.elf`
(8 premiers hex) et `console` vaut ce qu'imprime `gen_console_gz.py --check`.
Une requête contre une commande répond à « ce robot exécute-t-il ma copie de
travail ? », sans dissection d'ELF ni dump de flash.
Re-gzipper les sources pour comparer donne au contraire un faux négatif — le
zlib du python système et celui de PlatformIO produisent des flux deflate
différents (tous deux valides) à partir de la même entrée.

Il n'y a délibérément pas de date de compilation. La source évidente,
`esp_ota_get_app_description()->date`, est la date de construction des
*bibliothèques Arduino précompilées* — elle répondait « Mar 5 2024 » sur un
firmware compilé quelques minutes plus tôt. Un champ qui a l'air faisant
autorité et qui est faux vaut moins que pas de champ du tout, et c'est
exactement la panne à laquelle ce travail met fin.

Un redémarrage en forme de plantage est un constat, pas une valeur : `panic`,
les trois chiens de garde et `brownout` s'affichent en couleur d'erreur. Les
cinq valeurs sont fixes pour tout le boot — d'où une route à part plutôt que
cinq champs de plus dans le `/api/status` que la console sonde toutes les deux
secondes, et une lecture unique au chargement de la page.

Le même bloc ouvre désormais la trace série, qui n'imprimait jusqu'ici que
l'énumération brute de la cause de reset. Les deux viennent d'un seul
`FirmwareInfo.h` ; rien ici n'est une information nouvelle, la puce la
détenait déjà, elle n'avait simplement aucune sortie — et le port série n'en
est pas une, puisque l'ouvrir en USB natif redémarre la carte et détruit la
preuve qu'on venait y chercher.

---

### Les deux CLAUDE.md sont tenus ensemble (08-10)

Le fichier existe à la racine et dans `.claude/`, et rien ne les faisait
concorder — ils étaient tenus identiques à la main, soit la définition même du
jumeau qui diverge. `check-mirrors.py` les compare maintenant en entier et
nomme la première ligne qui diffère.

---

### L'en-tête tient sur une seule ligne (08-10)

L'identité à gauche, les destinations à droite. C'était deux lignes, et depuis
que l'en-tête fait partie d'un bloc **épinglé**, chacun de ses pixels est
devenu permanent : la marque se lit une fois, les onglets servent toute la
journée, et les empiler faisait payer les deux à toute la session.

`align-items:flex-end` est ce qui le rend juste et pas seulement compact — il
pose le soulignement de l'onglet actif exactement sur la bordure basse de
l'en-tête au lieu de le laisser flotter au-dessus. Sous 720 px les deux ne
tiennent plus côte à côte : les onglets passent alors sur leur propre ligne et
prennent toute la largeur plutôt que d'être comprimés dans ce qu'il en reste.

---

### La télémétrie est épinglée, et son graphe a de la place (08-10)

Le graphe et les chips d'état ne défilent plus : l'en-tête et la bande de
télémétrie forment désormais **un seul bloc collant**. Deux `sticky` empilés
auraient exigé que le `top` du second égale la HAUTEUR du premier — un nombre
que rien ne mesure, et qui change avec la police, la langue et le fait que la
barre d'onglets passe à la ligne ou non. Un unique ancêtre collant ne contient
aucun nombre de ce genre.

Le graphe passe de 72 à **132 px**. À 72, les quatre courbes passaient
l'essentiel de leur course à deux ou trois pixels les unes des autres et la
forme d'un creux mémoire relevait de la devinette ; la hauteur est la seule
dimension qu'ait une courbe. Il redescend sur les écrans courts (96 px sous 820,
72 sous 640) pour qu'un bloc épinglé ne dévore pas un portable, et la bande
entière se replie toujours quand elle gêne.

Un défaut corrigé au passage : `drawChart` ne prenait que la LARGEUR de la boîte
CSS, donc le canvas gardait son bitmap de 56 pixels et une boîte plus haute se
contentait de l'étirer. Les deux dimensions suivent maintenant la boîte — sans
quoi « plus grand » aurait voulu dire « plus flou ».

---

### Deux contrôles se sont retrouvés, et le mode Son peut allumer son micro (08-10)

« *J'ai Head-follow dans l'onglet pilotage et Suivi du son (tête) dans l'onglet
options.* » Les deux sont ce qui fait bouger la tête toute seule ; le suivi du
son siégeait à côté du microphone parce qu'il en a besoin, mais `setTrack`
allume déjà le micro quand on l'active — l'adjacence n'achetait rien et la
séparation coûtait un onglet. Il vit maintenant dans la carte Tête, à côté de
Head-follow et de Servos.

« *Sur Bande de statut → Son, je n'ai pas la possibilité de réactiver le micro
ni de savoir comment faire.* » Le mode dessinait « micro au repos » et une
phrase renvoyant vers un autre onglet — une consigne d'aller ailleurs et de
revenir, pour le seul interrupteur sans lequel ce mode ne peut pas fonctionner.
Il dit désormais ce que le microphone fait réellement (éteint, en préchauffage
avec le compte à rebours, en attente parce que le haut-parleur tient le bus
audio, ou à l'écoute) et propose la seule action qui change cela. Quand le micro
écoute déjà, il n'y a rien à proposer : le bouton disparaît au lieu d'être grisé.

**Et les deux boutons des règles ont cessé de télescoper l'onglet Fichiers.**
L'un *était* un doublon — la liste de fichiers offre déjà `↻` sur
`rules.txt`. L'autre était pire qu'un doublon : « Rafraîchir la liste » existait
mot pour mot dans Fichiers, pour une autre liste, celle de la carte. Les deux
nomment maintenant leur objet : **↻ Relire rules.txt** et **↺ Actualiser ce
tableau**. Les garder là où est le tableau vaut mieux que d'envoyer quelqu'un
dans un autre onglet agir sur ce qu'il a sous les yeux.

Les titres de valeurs du guide devaient aussi ressembler à des titres :
`field`, `op` et `action` ouvrent chacun une liste et se lisaient comme des
puces de code égarées. Ils portent désormais le vocabulaire de titre du guide —
un filet au-dessus, la partie traduite en petites capitales — tandis que le NOM
de la colonne reste en code, puisque c'est ce qu'on tape.

---

### Le mode d'emploi devient un exemple annoté (08-10)

Enseigner neuf colonnes puis montrer un exemple demande au lecteur de retenir
neuf définitions avant que quoi que ce soit ait un sens. C'est désormais
l'inverse : une vraie ligne, tirée du fichier livré avec le robot, lue à voix
haute en une phrase, puis chacun de ses neuf jetons face à la colonne qu'il
remplit et à ce qu'il veut dire **dans cette ligne-là**. Le lecteur peut pointer
la chose qu'on lui explique.

Puis les valeurs, en listes plutôt qu'en prose : chaque champ que le robot
publie lui-même avec ce que son nombre signifie, les six opérateurs, et chaque
action avec ses arguments. C'est ce que demande « qu'est-ce que je peux mettre
là » — une liste qu'on parcourt, pas un paragraphe qu'on décortique.

---

### La section Règles, remise dans l'ordre de celui qui arrive (08-10)

« *C'est un peu le fouillis.* » Ça l'était : une ligne de format, un exemple
commenté, un tableau de huit colonnes et trois paragraphes de mises en garde —
tout cela vrai, rien de tout cela utile aux neuf visites sur dix où l'on vient
seulement voir si sa règle est chargée.

L'ordre suit donc désormais ce qu'on fait vraiment. Une ligne de contexte, les
deux boutons, **le tableau**. Le mode d'emploi est replié dessous pour la
dixième visite, et à l'intérieur la référence ne vient plus en premier non plus :
trois étapes numérotées dans l'ordre où on les exécute — choisir un champ,
écrire la ligne, la déposer sur la carte et recharger — puis les colonnes, puis
les trois choses à savoir. L'exemple commenté est à l'étape 2, là où il sert,
avec la phrase qui le lit à voix haute.

Le paragraphe d'introduction est la formulation de l'utilisateur, et elle vaut
mieux que la mienne pour une raison qui mérite d'être retenue : trois phrases
courtes au lieu d'une phrase composée, et elle commence par OÙ est le fichier —
le lecteur sait ce qu'il regarde avant qu'on lui explique pourquoi ça existe.
« Poster » une action est devenu « déclencher » : poster est notre vocabulaire,
pas le sien.

Un défaut de ma main, attrapé sur la capture : le guide replié est un `details`
dans un `details`, donc son contenu héritait de la carte de verre de celui du
dessus. Du verre sur du verre se lit comme un bug d'affichage. Je l'avais
neutralisé pour le titre et oublié pour le corps.

---

### Le tableau des règles nomme ses propres colonnes (08-10)

Le guide parlait de `sustainMs` ; le tableau disait **TENUE** ; rien à l'écran
ne reliait les deux. Et le guide français avait traduit `field` en *champ* : une
même chose portait donc trois noms entre le tableau, le guide et le fichier que
le lecteur est censé taper.

L'en-tête a deux étages désormais. Le premier dit à quoi sert la colonne et se
traduit ; le second est le NOM du champ exactement tel qu'il apparaît dans
`rules.txt`, en code, et ne se traduit **jamais**. `Condition` couvre ses trois
— `field`, `op`, `value` — parce que c'est de cela qu'une condition est faite,
et les séparer est ce qui permet au guide de nommer chacun.

Le guide a été refait autour de ça. Il est indexé par les noms de colonnes :
qui se demande ce que veut dire *TENUE* trouve `sustainMs` juste au-dessus et
`sustainMs` de nouveau dans le guide. Il gagne un exemple lu en toutes lettres
— *tant que `claude` est actif, si `ctx` atteint 90 pendant trois secondes…* —
et une ligne pour `a1`/`a2`, qui étaient mentionnés dans la ligne de format et
expliqués nulle part.

---

### Sept défauts trouvés par l'audit, cinq de ma main (08-10)

Une comparaison des trois bins invités entre eux, et de chaque script
PowerShell avec son jumeau Linux. Les deux ont rapporté des choses vraies
plutôt que des choses qui avaient l'air fausses.

**Les deux qui étaient des bugs sur le robot.** La sauvegarde de config de
`flight-radar` annonçait le succès sans condition : `SD.open(FILE_WRITE)` tronque
le fichier, donc sur une carte pleine ou protégée en écriture `/config` disait
« Enregistré » par-dessus des réglages qui n'existaient plus. `ha-remote` l'a
payé exactement en juillet, et lui comme `space` vérifient `getWriteError()`
depuis — ce bin n'a jamais reçu le correctif. Et `space` gardait
`SCE_WIFI_SSID` et `SCE_WIFI_PASS` derrière un **seul** `#ifndef`, ce qui est le
piège que `SdPins.h` interdit trois lignes plus haut : un profil ne fournissant
que le mot de passe se le voit silencieusement redéfini à `""`.

**Et cinq dans les scripts Linux, quatre écrits pendant cette session.**

`claude-statusline.sh` prenait les **400 derniers kilo-octets** du transcript là
où son jumeau prend les **400 dernières lignes**. Une seule entrée portant un
résultat d'outil dépasse 100 ko, donc la fenêtre pouvait contenir quatre lignes
contre quatre cents pour le jumeau ; n'y trouvant aucun usage, il poussait
`claude=0` et toutes les règles livrées s'éteignaient — sous Linux seulement,
ce qui est la pire sorte de différence à remarquer. La fenêtre croît maintenant
de 256 ko à 8 Mo jusqu'à en trouver un. Le même script écrasait les espaces en
soulignés pour survivre à `read`, ce qui transformait un répertoire nommé
`mon_projet` en `mon projet` ; c'est séparé par tabulations désormais, et rien
n'est réécrit. Sa jauge de coût tronquait là où le jumeau arrondit : 0,126 $
affichait 2 ici et 3 là-bas.

`test-mag.sh` exigeait que `mag_x`, `mag_y` et `mag_z` soient **adjacents et
dans cet ordre** dans le JSON. Réordonnez une ligne de firmware — un changement
que personne ne relierait à ceci — et il capture zéro échantillon pendant que le
testeur PowerShell continue de marcher : les deux se contredisent alors sur un
capteur qui va bien. Et son chemin d'entrée vide émettait treize champs là où
les deux lecteurs en nomment douze.

`endurance-log.sh` rouvrait le périphérique série **à chaque ligne** : tout ce
qui arrivait entre la fermeture et l'ouverture suivante était perdu, sans
marqueur, dans une capture dont le but même est de remarquer ce qui s'est passé
la nuit. Une seule ouverture pour le run désormais, comme le jumeau.

`statusbar-push.ps1` analysait les nombres avec `[double]::TryParse`, qui suit
la culture courante. Sur la machine fr-FR où vit ce dépôt, « 62.5 » n'est pas un
nombre : chaque valeur décimale de jauge partait suffixée `_s` et poussée en
**texte**, et la jauge restait à zéro sans le dire. Culture invariante
maintenant — vérifié contre le réglage réel plutôt que supposé.

`check-all.ps1` lisait la **première** occurrence de `test cases:` et exigeait
« succeeded » sur la même ligne, là où le jumeau bash lit la dernière sans cette
exigence. Les deux alimentent le même `MIN_TESTS`, que `check-mirrors.py`
s'emploie à tenir synchronisé — le mesurer différemment sur les deux plateformes
vide ce miroir de son sens.

---

### Trois défauts de ma main dans le lot règles (08-10)

Trouvés en relisant ce que je venais d'écrire, et chacun vérifié sur cible
plutôt que discuté.

**Le curseur de la réponse chunkée appartenait au serveur, pas à la réponse.**
Membre de `WebApi`, il était partagé par tous les clients : deux navigateurs
interrogeant en même temps — une console qui sonde et un `curl` suffisent —
faisaient avancer le même compteur, et chacun recevait une demi-liste entrelacée
dans un JSON qu'aucun ne pouvait analyser. C'est désormais un `shared_ptr`
capturé par la lambda : il vit et meurt avec la réponse. Six requêtes
simultanées rendent maintenant six documents complets et identiques.

**Une description pouvait faire passer un caractère de contrôle dans le JSON.**
Une tabulation à l'intérieur d'un commentaire survit à l'analyseur qui l'a
capturée, et un caractère de contrôle brut n'est pas du JSON valide : le
document entier devient inanalysable et le symptôme est un tableau de règles
VIDE, qui n'atterrit nulle part près de sa cause. Replié en espace, à côté du
guillemet et de l'antislash déjà échappés. Vérifié avec un fichier contenant les
trois.

**Une règle qui échouait à l'analyse léguait son commentaire à la suivante.** Le
commentaire en attente n'était effacé qu'en cas de succès, donc une faute de
frappe produisait une règle fonctionnelle décrite par la phrase de quelqu'un
d'autre — pire que pas de description du tout. Il est maintenant consommé par
toute ligne qui n'est ni vide ni un commentaire, qu'elle ait été analysée ou non.

---

### Chaque colonne de règle se réduit à son contenu, la description prend le reste (08-10)

Six colonnes au vocabulaire court et fixe — `INTÉGRÉE`, `ctx ge 75`, `10s`,
`600s`, `SetEmotion Worried 4000` — se partageaient la largeur à égalité avec la
seule colonne faite de phrases, ce qui laissait à la description trois mots par
ligne. Elles se réduisent désormais à leur contenu et la description absorbe
tout ce qui reste.

Écrit « toutes les cellules, puis la dernière en arrière » plutôt qu'en liste de
`nth-child`, pour qu'une septième colonne ajoutée plus tard ne puisse pas s'y
soustraire en silence. Et la table doit déclarer `table-layout:auto` : la règle
globale `table{}` impose `fixed` pour les tables de réglages, et sous `fixed` un
`width:1px` est obéi au pied de la lettre — toutes les colonnes s'écrasaient à un
pixel et les lignes se chevauchaient en bouillie.

---

### Une règle porte le commentaire écrit au-dessus d'elle (08-10)

Le format n'a pas de champ description, et en ajouter un casserait tous les
`rules.txt` déjà écrits. Mais un humain met de toute façon l'explication sur la
ligne du dessus — le modèle livré le fait pour ses quatre règles — donc **cette
ligne EST la description**. Cela ne coûte rien au fichier et la console y gagne
une colonne.

Les lignes de commentaire consécutives s'accumulent en une seule description,
parce qu'une phrase repliée sur deux lignes reste une phrase : n'en garder que
la dernière donnait des règles décrites par « *mid-answer does not read as
celebration* », ce qui se lit comme un analyseur cassé plutôt que comme un
retour à la ligne. Un `#` seul est la coupure de paragraphe, et c'est elle qui
empêche l'en-tête de cinquante lignes du fichier de venir se coller à la
première règle en dessous. Au-delà de quatre-vingt-seize caractères, la coupe se
fait **sur un mot**, avec des points de suspension, et l'internement se fait
**en dernier** : l'arène est finie, et une règle qui a perdu sa description
fonctionne encore là où une règle qui a perdu son nom de champ ne fonctionne
plus.

**Le texte d'aide de la section a été refait pour qu'on écrive AVEC.** C'était
un paragraphe qui résumait le format ; c'est maintenant le format lui-même : la
forme de la ligne en code, puis chaque colonne avec ce à quoi elle sert, puis
les deux choses qui piègent réellement — un champ inconnu vaut **0**, donc
`ctx lt 20` est vraie quand rien ne publie `ctx` (et c'est à cela que sert la
grille `enable`), et une ligne de plus de 127 octets est jetée en entier, en
silence.

Deux défauts de ma main en chemin, tous deux attrapés par quelque chose et non
par chance. L'écriture chunkée de `/api/rules` copiait la **valeur de retour**
de `snprintf` — la longueur qu'il *aurait* écrite — depuis un tampon de
224 octets que la description avait dépassé ; la lecture sortait de la pile et
le serveur ne répondait plus du tout. Dimensionné pour le pire cas et borné à ce
qui est réellement dans le tampon, d'abord et toujours. Et en régénérant le
miroir, un fichier a été ouvert en écriture **avant** d'être lu : la lecture n'a
rien rendu et rien n'a été écrit. Le portail miroir a signalé une copie de
0 octet à l'exécution suivante, et la copie est désormais régénérée depuis le
littéral du firmware plutôt que retapée à côté de lui.

---

### Les règles s'expliquent, arrivent vivantes, et se regardent (08-10)

Trois choses étaient vraies à la fois du moteur de règles : le fichier ne disait
pas ce qu'était une règle, les exemples livrés étaient tous commentés, et rien
nulle part ne permettait de voir ce qui avait réellement été chargé.

**`rules.txt` enseigne désormais son propre format.** Le modèle que le firmware
écrit sur une carte qui n'en a pas porte la liste des champs, les opérateurs, la
sémantique du front tenu, et les deux propriétés qui rendent une règle
confiable — commandes autorisées seulement, et les réflexes au-dessus. Il énonce
aussi la limite de 127 octets par ligne, parce que `RuleStore::load` lit dans un
`char line[128]` et **jette** une ligne trop longue en entier : un commentaire
qui déborde disparaît du fichier même qu'on lit pour apprendre le format.

**Quatre règles VIVANTES pour la jauge de contexte Claude**, pas des exemples
commentés : avertissement à 75 %, alarme à 90 %, secouement de tête à 97 %, et
contentement sous 20 %. Toutes quatre sont grillées sur `claude`, que seul le
pont statusline publie : elles restent inertes tant qu'il n'est pas installé et
ne peuvent pas se déclencher sur un robot qui n'a jamais entendu parler de
Claude.

**Le pont publie désormais ce qu'elles observent.** `claude-statusline.ps1` et
son jumeau `.sh` dérivent `ctx` — la part de la fenêtre de contexte utilisée —
du transcript de session, puisque le JSON du statusline ne porte aucun champ de
ce genre et que son schéma n'est pas documenté. Le dénominateur résout
`$CLAUDE_CTX_WINDOW`, puis `autoCompactWindow`, puis 200 000, et il est énoncé
plutôt que supposé : sur un modèle à fenêtre de 1 M, le défaut affiche 100 % en
permanence, ce qui est une variable à poser et non un bug à chercher. Il met
`claude` à 1 **uniquement** quand `ctx` a pu être calculé — une règle comme
`ctx lt 20` est VRAIE sur un champ absent, donc un pont qui s'annoncerait sans
le nombre déclencherait joyeusement la règle du contexte vide.

**`GET /api/rules` dit ce qui est chargé**, ce qui n'est pas le contenu du
fichier : une ligne qui ne s'analyse pas est simplement absente, sans erreur ni
log, et le seul moyen de s'en apercevoir était de regarder le robot ne pas
réagir. Par règle il rapporte la grille, la condition, les temporisations,
l'action rendue, si elle vient de la carte, et si sa grille est ouverte à
l'instant — répondu par le moteur lui-même, car une console qui le calculerait
depuis sa propre copie du champ finirait par ne plus être d'accord sur les
règles vivantes.

**La console en fait un tableau, dans une section à elle.** Les règles étaient
le troisième bloc du panneau bande de statut, là où elles ont le moins leur
place : une règle observe un champ et poste une commande, et la bande est une
des choses qu'elle peut finir par changer, pas son domicile. Les grilles fermées
sont grisées et barrées plutôt que masquées — « ma règle ne fait rien » se
répond bien plus vite en la voyant là, grisée, que par son absence.

Et les deux copies du modèle — le littéral du firmware et
`sdcard/stackchan-companion/rules.txt.example` — sont désormais tenues par
`check-mirrors.py`. L'ancienne copie avait déjà dérivé : elle documentait
`touch_head`, `approval` et `decision`, trois champs que rien dans ce dépôt ne
publie.

---

### Le bloc Règles dit ce qu'est une règle (08-10)

`↻ Recharger règles SD` et le mot `rules.txt` à côté disaient comment recharger
quelque chose que le panneau n'expliquait nulle part. Une note courte dit
maintenant ce qu'est une règle — *un champ, une comparaison, une action*, avec
`batt lt 15 → SetEmotion Worried` pour toute forme —, d'où viennent les champs
(le robot lui-même, ou tout ce qui en pousse un par `/api/field`), et les deux
faits qui rendent leur usage sûr : une règle ne peut poster que des commandes
autorisées, et les réflexes restent au-dessus d'elle.

Elle indique aussi où le fichier se modifie, ce que le panneau n'avait jamais
mentionné : **l'onglet Fichiers**, puis rechargement ici, sans redémarrage.

---

### La notification prend un bloc à elle (08-10)

Elle siégeait sous **Mode**, la taille de texte et la vitesse de défilement à
côté d'elle, et cela disait deux fois la mauvaise chose. Aucune des trois ne
dépend du mode : une notification INTERROMPT ce que la bande montrait, quel que
soit ce mode, et elle défile à sa propre taille et à sa propre vitesse. Lues
comme les réglages d'un mode, ce sont elles qui font changer de mode pour
changer la taille d'un message.

Le panneau bande de statut est désormais trois blocs, séparés comme ceux de
Système :

- **Notification** — le champ, le bouton *Dire*, les deux curseurs sur une seule
  ligne parce qu'ils se lisent comme une paire, et une ligne qui dit ce qu'ils
  gouvernent vraiment (toute notification, alerte ou « say », quoi que la bande
  montre en dessous — et une alerte est dessinée en taille 2 minimum quoi qu'en
  dise le curseur, parce qu'elle est faite pour être lue).
- **Mode** — le sélecteur et, dessous, seulement ce que le mode COURANT sait
  utiliser.
- **Règles · carte SD** — ni la notification ni un mode : le fichier qui
  transforme un *champ* en émotion ou en danse, quoi que la bande affiche.

Passer en pleine largeur a eu un coût qu'il valait la peine d'attraper : les
trois champs de pourcentage du mode Jauges et son bouton *Pousser* s'étiraient
sur tout le panneau, et un bouton aussi large que son conteneur se lit comme
l'action du conteneur plutôt que celle de ce mode. Borné à 280 px.

---

### Quatre onglets, et les pastilles d'icônes rejoignent la ligne de télémétrie (08-10)

**Pilotage et Bandeau n'en font plus qu'un.** Ils étaient séparés par SUJET — le
corps du robot d'un côté, sa bande de statut de l'autre — et ce n'est pas ainsi
que la page s'utilise : piloter le robot et choisir ce qu'il affiche, c'est la
même séance. Quatre destinations désormais, et celle où l'on arrive contient
tout ce qu'on touche pour faire arriver quelque chose. Un `band` mémorisé
retombe sur `pilot` au lieu d'ouvrir une page vide, parce que `tab()` valide
contre la liste avant de toucher à quoi que ce soit.

**Les pastilles d'icônes passent sur la ligne de télémétrie, plaquées à
droite.** Elles choisissent ce qu'affiche la propre rangée de statut du robot —
la même question que celle à laquelle la bande répond sur son état — et elles
étaient la queue du panneau bande de statut, à trois défilements des chips
qu'elles reflètent. Plaquées par `margin-left:auto`, relâché sous 760 px : sur
une fenêtre étroite les pastilles passent à la ligne, et y être poussées à
droite se lirait comme une troisième colonne qui n'existe pas.

Une chaîne périmée s'est révélée sur la capture d'écran et non dans un test : la
note pomodoro française renvoyait encore le lecteur vers « l'onglet Tuning »
pour trois curseurs situés juste au-dessus — une mise en page vieille de deux
refontes. Corrigée, ainsi que le commentaire de code qui la répétait.

---

### Le robot devient le périphérique d'entrée de l'éditeur de chorégraphies (08-10)

Deux curseurs disent mal « comme ça ». **Relâchée, la tête du robot se pose à la
main et il rapporte les angles dans lesquels on l'a mise** — et le firmware
avait déjà tout le mécanisme : `GET /api/servo/pos` rapporte une pose mesurée,
et ne la rapporte que servos coupés, parce que le bus SCS0009 est en écriture
seule en fonctionnement et qu'une lecture glissée entre deux `WritePos` rend les
servos muets. Les relâcher n'y est pas un confort : c'est la condition pour les
mesurer.

Il manquait une chose de chaque côté.

**Sur le robot : `cors`, éteint par défaut.** L'éditeur est une page `file://`
locale, donc chacune de ses requêtes est cross-origin et le navigateur jette la
réponse sauf si le robot l'autorise. `Access-Control-Allow-Origin: *` est un
vrai élargissement — tant que c'est actif, n'importe quelle page affichée par le
navigateur peut parler au robot sur le réseau local, et Basic Auth coupé, cela
inclut le faire bouger — c'est donc un interrupteur qu'on bascule, pas un défaut
dont on hérite. Lu une fois au boot, parce que la liste d'en-têtes est globale au
serveur et en ajout seul : on ne l'enlève pas ensuite, la décision appartient au
boot qui a lu la config. Vérifié sur cible : absent par défaut, présent après
`cors=1` et un redémarrage.

**Dans l'éditeur : un panneau Capture.** IP, un interrupteur qui relâche les
servos et interroge la pose à 5 Hz, un affichage en direct, ⤓ Capturer dans la
keyframe, et une case *suivre en direct* qui écrit chaque mouvement dans la
keyframe sélectionnée pendant qu'on la pose. L'éteindre remet les servos **comme
ils étaient**, pas comme l'outil suppose qu'ils étaient — sans quoi un robot dont
le propriétaire tourne servos coupés se serait vu rendre une tâche servo qu'il
n'avait jamais. Fermer l'onglet les remet aussi, par `sendBeacon`.

Trois détails décident si c'est utilisable. Les angles reviennent en degrés servo
bruts et l'éditeur parle en offsets : les deux conversions sont une soustraction
(yaw − 166, pitch − 93) et les deux sens s'accordaient déjà avec les curseurs. Ce
qui est capturé est borné aux limites de l'éditeur — la tête se pose un peu
au-delà de ce qu'une chorégraphie peut demander, et un CSV que le robot rogne en
silence est exactement la panne que cet outil existe pour empêcher. Et une
requête cross-origin bloquée remonte en `TypeError` nu, sans statut ni corps :
rapporté tel quel, cela se lit « le robot est éteint », et on redémarre un robot
qui répondait très bien. Elle est nommée pour ce qu'elle est, avec
l'interrupteur à basculer et le redémarrage qu'il demande.

---

### La télémétrie devient une bande jumbo, et Options devient trois cartes (08-10)

Trois remarques sur la nouvelle console, et chacune nommait quelque chose qui
n'allait toujours pas.

**La télémétrie en direct va d'un bord à l'autre**, directement sous l'en-tête,
comme sa continuation plutôt que comme le premier panneau de l'onglet où l'on
se trouve — ce qu'elle n'est justement pas : elle n'appartient à aucun et est
vraie pour tous. Il a fallu neutraliser les règles génériques `details`, qui
peignent une carte de verre sur chaque `summary` et chaque enfant direct, et qui
avaient remis une boîte arrondie de 1020 px à l'intérieur d'un fond pleine
largeur — le pire des deux, une carte qui aurait perdu sa bordure. Contenu
compris : le graphe EST la bande, moins les 20 px qui gardent le texte à
distance du bord.

**Les deux interrupteurs qui PARLENT de télémétrie l'ont rejointe** : *Infos
debug (émotion·IP)*, qui la publie sur le bandeau du robot, et *Télémétrie
série*, qui la publie sur le fil. Ni l'un ni l'autre n'était une propriété de la
bande de statut ni des options où ils siégeaient.

**L'onglet Options, c'est trois cartes, disposées comme la rangée de Pilotage.**
La grille auto-fit tassait ses groupes selon la place disponible, si bien qu'un
jeu de quatre groupes se repliait l'un sous l'autre et que les titres de
colonnes cessaient de s'aligner — la seule chose pour laquelle un titre de
colonne existe. Diagnostic parti dans la bande, il en reste exactement trois, et
`.cgrid` est la rangée qu'utilisent déjà Émotions/Danses/Tête : une disposition
de moins à tenir d'accord avec elle-même. Son titre « Options » disparaît aussi :
l'onglet s'appelle déjà ainsi, et un panneau qui répétait le mot ne disait rien
deux fois.

Un onglet a également cessé d'être une carte : `.tab` est un `section` parce que
c'est l'élément honnête pour ça, et `section{}` dessinait un second cadre autour
de panneaux qui avaient déjà le leur.

---

### La console prend une forme : cinq onglets au-dessus d'un bloc vivant (08-10)

Même palette, même verre, même aurore — pas une couleur déplacée. Ce qui change,
c'est que la page a cessé d'être un seul défilement.

**Cinq onglets.** Huit panneaux empilés bout à bout, cela voulait dire passer
devant la caméra et le gestionnaire de fichiers à chaque fois qu'on voulait
atteindre les tables de réglage, et que la section Système — la plus longue —
était la plus loin. Pilotage (émotions, danses, tête), Bandeau, Options,
Fichiers, Système. L'onglet vit dans le hash de l'URL : `/#sys` est un lien et
un rechargement retombe où on était ; le repli est `localStorage`, parce que le
hash est perdu dès que quoi que ce soit navigue. Ni l'un ni l'autre ne suffit :
le hash se partage mais il est fragile, le stockage dure mais reste privé à un
navigateur.

**La télémétrie a quitté l'en-tête collant.** Le graphe et les chips d'état
faisaient environ 180 px de chrome permanent — un cinquième d'un écran de
portable, épinglé pendant qu'on déplace un curseur. Ils sont maintenant dans la
page, au-dessus des onglets et partagés par tous, dans un bloc qui se replie et
s'en souvient. L'en-tête, c'est la marque et les onglets, et il s'aligne enfin
sur le contenu : il se calait à 20 px du bord de la fenêtre pendant que `main`
se centrait à 960, si bien que sur un écran large le logo se retrouvait loin à
gauche de tout ce qu'il introduisait. Une variable `--wrap`, les deux côtés.

**Les options sont groupées par ce sur quoi elles agissent** — Écran, LEDs, Son,
Diagnostic — au lieu de neuf interrupteurs dans une colonne qu'on lit jusqu'au
bout à chaque fois. « Le micro est-il allumé » se répond désormais en regardant
un seul endroit. Les réglages fins les rejoignent, repliés : c'est la même
question posée à soixante-dix nombres plutôt qu'à dix interrupteurs, pas un
chapitre à part.

**ISO-FONCTIONNEL.** Pas un contrôle ajouté, retiré ni recâblé — chaque id,
chaque gestionnaire, chaque `data-i18n` survit, et `check-console.py` atteint
toujours les 71 clés de tuning. Vérifié en rendant les cinq onglets en headless
et en les regardant : cette passe a attrapé deux vrais défauts, un panneau
Bandeau resté aux deux tiers de la largeur dans la grille que son voisin avait
quittée, et un en-tête `Horloge · NTP` dont la pastille faisait lire « NTP NTP ».
Les règles `.rowflex` de cette grille sont supprimées plutôt que laissées
derrière.

---

### Les scripts gagnent des jumeaux Linux, et les deux faits qu'ils partagent sont tenus (08-10)

Sept `.sh` à côté de leurs `.ps1`, avec les mêmes arguments : `check-all.sh`,
`gates/test-native.sh`, et dans `dev/` `find-port.sh`, `test-mag.sh`,
`endurance-log.sh`, `statusbar-push.sh`, `claude-statusline.sh`. Les portails
eux-mêmes sont en Python et ont toujours été portables ; ce qu'il fallait
porter, c'est le lanceur autour.

**Les jumeaux qui comptent sont enregistrés.** `check-all.sh` redit les deux
seuils qui donnent son sens au portail des tests natifs (26 suites, 306 cas) et
`find-port.sh` redit l'identité USB de chaque carte. Ce sont des copies à la
main, donc elles dérivent de la même façon — une suite ajoutée sous Windows et
pas relevée sous Linux donne un run Linux qui passe avec *moins de tests qu'il
ne devrait*, et une carte ajoutée à une table reste inconnue de l'autre pendant
que le script continue de répondre « ABSENT » comme si c'était vrai. Six
nouvelles entrées dans `check-mirrors.py` les tiennent, ce que ce projet fait de
toute copie qu'il a décidé de garder.

Trois choses que le portage ne pouvait pas copier :

- **`find-port.sh` lit sysfs, pas WMI.** `/dev/ttyACM0` n'est pas plus une
  identité que `COM6` : la carte est résolue en remontant du tty vers le
  périphérique USB qui le possède, puis en comparant VID:PID. Ni `udevadm`, ni
  Python — une dépendance de moins, et ça marche dans un conteneur. Le cas
  Windows « pilote non installé » devient son équivalent Linux : le nœud existe
  et on ne peut pas l'ouvrir, donc le script nomme le groupe `dialout` plutôt
  que de laisser une erreur de permission remonter depuis esptool.
- **Aucun encodage pourcent fait main.** `statusbar-push.sh` construit sa requête
  avec le `--data-urlencode` de curl (`--get` le met dans l'URL, `-X POST` garde
  la méthode), parce qu'un libellé avec une espace, un accent ou un `&` est
  exactement ce qu'un échappement maison rate, et que la panne est un champ
  tronqué sur le robot sans rien dans aucun log. Vérifié contre le robot : un
  `say` avec une esperluette, trois jauges, un changement de mode et le retour.
- **`.gitattributes` épingle `*.sh` en LF.** `core.autocrlf` est vrai ici, donc
  sans cela un clone Linux reçoit `#!/usr/bin/env bash\r` et le noyau refuse
  avec « *bad interpreter: no such file or directory* » — un message qui nomme
  l'interpréteur, pas le retour chariot, et qui se lit donc comme un bash
  absent. Des fichiers qui existent pour qu'un utilisateur Linux ait quelque
  chose qui tourne seraient partis inexécutables.

---

### `scripts/` et `tools/` prennent une forme, et elle énonce une règle (08-10)

Quinze fichiers à plat dans `scripts/`, six dans `tools/`, avec les portails,
les corvées de session et une étape de build dans le même tas. Triés par **qui
les exécute**, la question qu'on se pose vraiment en ouvrant le répertoire :

```
scripts/  check-all.ps1   le point d'entrée unique
          gates/          ce que check-all lance, et RIEN d'autre
          dev/            ce qu'on lance à la main, contre une carte
          build/          ce que PlatformIO lance tout seul (hook pre:)
tools/    choregraphies/  une application : l'éditeur de danses
          generators/     écrivent un fichier que le firmware ou la SD consomme
          probes/         interrogent un service externe, pour analyser ce qu'il
                          répond vraiment plutôt que sa documentation
```

La règle de `gates/` est tout l'enjeu : un fichier là-dedans est un fichier que
le portail lance. Deux résidents sont nommés comme exceptions dans
`scripts/README.md` plutôt que laissés à ressembler à des oublis —
`check-comments-only.py` (outil de passe qui sort en 1 sur tout vrai commit ; il
a été dans la liste une fois et faisait échouer le portail dès sa première
exécution) et `test-native.ps1` (qui lance les suites seules, là où `check-all`
lance `pio test` lui-même pour pouvoir affirmer les NOMBRES de suites et de cas).

**Le piège était le calcul de la racine.** Chaque contrôle trouvait le dépôt par
`dirname(dirname(__file__))`, une hypothèse sur sa propre profondeur ; un niveau
plus bas, les six résolvaient vers `scripts/` et auraient rapporté un dépôt plein
de fichiers manquants. Ils remontent maintenant d'un cran de plus ET affirment
que `platformio.ini` est là, parce que cette panne-là ne plante pas : elle fait
manquer tous les chemins, et la sortie ressemble à un dépôt cassé plutôt qu'à un
script déplacé.

Soixante-deux fichiers portaient une référence : le hook de build de
`platformio.ini`, le lanceur de portails, les deux `CLAUDE.md`, les deux
`README`, dix documents, et l'en-tête « comment me lancer » de chaque test
natif. L'une d'elles était **déjà fausse** — `docs/ROADMAP` pointait vers
`scripts/extract-presets.py` depuis l'écriture de l'éditeur de danses, alors que
le fichier a toujours vécu dans `tools/choregraphies/`.

Les deux répertoires gagnent un README. Le tableau de `tools/` listait **deux de
ses six** outils, et c'est ainsi que trois d'entre eux sont restés inconnus des
mois durant : un outil introuvable est un outil qu'on réécrit.

---

### Forcer la resynchronisation de l'horloge, et un pluriel là où il y a plusieurs fichiers (08-10)

L'horloge ne pouvait être réglée qu'une fois, à la fin de la séquence de réveil,
et seulement si le robot avait déjà une IP à ce moment-là. Un réseau arrivé plus
tard — des identifiants saisis dans la console, un point d'accès revenu —
laissait le companion sur ce que la RTC avait dérivé, sans autre recours qu'un
redémarrage.

**`POST /api/clock/sync`** redémarre le client SNTP et, surtout, **réarme
l'écriture de la RTC**. Effacer le drapeau « un paquet est vraiment arrivé » est
tout l'enjeu : laissé actif, la passe de boucle suivante aurait réécrit la puce
depuis le vieux `time()` dérivé en appelant cela une synchronisation — la
tromperie exacte que le rappel SNTP avait été introduit pour finir. C'est
différé vers `loop()` comme toute écriture (A2.6 : `configTime` démonte et
remonte le client, et la RTC est de l'I2C sur le bus 11/12 partagé), et cela
répond **409 en mode AP** plutôt que de faire semblant : le robot y EST le
réseau, donc aucune route vers un serveur de temps, et un état « en attente »
serait un mensonge qui ne se résout jamais.

**`GET /api/clock`** publie deux faits que la console confondait. `plausible`
signifie que l'époque ressemble à une vraie date — ce qui ne prouve rien,
puisque `M5.begin()` restaure l'horloge système depuis la RTC à chaque
démarrage. `ntp` signifie qu'un paquet est vraiment arrivé. Une horloge qui a
l'air juste et qui n'a jamais vu de NTP, c'est exactement ce que l'ancien garde
a cru pendant des semaines.

Dans la console, un groupe **Horloge · NTP** ouvre la section Système avec le
bouton et les deux pastilles ; l'heure est accompagnée de « depuis la RTC, non
vérifiée » tant que NTP n'a pas confirmé. Enregistré avant `/api/clock` pour que
le préfixe ne l'avale pas (A2.19).

Et dans la section fichiers de la SD, le groupe qui contient `config.yaml`,
`rules.txt` et les configs invitées s'appelle désormais **Configurations** — il
n'y en a jamais eu qu'une seule.

---

### Le témoin de crête est coloré par sa propre hauteur (08-10)

Une couleur fixe rendait toutes les transitoires identiques : un claquement
atteignant la pleine échelle et une porte qui se ferme trois blocs plus haut
étaient la même marque à deux endroits, et l'œil devait lire la *position* pour
savoir laquelle. La couleur est le canal le plus rapide : elle porte désormais
le même fait que la hauteur — la palette, assombrie, près de l'axe ; blanc au
bord de la zone — et les deux s'accordent par construction.

Le blanchiment est **quadratique**. En linéaire, tout ce qui dépassait le milieu
se lisait à peu près blanc et le haut de l'échelle cessait d'être remarquable ;
au carré, le témoin reste sur la palette l'essentiel de son parcours et seules
les crêtes qui atteignent vraiment le bord virent au blanc. Cela répond aussi à
la remarque précédente par l'autre bout : le blanc pur partout criait plus fort
que la pile qu'il annote, et une couleur sombre unique ne disait à l'œil rien
que la position ne disait déjà. Maintenant les fortes crient, les autres non.

`sndGrad` se scinde en un cœur RGB888 et une enveloppe 565, pour qu'un appelant
qui doit continuer à mélanger — le témoin poursuit sa route vers le blanc — ne
soit pas obligé de refaire la palette depuis le début.

---

### Matrix cesse d'avaler les sons faibles (08-10)

*« columns a l'air plus sensible. »* En effet, et la mesure a séparé deux causes
qui n'en paraissaient qu'une.

Un harnais jetable a donné le même bloc aux trois habillages en rapportant la
hauteur dessinée par chacun. **Les crêtes coïncident exactement** — un ton de
625 Hz atteint 13 px en `wave` et 13 px en `columns` — donc aucun défaut
d'échelle, aucun gain qui dérive. Ce qui diffère, c'est la moyenne : 8,2 px
d'encre pour la courbe contre 12,5 pour les barres, parce que `wave` trace la
forme d'onde instantanée là où les habillages en barres tracent son ENVELOPPE.
Au-delà de quelques centaines de hertz, les barres se tiennent près de la crête
pendant que la courbe passe l'essentiel de son temps en dessous. C'est ce qu'est
une enveloppe ; aligner la courbe reviendrait à dessiner une enveloppe en
l'appelant oscilloscope.

La seconde cause était un vrai défaut. Matrix calculait son nombre de blocs par
`h / 4`, donc tout ce qui était sous un sixième de la pleine échelle ne
dessinait **rien du tout** alors que `columns` — les mêmes nombres, l'autre
habillage — le dessinait. Deux habillages d'une seule image en désaccord sur
l'existence même d'un son, la seule chose sur laquelle ils n'ont pas le droit de
diverger. Un niveau non nul allume désormais au moins un bloc ; la quantification
gouverne toujours le NIVEAU, elle ne gouverne plus la PRÉSENCE. Le plafond est
inchangé, donc le témoin de crête se pose toujours un bloc au-delà du bloc le
plus haut de la pile et les trois `static_assert` tiennent encore.

---

### Dix lignes d'air au-dessus des icônes, et la prose rattrape le code (08-10)

La zone du bandeau perd six lignes par le bas : **163..221 → 163..215**, l'axe
passe à 189, la demi-hauteur de 27 à 23. Les marges sont désormais volontairement
inégales — trois lignes sous les yeux, dix au-dessus de la rangée d'icônes —
parce qu'une barre à pleine échelle s'arrêtant deux lignes sous une ligne de
petits glyphes se lisait comme la touchant : deux choses sans rapport fondues en
une bande encombrée. En haut, la zone des yeux est surtout noire à sa base, donc
la trace y a de l'air que la constante le dise ou non. 23 et pas 26 pour la même
raison 4m+3 qu'avant, et ce sont les trois `static_assert` qui ont attrapé
l'arithmétique, pas l'écran.

**Et la prose a rattrapé le code.** La documentation décrivait encore le
visualiseur comme un oscilloscope PLUS un spectre disposé en deux moitiés — ce
qui a cessé d'être vrai le jour où les styles sont devenus des habillages d'une
seule forme d'onde, mais une prose ne fait pas échouer un build. Corrigé dans
`STATUSBAR.md` §6 (l'affirmation d'ouverture, le paragraphe sur l'absence de
symétrie forcée, la table d'analyse — qui nomme maintenant l'enveloppe et les
deux maintiens de crête, et dit lequel est dessiné), `CONFIG.md` (le gain fait
monter la trace, l'enveloppe et les bandes), `CLAUDE.md`, et les en-têtes de
`Renderer.h`, `SoundFrame.h` et `SoundViz.h` — ce dernier s'ouvrait encore sur
« les deux choses que dessine le bandeau ».

La ligne de coût mesuré a été remplacée plutôt que rafraîchie : l'ancien
« 4,3 à 8,8 ms » venait de `frameAvgUs`, qui est la frame ENTIÈRE, yeux compris.
Remesuré sur les trois styles, il tombe entre 4 et 10 ms, et bouge davantage
selon ce que font les yeux que selon le style choisi. Le dire vaut mieux qu'un
nombre qui a l'air d'être une mesure du bandeau.

**`flight-radar` cesse d'être « le bin de démo ».** Il avait été présenté ainsi
quand il était le seul invité ; il y en a quatre maintenant, plus deux portages
autonomes sur Fire. Le `README` retire l'étiquette, le démarrage rapide
construit `space` et dit que les deux mêmes lignes valent pour n'importe lequel,
et `docs/guests/SPACE.md` — qui existait mais n'était listé nulle part — rejoint
l'index du `README` et de `CLAUDE.md`.

---

### Le bandeau son prend la hauteur qu'il a (08-10)

Deux remarques de plus sur les styles, et la seconde était une question qui
valait d'être posée : non, le visualiseur n'exploitait pas la place dont il
disposait. Sa zone allait des lignes 166 à 205 — quarante lignes héritées de la
mise en page du TEXTE, là où une ligne défilante de 8 pixels veut se poser. Mais
un visualiseur n'est pas du texte. Entre la dernière ligne de la zone des yeux
et l'effacement de la rangée d'icônes, il y a soixante-trois lignes, et il en
utilisait quarante : la trace était un ruban fin dans une large bande noire.

La zone est désormais les lignes **163..221** (trois d'air sous les yeux, deux
au-dessus des icônes), l'axe passe à 192, et la demi-hauteur passe de **19 à
27** pour les trois styles d'un coup — trois habillages d'une seule forme d'onde
doivent s'accorder sur la hauteur de la pleine échelle, donc le 18 séparé de la
trace disparaît avec eux. Matrix gagne deux rangées de blocs par demi.

27 et pas 29, que la zone permettrait : la marque la plus extérieure de matrix
est le bloc de crête en `k = HALF/4`, dont la ligne haute est
`CY − 4·(HALF/4) − 3`, et cette ligne n'est dans la zone que si la demi-hauteur
est de la forme 4m+3. À 29, le bloc de crête d'une colonne à pleine échelle
serait sorti d'une ligne et `mSeg` l'aurait rogné en silence — le même rognage
discret que le déplacement de l'axe avait jadis corrigé, revenu par une autre
porte. Trois `static_assert` le vérifient maintenant à la compilation, plus deux
sur la table de segments, car le mode de panne ici n'est jamais un plantage :
c'est de la peinture qui cesse d'apparaître exactement au moment où on regarde.

Deux conséquences traitées. Le texte par-dessus le bandeau (une alerte, un
`say`) efface la zone propre du visualiseur à la transition, puisque
l'effacement par frame du texte ne la couvre plus et que des barres
survivraient au-dessus et au-dessous de la ligne. Et le témoin de crête de
matrix recule du blanc pur vers la couleur de texte de la console à 70 % : à
pleine luminosité, il criait plus fort que la pile qu'il est là pour annoter.

---

### Le bandeau son : trois rendus, et un témoin de crête légitime (08-10)

Trois demandes sur les styles, et la première rouvrait une décision.

**Matrix garde un témoin de crête blanc, et ses blocs sont carrés.** Le témoin
avait été retiré la veille pour une vraie raison — un maintien calculé sur la
colonne d'affichage est du lissage né dans le renderer, ce qu'A2.15 interdit.
La réponse n'est pas d'y renoncer mais de le mettre là où vivent les mémoires :
l'enveloppe ET son maintien vivent désormais dans `SoundViz` et sont publiés
dans `SoundFrame` (`env`, `envPeak`), et le renderer ne fait que les dessiner.
Il monte instantanément, redescend à vitesse fixe — environ trois secondes
depuis la pleine échelle — et un retour à *micro au repos* l'efface, si bien
qu'une transitoire d'il y a une minute n'est jamais la première chose que dit
le bandeau. Les blocs passent de 6 × 3 à **3 × 3** : on les disait carrés, ils
ne l'étaient pas. Dessiné en blanc plutôt qu'en teinte pâle de la palette, pour
se lire comme une marque d'une autre nature : non pas un niveau, mais où le
niveau est passé.

**L'encre de wave suit l'agitation.** Une courbe calme est quasi noire —
présente, à peine — et blanchit le long de la palette quand la courbe bouge. Un
trait de luminosité constante fait paraître le silence et la parole aussi
mouvementés l'un que l'autre ; maintenant la bande est sombre quand la pièce
l'est, et l'œil n'est appelé que s'il s'est passé quelque chose.

**Columns est monochrome.** Le dégradé en largeur faisait lire une rangée de
traits fins comme un graphe de dégradé — une seconde chose dite par la couleur
alors que la hauteur disait déjà la seule chose que ce style a à dire.

Le `hot` de `sndGrad` devient une quantité plutôt qu'un drapeau, puisque wave
blanchit progressivement et qu'un booléen ne peut que basculer ; `true` se
convertit toujours en 1,0, donc tous les appels antérieurs gardent leur sens.
Cinq cas natifs fixent le nouveau contrat côté producteur : l'enveloppe suit la
trace, prend le micro le plus fort, la crête se maintient puis retombe, ne
passe jamais sous l'enveloppe, et le repos l'oublie.

---

### Le bandeau son : pleine largeur, et trois habillages d'une même onde (08-09)

Deux demandes, et la seconde corrigeait une erreur de conception. D'abord les
trois styles couvrent la dalle **bord à bord** — wave passe à 160 colonnes ×
2 px = 320, les styles à barres au pas de 10 × 32 = 320, et la table de
segments suit (560 → 704, la dérive que son propre commentaire annonçait).

Puis la vraie : **columns et matrix sont désormais des habillages de wave**,
pas des spectres. Les images de référence n'ont jamais montré d'analyseurs —
elles montraient la même silhouette de forme d'onde vêtue en traits fins
(viz2) et en blocs de pixels (viz3). Les trois styles dessinent la même
fenêtre déclenchée de 256 échantillons, le temps en largeur : wave en courbes
continues, columns en **enveloppe** de chaque groupe de cinq échantillons (le
plus fort des deux micros — une silhouette n'a qu'une hauteur), matrix en la
même enveloppe quantifiée en blocs. Changer de style change l'habit, jamais le
propos.

Cela répond aussi à la question de la symétrie : le miroir gauche/droite
venait de la disposition du spectre (un canal par moitié, graves vers
l'extérieur) sur deux micros qui entendent presque la même pièce. Un habillage
de forme d'onde n'a pas de moitiés.

Plus de témoin de crête nulle part — un maintien calculé sur la colonne
d'affichage serait du lissage né dans le renderer (A2.15). Le spectre par
bandes reste calculé et publié dans `SoundFrame` : rien ne le dessine
aujourd'hui, c'est la nourriture de la bouche de l'assistant (ROADMAP §8).
Mesuré sur cible : columns 9,9 ms, matrix 8,0 ms sur 33.

---

### La revue max atterrit : quinze correctifs, dont quatre invisibles à l'écran (08-09)

La revue interrompue a fini par aboutir : 24 constats vérifiés, 15 au-dessus du
seuil, tous corrigés. Les invisibles sont ceux qui valent lecture :

- **L'auto-luminosité de `space` s'éteignait à chaque démarrage.** Le couplage
  « un réglage à la main coupe l'auto » tirait aussi pendant la RELECTURE du
  yaml, où `bright` porte encore le défaut compilé — la ligne `bright:` de la
  carte tuait donc l'`auto_bright: 1` chargée deux lignes plus haut. Un
  fichier rejoué n'est pas une main.
- **Le jeton NOTAM rouvrait la course à deux tâches que son propre commentaire
  déclarait fermée** — le chemin 401 sauvait encore depuis netTask, et en
  pré-NTP il persistait une échéance que le chargeur jette.
- **Un fuseau hérité se scellait** dans le yaml du bin au premier
  enregistrement sans rapport, rendant « la clé du bin gagne » indéfaisable.
- **La trace gardait le continu que le spectre retire** : un micro sur offset
  ne croise jamais zéro, le déclencheur ne tirait jamais — l'artefact exact
  qu'il existe pour empêcher.

Aussi : `band_mouth` transportait la clé mais pas les VALEURS renumérotées
(une carte qui avait choisi Onde recevait Colonnes) ; deux lecteurs « bornés »
sans drain de ligne trop longue (aéroport fantôme mis en cache pour toujours,
entité HA fantôme) ; le roster HA réécrivait des octets identiques ~2900 fois
par jour ; le flush du bandeau payait ~512 transactions SPI non groupées par
frame bruyante ; les traces WAVE se perforaient aux croisements ; le registre
micro mentait sur les vieilles cartes ; l'analyseur tournait ~31 FFT/s pour un
bandeau invisible ; un cran de luminosité ne s'appliquait jamais ; et la
console FR étiquetait « (min) » un curseur en HEURES.

Les documents de pilotage ont rejoint la politique tel-quel le même jour —
ROADMAP 1256 → 1079 lignes, lignes fermées du backlog supprimées, dates
d'incident sorties des 24 règles, et de vraies contradictions corrigées contre
la source (`presets/` annoncé « GÉNÉRÉ — ne pas éditer », l'inverse exact de la
règle A2.11).

---

### La colonne LUNE répond à la question qu'on lui pose (08-09)

La colonne de droite utilisait deux tiers de son cadre de 124 × 162 ; elle le
remplit désormais, de l'identitaire au factuel : le nom de la phase, le
pourcentage éclairé en taille **triple**, une barre d'identité cyan→indigo, puis
**la prochaine échéance** — *pleine lune ~ 3 j*, *nouvelle lune ~ 12 j*, *ce
soir* sous un jour. « C'est quand la pleine lune » est la question qu'on pose à
un écran de lune, et la colonne n'y répondait pas : la bande des phases montre
l'ordre de ce qui vient, pas le temps pour y arriver. `moonDaysToElong` est pur,
au taux synodique moyen (l'exact demanderait une recherche de racine pour un
chiffre lu en jours entiers), testé nativement.

Le travail de géométrie a fait sortir trois défauts : le plus long nom de phase
(« Gibbeuse décroissante », 126 px) **débordait du panneau de 2 px depuis
toujours** — la colonne passe à x=190, où tout le vocabulaire tient ; GCC 8.4
**déroule une boucle à bornes constantes de deux itérations** et la retransforme
en deux appels de dessin similaires qu'A2.22 interdit, `#pragma GCC unroll 1` ne
l'arrête pas (vérifié au désassemblage — un `asm volatile` vide sur la borne,
oui) ; et le premier libellé nocturne ne tenait **ni dans son tampon ni sur la
dalle** — 24 caractères dans un `buf[24]` et 144 px sur une colonne de 130.
« ce soir » : 21 glyphes, la capacité exacte.

Revue de suite : cinq littéraux `15000000` avaient survécu à la factorisation
SdPins (deux dans `hal/Board.h` — le companion, la cible dont la dérive
silencieuse coûte le plus — trois dans le radar) ; tous lisent désormais
`SCE_SD_HZ`, plus aucun littéral hors `SdPins.h`. Et la borne de la pleine lune
était stricte : à exactement 180°, le compte à rebours annonçait la prochaine
*nouvelle* lune, à quinze jours, à l'instant même de l'événement.

---

### Les deux dernières duplications, et une doc qui se contredisait (08-08)

**Le brochage SD était écrit quatre fois** — trois bins invités et
`hal/Board.h` — et les copies s'accordaient sur les nombres en divergeant sur la
discipline, ce qui est la moitié la plus dangereuse. `flight-radar` gardait
chaque broche séparément et écrivait pourquoi ; `space` gardait les quatre
derrière la première, c'est-à-dire **exactement le piège que ce commentaire
décrit** : un profil de carte qui n'écrase que `SCE_SD_CS` laisse `SCE_SD_SCK`
indéfini, le bloc remet CS à 4 en silence, l'avertissement de redéfinition file
dans le log, le montage échoue, et il n'y a plus ni identifiants WiFi ni STA.
`ha-remote` n'avait aucune macro — `SPI.begin(36, 35, 37, 4)` en littéraux, deux
fois, hors d'atteinte de tout profil. Un seul en-tête désormais, quatre gardes,
une par broche ; les deux profils Fire vérifiés comme l'écrasant toujours.

**Le contrat invité se contredisait sur le WiFi.** La section d'intégration
décrivait encore la chaîne d'avant la NVS — carte, puis arguments de compilation,
puis point d'accès — pendant qu'une section plus bas décrivait la nouvelle. La
priorité est maintenant une table, là où le lecteur la rencontre, avec la NVS au
rang 1 et la raison pour laquelle elle passe devant la carte.

**Et le verdict de la page de réglages est écrit.** Les trois bins répondent
différemment quand une écriture SD échoue : `space` rend le résultat synchrone
réel, donc une carte pleine affiche vraiment une erreur ; les deux autres
annoncent un succès et diffèrent l'écriture, parce que la leur doit relâcher un
verrou et ne doit pas bloquer un gestionnaire HTTP. Aucune n'est fausse, les deux
sont délibérées, et aucune n'était documentée — un quatrième bin en aurait donc
choisi une par accident. Le contrat dit maintenant ce que `false` promet, et
qu'un réessai différé demande un backoff.

---

### Revue : une réponse fausse sur trois, et huit affirmations démenties (08-08)

**`off` valait vrai.** Trois bins avaient chacun leur idée de ce qu'est une
valeur de configuration. Deux comparaient exactement à `"1" | "on" | "true"` ;
`space` testait le PREMIER CARACTÈRE, donc `off` se lisait **vrai** — il
commence comme `on`. Rien n'envoyait `off` depuis le formulaire, donc cela n'a
jamais tiré ; un yaml édité à la main l'aurait fait. Une seule définition
désormais, dans `firmware/common/CfgBool.h`, **pure et testée nativement**
(`test_cfgbool`, 6 cas, dont le premier est `off`).

**Un correctif arrivé chez un jumeau et pas chez l'autre.** `ha-remote` s'est
doté d'un backoff de 5 s sur une écriture SD ratée, en écrivant pourquoi : « sans
lui, loop() martelait le bus SPI2 partagé avec l'écran ». `flight-radar` avait la
même forme, sans limite : sur une carte pleine ou protégée en écriture, il
rouvrait le fichier toutes les dix millisecondes, pour toujours, sur le bus dont
le renderer a besoin.

**Le dégradé de matrix s'arrêtait trop tôt.** Son étendue était le littéral `8`,
le nombre de blocs de l'ancienne barre pleine hauteur. Les barres devenues
demi-hauteur, le bloc du haut plafonnait à 0,69 de clarté au lieu de 0,94, terne
à côté d'une crête toujours dessinée à pleine intensité — précisément au moment
où l'on regarde. L'étendue est maintenant dérivée de la constante qui la
détermine.

**Et huit affirmations documentées étaient fausses**, la pire activement
dangereuse : `CONFIG.md` annonçait `soundtrack_sign: 1` comme « validé K151 »
alors que le code porte `-1` et avertit, deux fois, que l'inversion a déjà été
faite deux fois — la doc invitait à la faire une troisième. Également corrigés :
`head_follow` documenté à l'arrêt alors qu'il est actif par défaut, le plafond de
réglages invités (20 → 24), le budget de lignes de l'écran sans carte (6 → 5),
deux clés de tuning vivantes jamais documentées (`led_swap`, `cam_colorbar`), la
section lancements décrivant encore la file de texte que l'horizon a remplacée,
et la table des familles de fusées sans le profil `not announced`.

---

### La palette du bin space n'avait jamais été vérifiée (08-08)

`scripts/gates/check-contrast.py` garde les quatre thèmes du radar depuis sa création,
et il lisait exactement un fichier. Le bin `space` porte un second `THEMES[]`
avec une autre liste de champs : il n'a donc jamais été analysé — et `hint`, que
ce bin utilise pour de l'**information** (l'attente d'horloge affichée en taille
2 à chaque démarrage, la puce radio de PASSAGES, toutes les légendes du dôme, la
puce TBC/TBD des lancements), mesurait **2,78:1 en Deep et 1,66:1 en Nuit** pour
un seuil de 4,5:1. Un gate qui couvre une palette sur deux annonce CONFORME à
propos de la moitié du produit.

Le gate analyse désormais les deux, avec un **fond par champ** — les traits de
côte sont dessinés sur les remplissages jour et nuit de la carte, et les juger
sur du noir les aurait flattés. Sept échecs sont sortis ; c'est la palette qui a
été corrigée, pas les seuils relâchés, chaque couleur montée au minimum qui
passe en gardant sa teinte.

Du même audit :

- **La légende ISS pouvait manger la longitude.** Un nom de satellite de 24
  caractères plus `"  -90.00  -180.000"` fait 42 caractères dans un tampon de
  39 : `snprintf` coupait la *coordonnée*, en silence, et une longitude tronquée
  se lit comme un autre endroit. Le nom est ajusté d'abord, pour que la perte
  tombe là où elle se voit et coûte le moins.
- **La puce d'état des lancements pouvait recouvrir le nom du lanceur.** Le
  commentaire affirmait que le plus long statut était « In Flight » et que la
  collision était impossible ; le champ en accepte onze caractères et Launch
  Library envoie « Partial Failure ». Un fait sur un flux se met dans une borne,
  pas dans un commentaire.
- **Le « E » du dôme et la mention « de jour » partageaient leurs pixels**, sans
  une colonne entre eux, à chaque heure diurne — c'est-à-dire la plupart des
  heures où cet écran s'ouvre.
- **L'élévation porte son unité** partout où elle voisine un azimut qui la
  portait déjà : le même panneau montrait deux valeurs en degrés à quatre pixels
  l'une de l'autre, l'une étiquetée et l'autre non.
- **La bande des lancements annonce son geste.** PASSAGES dit que ses lignes se
  tapent ; celle-ci ne disait rien, et son geste est le moins devinable des deux.

---

### L'écran « pas de carte » appartient à SceGuest (08-08)

Deux bins l'avaient écrit à la main, le second en copiant le premier — le mode
de défaillance exact de la règle 17, et les jumeaux avaient déjà commencé à
diverger. Il vit désormais dans `SceGuest`, où chaque bin invité en hérite, y
compris ceux que personne n'a encore écrits.

- **SceGuest possède le mécanisme** : la mise en page, la boucle de réessai,
  l'aiguillage des entrées, le point d'appel `drawString` unique (A2.22, épinglé
  UNE fois au lieu d'une par bin), et toute ligne vraie de n'importe quel bin.
- **Le bin possède ses conséquences**, et elles sont **optionnelles** —
  `guest.noSdNotice(remount)` seul donne un écran complet. Un écran qui ne
  marche qu'une fois cinq chaînes écrites est un écran que les nouveaux bins
  n'auront pas.
- **Le bin possède le remontage** : l'en-tête ne doit jamais apprendre le
  brochage SD de qui que ce soit.
- **Les entrées interrogent la dalle, pas un drapeau de build**
  (`M5.Touch.isEnabled()`) : moitiés tactiles sur CoreS3, A/B/C sur Fire, depuis
  le même binaire.

`ha-remote` a gagné l'écran qu'il n'avait jamais eu, et c'est à cela que sert
une factorisation. Sans carte il n'a aucun Home Assistant — l'hôte et le jeton
longue durée vivent dans le yaml, sans défaut compilé — donc il démarrait, ne
découvrait rien, et affichait un accueil vide impossible à distinguer d'un
accueil qui marche.

Les lignes génériques disent aussi ce qu'une carte absente **laisse** : le
réseau, qui vit maintenant en NVS. Sans cette ligne, l'écran se lirait comme
« vous êtes coincé sur le point d'accès pour toujours ».

---

### `space` dit quand il n'y a pas de carte (08-08)

La carte était optionnelle et son absence silencieuse — la pire combinaison.
Sans elle, l'observateur retombe sur la **position compilée** (Paris), et un
ciel dessiné pour le mauvais endroit ressemble exactement à un ciel dessiné pour
le bon : les passages sont faux de plusieurs heures, et au sud de l'équateur la
Lune est éclairée du mauvais côté.

L'écran de démarrage le dit désormais, et annonce la position sur laquelle il
est réellement retombé — **formatée depuis la config vivante**, jamais écrite en
dur, parce qu'un « Paris » codé continuerait d'annoncer Paris le jour où le
défaut compilé change. Réessayer après avoir inséré une carte, ou continuer sans
elle ; une carte trouvée au réessai fait relire la configuration, puisque tout
ce qui a été lu avant tournait sans carte.

Même idiome que l'écran du radar, point d'appel `drawString` unique compris
(A2.22) — onze lignes qui ne diffèrent que par le datum et la couleur. Celui-là
est épinglé dans le gate : une ligne amincie du binaire emporterait
l'avertissement avec elle.

---

### Une phrase secrète WiFi peut contenir n'importe quel caractère imprimable (08-08)

Elle ne le pouvait pas, et rien ne le disait. `"` et `\` étaient **supprimés en
silence** à trois endroits — le filtre de `/api/wifi`, le nettoyeur des
identifiants d'API et l'écriture du YAML — au motif qu'il fallait tenir les
guillemets à l'écart du JSON et de la carte. Ces deux caractères sont légaux en
WPA-PSK, qui accepte 8 à 63 caractères ASCII **imprimables**.

Le mode de défaillance était le plus vicieux. La valeur acceptée par l'API était
celle passée à `WiFi.begin()` : **le robot rejoignait donc le réseau** et
annonçait un succès. La valeur écrite sur la carte était la valeur amputée :
après le redémarrage suivant, plus aucune connexion — une panne qui survient des
heures plus tard, à un redémarrage que personne ne relie au réglage, sans un
message nulle part.

- `"` et `\` sont désormais **échappés** dans les scalaires quotés de la carte,
  et le décodeur partagé les relit (`\"` et `\\`). Les guillemets simples ne
  prennent pas d'échappement, c'est la règle de YAML.
- Le filtre d'identifiants prend un drapeau `quotable`. Les identifiants WiFi
  l'utilisent ; **ceux de l'API non**, et cette différence est réelle et non un
  oubli : le nom d'utilisateur de l'API est émis dans `/api/status` avec un `%s`
  brut, donc un guillemet non échappé y casse tout le document JSON et vide la
  console.
- Six cas natifs fixent l'aller-retour, dont un antislash juste avant la quote
  fermante — celui qui fait avaler le reste de la ligne à un analyseur naïf. Ils
  échouent contre le code précédent.

Tout le reste marchait déjà et reste couvert : `#`, `:`, espaces, `&`, `%`, `+`,
`<`, `>`, `'` et l'UTF-8 accentué.

---

### `space` tourne aussi sur un Fire (08-08)

Le même instrument en application autonome sur M5Stack Fire : pas de StackChan,
pas de companion, pas de K151. Pas un fork — la même source, avec les
différences matérielles déclarées en drapeaux de capacité
(`SCE_INPUT_BUTTONS`, `SCE_COMPANION`, `SCE_SD_*`), chacun nommé d'après ce que
la carte **possède**.

- **La cartographie des boutons avait un trou, et c'était le morceau
  intéressant.** C court valait *Select* sur les deux vues en liste, qui se
  retrouvaient donc **sans pas en avant** : le curseur ne pouvait être parcouru
  qu'à l'envers, précisément sur les écrans bâtis autour d'un curseur. La
  cartographie est désormais uniforme et ne dépend plus de la vue — A/B/C court
  donnent élément précédent / vue suivante / élément suivant, long donnent
  réglages / rafraîchir / ouvrir le détail. Un contrôle dont le sens change avec
  l'écran est un contrôle qu'il faut apprendre, et quitter une modale ne demande
  pas de bouton à soi puisque B la ferme déjà.
- **Pas de `SCE_HAS_LTR553`, contrairement au radar**, et c'est une décision :
  le capteur de lumière est sondé au boot et le résultat publié dans `/config`.
  La sonde couvre déjà la carte sans capteur — et en prime celle dont le
  capteur est mort, ce qu'un drapeau de compilation ne ferait jamais.
- Le geste de sortie est coupé là où il n'y a pas de companion : le swipe
  ouvrirait une confirmation dont le « oui » ne mène nulle part.
- **La table A2.22 de `space-fire` est dérivée de celle de `space`**, pas
  recopiée : un point ajouté à l'une couvre l'autre. Les deux tables du radar
  étaient tenues à la main et avaient déjà divergé. Une exclusion est nommée
  plutôt que filtrée en silence : sur le backend ESP32, GCC inline
  `drawSkyNames`, il ne reste donc aucun symbole à compter.

Le gate construit six firmwares et vérifie A2.22 sur les deux backends — 69
points d'appel épinglés.

---

### La bande de statut gagne un vrai analyseur, et l'écran sa propre luminosité (08-08)

- **Le mode son est mesuré, pas généré.** La bande dessinait une texture et le
  disait honnêtement en commentaire ; elle dessine désormais une **trace
  d'oscilloscope** et un **spectre par bandes** issus d'une vraie
  transformation. `engine/Fft.h` est pur et testé nativement — une sinusoïde
  tombe dans sa case et nulle part ailleurs, le continu disparaît partout, un
  canal reste muet pendant que l'autre sature. Les deux micros sortent d'**une
  seule** transformation de 512 points : le spectre d'un signal réel est à
  symétrie conjuguée, donc le gauche part dans la partie réelle, le droit dans
  l'imaginaire, et les deux se séparent exactement après coup. L'oscilloscope
  est **déclenché** sur un passage par zéro montant, sans quoi un ton stable
  commence à une phase différente à chaque bloc et l'image glisse indéfiniment.
  Seize bandes logarithmiques, chacune valant le pic de ses cases, sur une
  échelle en décibels à attaque rapide et relâchement lent. Mesuré sur cible :
  **4,3 à 8,8 ms** sur les 33 ms de frame — moins cher que la texture qu'il
  remplace.
- **La moyenne est retirée avant la fenêtre, pas après.** Une fenêtre a un
  spectre à elle : une constante multipliée par une fenêtre de Hann atterrit en
  cases 1 et 2 autant qu'en case 0, donc annuler la case 0 laisse deux barres
  fantômes plantées sous tout le reste. La suite native a pris l'ordre en défaut
  avant le moindre passage sur matériel.
- **`screen_bright` est la dalle ; `eye_color_dim` est l'encre.** Le seul réglage
  de luminosité était la palette des yeux, qui laissait la bande de statut, le
  launcher et tous les bins invités à pleine puissance. Les deux sources —
  capteur et manuelle — calculent maintenant **une** cible en un seul endroit, et
  la valeur manuelle s'applique dès qu'elle change au lieu d'attendre le pas de
  deux secondes du capteur. Bouger le curseur coupe la luminosité automatique,
  sans quoi le capteur écraserait la valeur en deux secondes. Plancher à 10 :
  c'est le seul réglage qui pourrait se cacher lui-même.
- **Le spectre avait ses bandes à l'envers.** Les graves occupaient l'écart
  central et les aigus les bords, l'inverse de la conception et du propre
  commentaire du code. Les graves dominent presque tout signal : l'affichage se
  fabriquait une bosse centrale permanente qui ne disait rien du son.
- **Le témoin de crête perforait sa propre barre.** Sur une transitoire la barre
  et la crête montent ensemble, donc la ligne que le témoin occupait se retrouve
  DANS la barre — et l'effacer en noir y taillait une encoche, une frame par
  claquement.
- **« micro au repos » ne pouvait jamais s'afficher.** Le producteur ne publiait
  que sur une transition vivant→repos ; au démarrage rien n'a jamais été vivant,
  et le micro est éteint par défaut. La bande restait vide au lieu de dire
  pourquoi.

### Un bin invité peut apprendre un réseau (08-08)

- **La page de réglages porte toujours un bloc Réseau**, que le bin déclare ou
  non le moindre réglage à lui — c'est précisément celui-là qui risque de rester
  échoué. Les identifiants vont en **NVS** et non sur la carte : la NVS survit à
  un reflash de l'application, pas une carte SD ; une carte s'écrit une fois et
  se clone sur dix robots, pas la NVS.
- **Ce qui a été saisi sur l'appareil l'emporte sur la carte.** L'inverse, et le
  champ ne fait silencieusement rien sur tout robot dont le `config.yaml` nomme
  encore l'ancien réseau — c'est-à-dire le robot devant lequel quelqu'un se
  tient. *L'oublier* rend la main à la carte.
- **Le point d'accès sert un portail captif.** Tout nom résout vers le bin, donc
  rejoindre le réseau ouvre la page tout seul au lieu d'exiger une adresse que
  rien n'a affichée. `isAp()`/`apSsid()` laissent un bin nommer le réseau à
  rejoindre à l'écran ; l'écran info du radar le fait.
- Le réseau est enregistré **avant et hors** du verrou de l'application : un
  utilisateur qui vient de saisir un réseau sur un robot échoué ne doit pas le
  perdre parce qu'une écriture sans rapport sur une carte absente a rendu false.

### L'écran des lancements devient graphique (08-08)

- **Un horizon remplace la file de cinq lignes de texte.** Un axe de temps
  logarithmique (MAINT / 1j / 1sem / 1M) porte la date par la position, un
  lanceur dessiné porte ce qui vole, et une pastille porte le fait que ce soit
  confirmé. Logarithmique parce qu'en linéaire quatre marqueurs sur cinq
  tiennent dans le premier centimètre.
- La puce d'état est **pleine** quand la date est tenue et **vide** quand elle
  est TBD/TBC : la forme porte la distinction, donc le gris terne peut rester
  terne sans tricher sur le contraste.
- L'appui vise le marqueur le **plus proche** et non une boîte — une silhouette
  de 10 px n'est pas une cible de doigt.

### La documentation décrit le système tel qu'il est (08-08)

Le projet n'est pas publié : le récit de ses évolutions n'apprenait rien à qui
venait s'en servir, et il vieillissait mal. Quatorze fichiers réécrits dans les
deux langues, chaque « avant / désormais » devenu une règle ou la conséquence de
l'enfreindre. Cette histoire vit ici, c'est le rôle d'un changelog.

Des faits périmés corrigés contre la source et non devinés : l'amplitude de
lacet, `cfg_version`, la borne de lacet des danses, le `MM:SS` du minuteur, et
une page de conventions qui réclamait des commentaires en français à côté
d'en-têtes rédigés en anglais partout. `docs/guests/SPACE.md` et
`FLIGHT-RADAR.md` documentent maintenant leurs mécanismes et leurs calculs en
entier, avec du mermaid là où un schéma le mérite.

---

### Caches : ce qui ne change pas n'est plus demande deux fois (08-04)

Quatre interrogations etaient repayees sur un rythme sans rapport avec la
frequence a laquelle leurs reponses changent.

- **flight-radar — les aerodromes.** Resoudre une route, c'est une requete plus
  une par point de passage, jusqu'a six sessions TLS pour un seul appui ;
  l'anneau de routes a deux heures epargnait la repetition dans la session, et
  chaque redemarrage repayait le prix entier. `/stackchan-companion/radar-airports.csv`
  retient desormais position, ville, IATA, pays, **et les frequences tour/ATIS et
  l'altitude du terrain** qui etaient un second point d'acces interroge a chaque
  demarrage — une entree par aerodrome, pas un cache par question. Aucune
  peremption : un aerodrome ne bouge pas. Les reponses negatives sont retenues
  aussi, mais en RAM seulement et uniquement pour les codes auxquels la base a
  effectivement REPONDU ; une panne ne doit pas etre mise en cache comme un fait.
- **flight-radar — le relevé METAR est pilote par l'observation.** Un minuteur
  fixe de dix minutes demandait six fois par heure ce qu'une station publie deux
  fois, et montrait quand meme un rapport jusqu'a dix minutes apres son emission.
  Le cycle est maintenant APPRIS des `obsTime` consecutifs (borne a 10-60 min) et
  le relevé suivant echoit a `obsTime + cycle + 150 s`, borne a 5-15 minutes.
- **flight-radar — un jeton NOTAM frappe avant NTP est conserve.** Son echeance
  ne pouvait pas etre calculee, donc il n'etait jamais ecrit sur la carte — le
  pire cas a jeter, puisqu'un bin qui frappe avant NTP est un bin qui vient de
  redemarrer, et que le compte n'autorise que vingt jetons par semaine. La duree
  restante est desormais mesuree sur l'horloge monotone et datee des que NTP
  arrive.
- **ha-remote — le roster survit au redemarrage.** La decouverte coute quinze
  secondes pendant lesquelles l'ecran d'accueil compte zero de tout.
  Identifiants, noms, categories et capacites sont ecrits dans
  `/stackchan-companion/ha-entities.tsv` et relus au demarrage : le tableau est la en
  une seconde — avec les **etats volontairement vides**, parce qu'un `on` retenu
  et presente comme courant est le seul mensonge qu'une telecommande ne doit pas
  dire.

## Architecture

- **FreeRTOS** : Brain 100 Hz (cœur 1 prio 4, seul écrivain de l'état facial),
  Renderer 30 Hz (cœur 1 prio 3, seul propriétaire de `M5.Display`),
  ServoMotion 50 Hz (cœur 0 prio 3), **tâche caméra** (cœur 0 prio 1 —
  capture + encodage JPEG logiciel hors de la boucle, préemptée par les
  servos et le réseau), boucle Arduino cœur 0 (10 ms).
  `TripleBuffer<FaceState>` lock-free + `CommandQueue` (xQueue) + registre
  `Tuning` (~58 paramètres à chaud, persistés SD, `GET/POST /api/tuning` —
  sauvegarde SD uniquement sur **vrai changement** de valeur).
  `Renderer::pause()` à **refcount sous spinlock** (pauseurs concurrents :
  SD de loop() + DMA caméra, cœurs différents — paire compteur/demande
  atomique, compteur clampé, ack re-vérifié avant de dessiner).
- **Verrou de bus I2C partagé** (`hal/I2cBus.h`) : le bus interne CoreS3
  (`M5.In_I2C` — IMU, AXP, RTC, touch écran, SCCB caméra, codecs audio) et le
  bus corps (`Wire1` — LEDs PY32, touch tête Si12T, jauge INA226) sont sur
  **les mêmes broches physiques G11/G12**. `m5gfx::i2c` n'étant pas
  thread-safe, chaque transaction (IMU, SCCB, batterie, LED de charge, touch,
  LEDs corps) est prise sous un mutex FreeRTOS court à héritage de priorité.
  Le Brain **ne saute aucune** lecture IMU : il bloque au pire le temps
  d'une transaction. SEULE exception : l'init caméra tient le verrou
  EXCLUSIVEMENT sur la rafale de config SCCB (~0,5 s — une rafale non
  exclusive corrompt le capteur) ; le power-cycle ALDO3 (~650 ms) se fait
  HORS verrou, le VOR vit pendant.

## Rendu, yeux, expressions
- **Le robot SE REVEILLE au lieu de se mettre au garde-a-vous** (08-03, idee de
  l'utilisateur). La tete claquait sur la butee basse a chaque demarrage du
  companion : le relachement au repos coupe le couple la ou la tete se trouve,
  plus rien ne la retient, elle s'affaisse sur la butee, et le demarrage suivant
  reengageait le couple contre cette butee et poussait. Jamais sur un bin
  invite, qui n'ecrit rien sur le bus servo.
  Deux mecanismes le corrigent, la choregraphie fait le reste.
  `ServoMotion::begin()` **relache le couple des que le bus existe** — un servo
  mou ne peut rien forcer — et **initialise la pose depuis une MESURE** au lieu
  de la supposer egale aux valeurs de depart, ce qui faisait interpoler toute
  trajectoire ulterieure depuis une fiction et rattraper l'ecart d'un coup au
  premier mouvement. `readDeg` est legal precisement la : la tache de mouvement
  n'a pas demarre, le bus demi-duplex est inerte, il n'y a rien a corrompre.
  `homeSlowly()` reengage ensuite le couple **a la pose mesuree**, donc rien
  n'est force a cet instant, et fait marcher la cible jusqu'a HOME en 1,8 s
  contre les ~400 ms d'un mouvement ordinaire.
  Puis la sequence : yeux fermes, un clignement, la tete se leve, les yeux
  s'ouvrent, la vie normale reprend.
  **C'est l'ORDRE qui fait le correctif, et la premiere version l'avait faux**
  (utilisateur : « je vois Normal avant Sleepy, et les servos s'enclenchent
  avant meme que je voie Normal ») : le premier tick du Brain publie le visage
  par defaut ET commande le cou par le head-follow, ce qui reengage le couple.
  L'emotion est desormais MISE EN FILE AVANT le demarrage du Brain — la
  CommandQueue est vidangee au premier tick, donc la frame une est deja Sleepy —
  et le head-follow est tenu a zero jusqu'a l'arrivee de la tete, puis restaure
  a la valeur de l'utilisateur.
  Trois defauts de plus ont emerge sur cible et en revue (08-03/04), chacun
  cache derriere le precedent. La **mesure elle-meme mentait** : a 120 ms
  apres la mise sous tension le SCS0009 ne repond pas encore, la lecture
  revenait vide et retombait EN SILENCE sur la pose supposee — exactement la
  fiction que le changement existe pour supprimer, reinstauree par un delai ;
  trois tentatives espacees de 150 ms ont produit la capture decisive,
  `pose mesuree 167/71` la ou le code supposait 93 — 22° d'ecart que chaque
  premiere trajectoire rattrapait d'un coup. L'**attach de la bibliotheque
  etait le mouvement residuel** : il ecrit sa position de depart sur un servo
  alimente couple actif avant que notre `torque(false)` puisse agir, donc le
  rail est desormais COUPE pendant l'attach et relachement+mesure vivent dans
  `settleAfterPower()`, premiere chose sur le bus vivant. Et **`homeSlowly()`
  ne demarrait jamais sa rampe** (revue) : il stockait les cibles sans
  incrementer le compteur de sequence que la tache de trajectoire surveille,
  donc la montee de 1,8 s n'existait pas — la tete ne bougeait que parce que
  le changement d'emotion suivant du Brain emettait son propre mouvement de
  700 ms. Il passe desormais par `moveTo()`, et la choregraphie attend la fin
  de la rampe avant de poster Normal, dont le propre mouvement commande alors
  la pose ou la tete est deja.
  **Les yeux s'ouvrent sur l'IP, pas sur un minuteur** (utilisateur 08-04 :
  « garde sleepy jusqu'a ce que StackChan acquiert une IP »). Sleepy tient
  desormais tout le demarrage — montee de tete, chargements SD, association
  WiFi — et Normal est poste juste apres que `webApi->begin()` rend une
  adresse (STA ou AP de repli) : des yeux qui s'ouvrent avant que le robot
  soit joignable promettent une disponibilite qui n'existe pas, et un visage
  endormi pendant une longue association rend « pas de reseau » visible de
  l'autre bout de la piece. Les 120 s de maintien Sleepy sont le PLAFOND :
  un robot dont la radio a echoue se reveille quand meme, dormir pour
  toujours se lirait comme un robot mort. Cela solde aussi la dette relevee
  en revue du reveil PRECEDANT la connexion STA : les secondes endormies
  sont desormais les secondes d'association.
  Cote Renderer, le DERNIER eclair Normal etait le sien : les rigs des yeux
  NAISSENT dans le preset Normal, donc la premiere emotion publiee arrivait
  comme un *morph* Normal→Sleepy — rejouant une face jamais publiee. La
  premiere frame consommee s'applique desormais INSTANTANEMENT, forme et
  couleur : il n'existe aucun etat precedent a l'ecran d'ou morpher, et
  interpoler depuis le defaut du constructeur est une fiction de la meme
  famille.
- **La ligne d'identite NOTAM n'etiquette plus son titre `Q)`** (08-03,
  utilisateur : « on affiche `Q)...` et juste dessous encore `Q)`, n'est-ce pas
  trompeur ? »). Ca l'etait : la ligne du dessous EST l'item Q en entier, et elle
  *contient* ces quatre lettres. Deux choses etiquetees `Q)`, dont une qui n'en
  est qu'un fragment, invite a croire a deux champs differents. Le code reste,
  sans etiquette, comme le titre qu'il est reellement.
- *(Remplacee le jour meme : une entree anterieure consignait ici le claquement
  comme « diagnostique, pas encore corrige », avec le parcage a HOME avant
  relachement comme correctif prevu. L'utilisateur a choisi la sequence de
  reveil ci-dessus a la place — la tete a le droit de s'affaisser, et le
  demarrage la releve lentement d'ou elle se trouve.)*
- **La source ADS-B selectionnee devient la PREFEREE, plus la seule** (08-03).
  Sa panne etait la panne du radar : un ecran vide, sans moyen de distinguer
  « pas de trafic » de « pas de serveur ». La source preferee est toujours
  interrogee en premier, a chaque cycle ; sur un echec de TRANSPORT les autres
  sont essayees dans l'ordre et celle qui repond sert le cycle - et la
  substitution est ENONCEE a l'ecran tant qu'elle dure, parce qu'un radar qui
  montre en silence du trafic venu d'une source que vous n'avez pas choisie est
  pire qu'un radar vide. Un 200 avec un ciel vide n'est PAS un echec et ne la
  declenche pas, sinon chaque heure calme serait poursuivie a travers les trois
  serveurs. SafeSky reste hors rotation comme CIBLE : elle est liee a un
  compte, et se rabattre sur quelque chose que l'utilisateur n'a pas configure
  est une surprise, pas un secours. Elle bascule bien DEPUIS (revue 08-04) —
  la garde excluait aussi SafeSky comme source preferee, privant ses
  utilisateurs du secours exact pour lequel ce mecanisme existe — sauf sur une
  cle absente, qui est une erreur de reglage avec son propre diagnostic a
  l'ecran, pas une panne.
- **« Aeroport inconnu » disait souvent « la base n'a rien repondu »** (08-03,
  recherche `CDG` par l'utilisateur). `CDG` est un code IATA parfaitement
  valide ; c'est hexdb.io qui avait cesse de repondre - DNS resolu, port 443
  accepte, requete suspendue jusqu'au delai, pendant qu'une autre API
  aeronautique repondait 200 depuis la meme machine. La recherche rapportait
  cela comme *inconnu* : elle disait a l'utilisateur quelque chose de faux sur
  son aeroport plutot que quelque chose de vrai sur la base. Les deux sont
  desormais distincts : un HTTP 200 sans latitude signifie que le code est
  inconnu, tout le reste que la base n'a pas parle, et le clavier dit lequel.
- **La pastille de categorie de vol du METAR est toujours dessinee** (08-03,
  utilisateur : « parfois il n'y a rien a la place de VFR »). A moitie normal -
  la categorie n'est PAS calculee ici, elle arrive dans `fltCat` de NOAA, et la
  deriver reviendrait a contredire la source officielle sur une decision liee a
  la securite ; NOAA l'omet souvent, et un commentaire de `fetchMetar` notait
  deja que FMEE le fait regulierement. L'autre moitie etait un defaut : la ligne
  etait SAUTEE, donc 34 px disparaissaient et tout ce qui suit remontait. Le
  lecteur voyait un ecran different sans pouvoir distinguer « la source s'est
  tue » de « l'affichage a oublie » - la regle meme que ce projet applique a une
  troncature. `fltCatColor` prevoyait deja un gris pour l'inconnu ; la pastille
  savait dire « je ne sais pas » et on ne le lui a jamais demande.
- **L'ecran NOTAM a ete redispose pour en porter davantage** (08-03), et la
  carte des champs est documentee dans `docs/guests/FLIGHT-RADAR.fr.md`. Une
  **bande de pont** sous l'en-tete donne trois faits en quatre pixels : combien
  d'avis sont en vigueur, lequel est ouvert, et - par la couleur - combien
  d'URGENTS restent derriere. Les chevrons ont quitte le milieu du bloc de texte
  pour une **bande de page** a son pied, ce qui a rendu au texte toute la largeur
  des filets (47 -> 50 glyphes, une quarantaine de caracteres de plus par page)
  et permis au numero de page de quitter la ligne d'identite. La ligne de
  validite gagne le **temps restant**, grossier a dessein - heures sous deux
  jours, jours au-dela - parce que compter les minutes d'un avis emis a l'heure
  serait une fausse precision.
- **Revue du 08-03 — dix defauts, la plupart dans le code commite le jour meme.**
  Les marquants : la nouvelle detection de changement des jauges n'etait armee
  que par UN des trois chemins qui noircissent ces lignes, donc une alerte ou un
  `/api/say` laissait le bandeau noir en permanence (son propre commentaire
  enoncait le principe que le code enfreignait) ; `servo_idle_release_ms` est
  passe de 15 s a 4 s sans bump de `CFG_VERSION`, donc tout le correctif de
  relachement des servos n'atteignait jamais un robot ayant deja un
  `config.yaml` — c'est-a-dire tous ceux qui avaient le bug (schema **v5**
  ajoute) ; `/api/sd/put` et `/api/sd/delete` n'invalidaient pas le cache de
  noms `/bins`, donc un invite televerse depuis la console etait liste avec un
  bouton Lancer qui repondait 404 ; le travail de listage differe utilisait un
  drapeau `dead` partage avec un callback par requete, donc une deconnexion
  appartenant a un listage DEJA REPONDU tuait le suivant en silence (compteur de
  generation desormais) ; le rail servo n'etait coupe que sur le chemin API,
  donc le launcher tactile — celui que les utilisateurs empruntent — passait un
  cou verrouille a l'invite, et un flash rate laissait le cou sans alimentation
  pour la session ; `enableServoPower()` etait appele sous le verrou I2C et se
  termine par `delay(300)`, la seule chose qu'A2.21 interdit formellement ;
  `danceStore.reload()` tournait hors de son `pause()` ; et le tampon fantome
  etait invalide a CHAQUE resume, donc les pauses camera a ~10 fps rendaient le
  gain des rectangles sales sur une frame sur trois (`pause(willPaint)`
  desormais).
  Deux portails ne mordaient pas non plus : le test de separation de la palette
  sautait toute paire TOUCHANT le cyan neutre au lieu de cyan-contre-cyan, et le
  controle binaire A2.22 ne couvrait pas du tout le companion — `drawBlush`
  compris, reecrit pour cette regle precise le meme jour.
  Et le poids d'`intensify(rgb, t)` ne faisait rien : passant par `blendRgb888`
  il heritait du recalage sur le parent le plus lumineux, et ce parent est le
  blanc, donc chaque resultat etait epingle au maximum quel que soit `t`, et
  `t = 0` n'etait pas l'identite. Trois emotions avaient ete reglees par un
  bouton inexistant. Devenu un lerp simple ; `Suspicious` a du etre retrouve
  contre le portail (0,20 -> 0,45).
- **Le renderer ne pousse plus que ce qui a CHANGE, et la frame passe de 26 ms a
  4-11 ms** (08-03, mesure sur cible). Le diagnostic etait que `pushSprite`
  transporte 320x160 px vers un panneau 16 bpp = 102 400 octets sur SPI2 a
  40 MHz = 20,5 ms de pur temps de fil, alors que TOUT le code de dessin reuni
  pese ~3,5 ms - le seul levier du bon ordre etait donc le nombre de pixels mis
  sur le fil, pas la geometrie. Un tampon fantome en PSRAM est compare bande par
  bande apres le dessin et seuls les segments differents sont pousses
  (`engine/DirtyBands.h`, teste nativement). Mesure sur le robot : **1 600 a
  14 400 pixels sur 51 200**, frame moyenne 3 900 a 10 800 us contre 26 062
  avant. La comparaison porte sur des PIXELS DEJA RENDUS, jamais sur des valeurs
  de canal - c'est ce qui la met hors d'atteinte d'A2.15, qui reserve toute
  decision de lissage au Brain. Le fantome est invalide partout ou un tiers
  peint dans la zone des yeux (launcher, SD-Updater, ecran occupe), sur le
  modele de l'invalidation du bandeau, et retombe sur la poussee complete si la
  PSRAM manque.
- **Le bandeau d'etat a cesse de scintiller** (08-03, utilisateur : « ca
  scintille, parfois ca clignote »). Deux defauts, et le commentaire qui y
  siegeait n'en reconnaissait aucun - il disait « redessin limite + seulement
  quand une valeur a change », et il n'y avait AUCUNE detection de changement.
  Un commentaire qui affirme un garde-fou que personne n'a ecrit est pire que
  pas de commentaire : il empeche le lecteur suivant de regarder. Chaque ligne
  de jauge etait EFFACEE EN NOIR puis redessinee directement sur la dalle, cinq
  fois par seconde ; contrairement a la zone des yeux - qui rend dans un canvas
  et pousse une fois - le trou noir etait reellement a l'ecran. Les lignes sont
  desormais rendues dans un sprite 320x15 pousse d'un coup, et une ligne dont la
  valeur, l'etiquette et le texte de droite n'ont pas bouge n'est pas touchee.
  Etage bandeau : **2 369 us -> 248-716 us** par frame.
- **Les trois routes de listage ont quitte le callback AsyncTCP** (08-03).
  `/api/bins`, `/api/sd/list` et `/api/sd/get` lisaient la carte dans le
  callback - E/S bloquante la ou A2.6 n'autorise que `post()` et l'ecriture de
  Tuning, sur le bus SPI2 que l'ecran partage. Elles ne pouvaient pas etre
  differees comme une ecriture, le contenu de la carte ETANT leur corps de
  reponse, ni emprunter `renderer.pause()` : il attend l'accuse de fin de frame,
  jusqu'a 500 ms de `vTaskDelay`, et bloquer la tache AsyncTCP un tiers de
  seconde est pire que la contention. La requete est donc GAREE, `loop()`
  construit le corps en PSRAM avec le renderer en pause, et la reponse est
  remplie par un memcpy cote AsyncTCP. Un seul travail a la fois : un second
  listage concurrent est refuse par un 503 propre plutot que de corrompre le
  premier. Un client qui se deconnecte en vol est rattrape par un pointeur
  faible sur la requete et un drapeau de deconnexion, et le bloc PSRAM
  appartient a la reponse, donc il est libere exactement une fois dans les deux
  cas.
  Un bug trouve sur cible et non a la lecture : `sendPsBody(req, buf.release(),
  buf.len)` - les deux arguments sont INDETERMINEMENT SEQUENCES en C++, GCC a
  evalue `release()` d'abord, ce qui met `len` a zero, et l'endpoint repondait
  HTTP 200 avec les bons en-tetes, sans Content-Length et avec un corps VIDE. Ca
  ressemblait exactement a un defaut de carte et c'etait une regle de
  sequencement.
- **La palette d'emotions est un SYSTEME, pas trente choix** (08-03, images de
  reference dans `docs/assets/insideout - *.png`). C'etaient douze valeurs
  choisies a la main et regroupees par intuition, et le regroupement se voyait :
  Happy, Glee, Excited et Smug etaient le MEME jaune, donc quatre visages tres
  differents allumaient les LEDs a l'identique, pendant que Surprised et Awe
  etaient blancs.
  **Inside Out** de Disney/Pixar avait deja resolu cette forme - un petit jeu de
  sentiments de base a la teinte sans equivoque, et un tableau publie de ce que
  donne chaque PAIRE. Neuf bases (cinq du premier film, quatre du second) et des
  melanges CALCULES : une paire qui sonne faux se corrige en deplacant UNE base,
  tous ses melanges suivent. Le tableau retenu est le DIRECTIONNEL : la ligne
  mene, la colonne suit, donc Joie+Degout donne "disdain" quand la joie mene et
  "ironic" quand le degout mene - exactement ce qu'exprime le poids du melange,
  la ou le tableau symetrique l'aurait gaspille.
  **C'est le fond noir qui a dicte l'ingenierie.** Melanger deux teintes
  saturees en sRVB fait DEUX degats : ca assombrit ET ca delave. Ne rattraper
  que la luminosite laissait Frustrated, Scary et Blush en trois roses voisins.
  Le melange restaure desormais la SATURATION des parents avant de recuperer la
  lumiere - la teinte vient du melange, le mordant et l'energie des parents.
  Deux portails natifs le verrouillent (`test/test_emocolor`) et les deux ont
  mordu pendant l'ecriture : **contraste >= 3:1 sur noir** apres l'attenuation
  par defaut a attrape Sleepy a 2,99:1 - la DEUXIEME fois que cette emotion
  precise doit etre eclaircie pour cette raison precise, l'ancienne palette
  portant deja la note "un violet plus sombre devient illisible une fois
  attenue" - et **aucune couleur a moins de 32 d'une autre** a attrape Doubt a
  0x8889FF contre Dead a 0x8899FF, seize unites sur un seul canal. Les poids ont
  ete CHERCHES, pas choisis : le reglage a la main ne converge pas (chaque
  correction pousse une couleur dans sa voisine), donc la part libre a ete
  choisie en maximisant la plus petite distance entre deux des trente sous la
  contrainte de contraste. Ca plafonne a ~34, et c'est un plafond - trente
  couleurs dans un gamut fini, dont la moitie tirees vers le blanc pour survivre
  au noir.
  Quatre exemptions DECLAREES, chacune avec sa raison, parce que la couleur
  n'est pas le seul canal : Happy/Glee/Excited partagent un ambre (le sourire
  simple, le rictus serre et les yeux ecarquilles a etoile sont deja trois
  visages sans equivoque - ce qui inverse volontairement le constat ayant ouvert
  la reecriture), Surprised et Awe sont blancs (etre surpris n'est pas une sorte
  de joie), Angry et Furious sont le meme rouge vivifie plutot qu'eclairci (la
  fureur est la colere avec le volume monte, et le melange vers le blanc l'aurait
  fait deriver vers le rose), et **Dead passe a 2,3:1** - un GRIS fonce, et le
  gris est le propos : les vingt-neuf autres portent une teinte, donc le seul
  visage qui n'est pas un sentiment est celui qui n'en a aucune, et ne peut pas
  se lire comme une version calme de quoi que ce soit. Un "mort" qui brille
  autant que Happy se contredirait.
  Le robot GARDE SON CYAN sur Normal, Focused et Squint : Inside Out n'a pas de
  personnage pour "rien de particulier", et ce robot y passe l'essentiel de sa
  vie. Emprunter un systeme n'est pas ceder le visage.
- **Dead est immobile, Sleepy est plaque** (08-03). Regard, VOR, respiration et
  squash sont geles pour Dead au point UNIQUE de publication de l'etat du
  visage - dans le Brain, pas dans le Renderer : les regles 3 et A2.15 mettent
  tous les canaux continus dans les mains du Brain, et un Renderer qui decide
  seul d'ignorer un canal pour une emotion est le bug que ces regles previennent.
  La tete garde sa choregraphie de redressement agonisant ; ce qui s'arrete, ce
  sont les yeux. Sleepy repose a 20 px du plancher de la zone et ne gele que ses
  canaux VERTICAUX - y du regard, respiration et squashY, ce dernier parce qu'un
  ecrasement vertical deplace les deux bords meme quand le centre ne bouge pas.
  Le regard lateral fonctionne toujours : un visage endormi qui ne bouge plus du
  tout est un mort, et la table en a deja un.
  Les deux INVERSENT des demandes anterieures enregistrees (`VALIDATION.md` :
  "Dead : la croix suit le regard" ; le verdict du 2026-07-12 sur l'alignement
  bas de Sleepy). Les deux sont consignees comme superseded plutot que
  discretement abandonnees - une valeur de preset qui porte le nom d'un
  utilisateur n'est pas un parametre libre.

- **VOR** (réflexe vestibulo-oculaire) : contre-rotation gyro directe, biais
  appris immobile, zone morte, dérive complémentaire gated, saccades de
  rattrapage vers la cible d'inclinaison, copie d'efférence servo ; mapping
  capteur→écran réglable à chaud (`gyro_*`, validé HW) ; secousse (3 axes) →
  Scared ; PickupDetector → Curious (« pieds ballants », couple relâché).
  Réflexes PRÉEMPTIFS (coupent tout). **Interactions manuelles prioritaires**
  (A2.5) : swipe écran, caresse tête (Si12T press/avant/arrière) et émotion
  console/API coupent la danse en cours (AbortDance avant SetEmotion) — sinon
  ses keyframes ré-appliquent leur émotion et masquent la réaction.
- **Idle vivant** : fixation → saccade sèche → micro-overshoot + squash &
  stretch procédural ; BlinkController à politiques par expression + machine
  Sleepy « lutte contre le sommeil » ; blink « Cozmo » (deux yeux fermés = une
  ligne 1 px posée sur le bord bas).
- **Budget frame borné** (33 ms/30 Hz) : les rendus spéciaux (X « mort » Dead,
  étoile Excited, et **le blush depuis le 08-03** — l'affirmation était FAUSSE
  pour lui jusque-là, `drawBlush` traçait encore huit traits anti-aliasés à
  chaque frame de Blush, Glee et Smug ; débusqué par l'audit du budget frame,
  ce à quoi sert un audit) utilisent des primitives BON MARCHÉ (`fillTriangle`/
  `fillCircle`, scanline) et non `drawWideLine` (anti-aliasé, coûteux) — une
  frame > 33 ms affamerait le polling tactile de `loop()` (prio inférieure,
  même cœur 1) et gèlerait les swipes. Garde anti-famine durcie : frame en
  dépassement → 3 ticks de répit garantis pour loop(). Le X Dead est tamponné
  `fillCircle` en **une seule boucle à point d'appel unique** — contrainte
  VÉRIFIÉE SUR CIBLE : GCC 8.4 Xtensa supprime du binaire le second de deux
  appels de dessin similaires dans un même corps (bras / jamais dessiné,
  prouvé par relecture du buffer canvas + compteur d'appels série).
- **Le frame est MESURÉ PAR ÉTAGE** (08-01). Le heartbeat publiait un seul
  chiffre (`frame avg 27536us max 42066us`) — au-dessus du budget 33 ms sur les
  pics, et muet sur l'endroit où passait le temps. Il sépare désormais les deux
  étages qui composent le frame, `drawFrame` (les yeux) et `drawStatusBand` :
  `frame avg 26062us max 40231us (eyes 24097us band 1956us)`. Mesuré sur cible :
  **les yeux valent ~92 % du frame et le bandeau ~7 %**, donc tout travail sur
  le budget se situe dans `drawFrame` et nulle part ailleurs. Deux appels
  `esp_timer_get_time()` par frame, conservés en permanence — le coût est de
  ~1 µs et l'alternative est d'optimiser à l'aveugle.
- **30 expressions** (+ effets overlay `engine/EyeEffects.h` : joues rosées
  — style anime : 4 traits fins parallèles de longueurs
  différentes, quasi verticaux, en miroir sur chaque joue ; **calés sous le
  bord bas RÉEL des yeux et suivant le regard** (gaze + squash), écartés pour
  dépasser légèrement les bords des yeux —, sparkles, goutte de sueur) ; miroir
  aléatoire des asymétries décidé par le
  Brain et calé sur le sens des danses ; formes dynamiques (Sad « regard
  ciel », Curious « œil de bord ») ; équidistance des centres (`eye_spacing`).
- **15 danses** (Sequencer, keyframes unifiées servo+expression+paupières+
  regard, abort réflexe) + **chorégraphies CSV sur carte SD** (`DanceStore`).
- **Effet CRT** (LUT) activable à chaud ; **EmotionLeds** (PY32) + **SoundFx**
  chirps par valence, off par défaut.

## Tête / servos

- **Posture de tête par émotion** : home pitch 93°, `Brain::pitchBiasFor`
  (Sad/Sleepy/Blush baissent, Smug/Excited relèvent), biais conservé par le
  head-follow (activé par défaut). **Dead** relève la tête à un pitch raw **55**
  (agonie/gorge offerte) — sans tenir la butée (calage servo) ; montée SÈCHE
  400 ms (les autres émotions gardent le nudge doux 700 ms), ~2 s en haut.
- **Limites servo Y = SPEC OFFICIELLE M5Stack** : plage sûre 5–85° officiel =
  raw 19–99 (`PITCH_MIN`/`PITCH_MAX`, `raw ≈ 104 − officiel`). Tenir les
  butées cale le servo (dommage permanent) — jamais atteintes. Le biais tête-
  basse maximal atteignable est dérivé (`PITCH_DOWN_MAX = PITCH_MAX −
  PITCH_NEUTRAL`), jamais codé en dur, pour que toute recalibration se
  propage.
- **Retour au home généralisé** (`head_home_ms`) : après une pause sans
  activité de tête ni son, la tête revient au home. La cible est clampée à la
  plage servo atteignable → la condition de repos converge toujours (pas de
  re-commande servo en boucle).
- **`GET /api/servo/pos` — la pose MESURÉE** (08-02), par opposition à
  `/api/status` qui rapporte la CONSIGNE et ne peut jamais confirmer que le
  servo y est arrivé. Le bus SCS0009 est en **écriture seule en fonctionnement**
  — le lire entre deux `WritePos` les corrompt et les servos deviennent muets
  (essayé puis reverté le 2026-07-21) — donc la lecture est verrouillée sur
  `servos = 0`, où la tâche de mouvement n'écrit rien et le bus est libre. Elle
  répond `valid:false` sinon, plutôt qu'un chiffre périmé. L'échantillonnage vit
  dans `loop()` à 5 Hz, pas dans le callback AsyncTCP : une lecture série
  bloquante n'a rien à y faire (A2.6). Enregistré AVANT `/api/servo` (A2.19).
  Ce qu'il a établi, sur le matériel : commandé 166,0 → **mesuré 166,9**, et un
  tour complet à la main lit **0,3 → 300,0 puis décroche** — le potentiomètre
  n'a pas de piste sur les ~60° restants, ce qui explique qu'une position soit
  ambiguë après plusieurs tours et pourquoi `YAW_RANGE = 130` garde `[36, 296]`
  dans l'arc lisible.
- **En-tête de la console** : `horloge` (UTC synchronisée NTP, `n/a` avant la
  première synchro) et `nuit` (ce que SoundFx en déduit, avec `lat`/`lon`)
  rejoignent les chips, et **l'uptime s'écrit en unités lisibles** — `4j 18h`,
  `5h 12min`, `42min`, les secondes seulement sous la minute. Les deux plus
  grandes unités seulement : `4j 18h 32min 43s` oblige à chercher ce qui compte.
- **Télécommande servo** `POST /api/servo` (absolu/relatif) + auto-release du
  couple après inactivité.
- **Option `servos`** (0 = servos désactivés) : couple RELÂCHÉ (tête molle,
  manipulable à la main, zéro conso) + aucune écriture bus — VOR/danses/
  head-follow inertes côté physique, les yeux continuent ; le réflexe
  secousse→Scared reste ACTIF (isMoving ne publie pas de trajectoire
  gelée). Réactivation = **reprise douce** : la position PHYSIQUE est
  relue (`ReadPos`) et la trajectoire rebasée dessus — une tête déplacée à
  la main ne claque pas au retour du couple.

## Caméra (GC0308 → Home Assistant / Frigate)

- Option `camera` (off par défaut) : vue **live** `GET /api/camera/still.jpg`
  (compressée `cam_stream_quality`, **320×240** si `cam_stream_qvga=1` — ~4×
  moins d'octets radio, défaut), instantané **VGA pleine qualité**
  `?full=1` (capture à la demande, 503 tant que pas frais), **flux MJPEG**
  `GET /api/camera/stream` + état dans `/api/status` (`cam`, `camErr`) + vue
  live console + réglages capteur à chaud (`cam_fps`, `cam_quality`,
  `cam_brightness`, `cam_contrast`, `cam_saturation`, `cam_vflip`,
  `cam_hmirror`, `cam_lowlight`, `cam_colorbar`).
- HAL `hal/Camera.h` : capture + encodage JPEG sur **tâche dédiée** (cœur 0
  prio 1 — préemptée par servos/réseau : danses fluides et HTTP réactif
  pendant le flux), JPEG persistant en **PSRAM** (le heap interne ne s'épuise
  plus sous flux), init à la demande, **auto-extinction après 60 s sans
  consommateur**, échec gracieux (rail capteur coupé), capture **inhibée
  pendant les flash** (OTA, upload/lancement `.bin`). Cadence asservie à la
  consommation ; `camera=0` coupe capture, vue console ET flux ouverts.
- **Init fiable** : power-cycle ALDO3 robuste (le GC0308 n'a ni RESET ni PWDN
  câblés), rafale SCCB exclusive (verrou bus), reset du frame-gate au deinit —
  les cycles `camera=0→1` relivrent des frames sans reboot.
- Obstacles matériels résolus (détail `docs/integrations/HOMEASSISTANT.md §Caméra`) : SCCB
  routé via `M5.In_I2C` (override `SCCB_*`, `sccb_m5.cpp` — sous le verrou de
  bus), frames partielles (pause renderer pendant le DMA), YUV422 + `frame2jpg`
  (pas de JPEG matériel — qualité `cam_quality` mappée correctement sur
  l'encodeur logiciel), table d'init officielle Espressif `esp_cam_sensor`
  (réglages en écritures registre directes).

## Bande de statut & plugins (contrat `sources → champs → {widgets, règles}`)

- **Blackboard** (`engine/FieldStore.h`) : magasin de champs nommés (float +
  chaîne), source UNIQUE de l'affichage bas d'écran ET des réactions. Toute
  source (API `POST /api/field`, capteurs, scripts, futur BLE) écrit des
  champs ; personne ne câble de chemin dédié.
- **Bande de statut** (bas d'écran, `Renderer::drawStatusBand`, dessinée par le
  renderer — règle 1) : modes de la zone dynamique (`POST /api/statusbar` —
  aucune / vu-mètre stéréo / jauges), **persistés** (`band_mode`, survit au
  reboot) + rangée d'icônes **masquables**
  (`icon_mask` — batterie, WiFi, caméra=point rouge, micro, nuit ; couleurs
  **indépendantes des yeux**, batterie verte en charge) + **infos debug
  `émotion·ip` en option** (`band_debug`, centrées dans la rangée d'icônes, plus
  un mode) + notifications `POST /api/say` (texte long → **défilement marquee**
  réglable `band_scroll_speed`, taille `band_text_size`, tampon 160 car., zone
  purgée avant/après) et alerte batterie (prioritaires). Jauges à couleur
  propre par ligne. **Swipe horizontal dans la zone de la bande** (y ≥ 160)
  fait défiler les modes (gauche = suivant, droite = précédent, persisté —
  coalescé : une rafale de swipes = une seule écriture SD) ; au-dessus, le
  swipe L/R défile les émotions ; les **swipes verticaux dans la bande sont
  inertes** (pas de danse/launcher sur un geste diagonal). `band_mode` est
  **validé** par loop() ({0,2,3} — une valeur héritée invalide, ex. l'ex-mode
  debug 1, est ramenée à 0). Change-detection par région.
  Référence dédiée : **`docs/reference/STATUSBAR.md`**.
- **Moteur de règles** (`behavior/RuleEngine.h`, PUR, testé) : règles
  déclaratives `champ → Commande` whitelistée (ou `→ champ`), sémantique
  « front tenu » (sustain + cooldown + gate). **`dark_sleepy` est une règle
  déclarative** (pas de machine d'état ad-hoc). Les réflexes
  restent prioritaires (A2.5).
- **`POST /api/bins/launch` n'échoue plus en silence** (08-01) : le test de
  taille `sz > 0 && sz <= partition` n'avait pas d'`else`, donc une taille nulle
  ne faisait RIEN — ni flash, ni message — alors que l'API avait déjà répondu
  « flash + reboot dans ~2 s ». L'appelant recevait le CONTRAIRE de ce qui se
  passait, la pire des trois réponses possibles. Les deux refus disent
  maintenant pourquoi, et l'ouverture est RÉESSAYÉE trois fois : juste après un
  redémarrage ou un gros upload, la carte répond réellement 0 pour un fichier
  qui est là (observé trois fois en déployant le bin invité), et se rétablit
  quelques centaines de ms plus tard. La lecture est encadrée par
  pause()/resume() du renderer (A2.16).
- **La console est servie COMPRESSÉE** (08-01) : 65 563 o sur le fil deviennent
  22 857 o, et le firmware MAIGRIT de 51 468 o parce que les octets compressés
  remplacent les clairs en flash au lieu de s'ajouter à eux — `WebApi.h`
  n'inclut plus `WebConsole.h`, donc les littéraux clairs ne sont jamais émis
  (sans dépendre de `--gc-sections`). Généré par un script `pre:` PlatformIO
  pour qu'on ne puisse pas l'oublier, stable à l'octet (`mtime=0`) pour qu'une
  console inchangée ne déclenche aucune recompilation, et le tableau est
  gitignoré. Le temps de compilation a BAISSÉ (15,6 s -> 10,6 s : un tableau de
  22 Ko s'analyse plus vite que 77 Ko de littéraux bruts). La remorque de
  langue ne pouvait pas survivre à un flux gzip : la langue voyage désormais en
  `Set-Cookie` sur la réponse même qui porte la page — stockée pendant le
  traitement des en-têtes, donc AVANT que l'analyseur n'atteigne le script :
  aucun aller-retour de plus, aucun clignotement dans la mauvaise langue.
- **La nuit suit le SOLEIL, plus une fenêtre d'horloge** (08-01,
  `firmware/common/SunClock.h`, 15 tests natifs) : le thème Nuit du
  flight-radar utilisait un 21 h -> 7 h fixe, et à La Réunion le coucher bouge
  de 1 h 13 dans l'année — en juin l'écran restait clair plus de trois heures
  après la tombée du jour. Position solaire NOAA depuis la latitude et la
  longitude PROPRES au radar : juste là où le bin tourne, pas seulement ici.
  Un écart assumé avec la forme NOAA publiée, et il vaut 4 minutes à Paris :
  mesurer la phase solaire en années TROPIQUES depuis 2000 au lieu de la
  remettre à zéro chaque 1er janvier, ce qui dérive d'un quart de jour par an
  et se réinitialise les années bissextiles.
- **Plugins réactifs SD** (`app/RuleStore.h`, `/stackchan-companion/rules.txt`,
  hot-reload `POST /api/rules/reload`) : la communauté ajoute des comportements
  (Claude-buddy, automatisations) en éditant un fichier — mêmes conventions que
  les danses CSV. Détail : `docs/reference/PLUGINS.md`.
- **Bin invité `ha-remote`** (`firmware/ha-remote/`, env dédié) :
  télécommande Home Assistant — le robot est CLIENT de votre domotique
  (l'inverse de `docs/integrations/HOMEASSISTANT.md`). Écran d'accueil par catégories
  (volets, lumières, prises, caméras) avec effectifs, écran de liste à
  une entité (swipe ←/→), actions par domaine, vignettes caméra via
  `/api/camera_proxy`. **Réglages continus** : position d'un volet
  (mi-course comprise), puissance / température de blanc / couleur d'une
  lampe — les contrôles suivent les CAPACITÉS déclarées par
  `supported_color_modes`, jamais la présence d'une valeur (une lampe
  éteinte cesse de publier sa luminosité). Les curseurs suivent le doigt
  en local et n'appellent le service qu'**au relâchement**. **Retour quasi
  temps réel** à deux cadences : l'entité affichée est sondée seule
  (`/api/states/<id>`) à 1 Hz, et à 400 ms pendant les 8 s qui suivent une
  action — le catalogue complet reste à `poll_s`. Pas d'action « tout
  fermer » maison : un GROUPE Home Assistant est déjà une entité `cover.*`
  ordinaire dans la liste. Documents JSON filtrés et alloués en PSRAM (le
  heap interne est partagé avec WiFi/TLS), 64 entités max. Jeton longue
  durée sur SD, saisi depuis la page web du bin en champ **`Secret`**.
  **Écrans calqués sur l'usage** : accueil à quatre cartes avec icône et
  compteur *actifs/total* (`3/6`) plus l'état de la liaison ; volet commandé
  comme il bouge (tablier vertical glissable, `OUVRIR`/`STOP`/`FERMER`
  jointifs sur toute la hauteur) ; lampe à disposition **adaptative** selon
  ses capacités, pastilles 34×30 et **bouton d'alimentation unique** qui
  propose la seule action utile ; caméra **plein écran** rafraîchie
  automatiquement, JPEG **et PNG**, mise à l'échelle depuis les dimensions
  lues dans l'en-tête. Panneau de réglages au **glissé droite**, même geste
  que `flight-radar`.
  Détail : `docs/guests/HA-REMOTE.md`.
- **`ha-remote` — QUELLES 64 entités survivent** (08-03). La table en tient 64
  et une installation réelle en sert plusieurs centaines ; elle était remplie
  dans l'ordre où `/api/states` les servait, donc ce qui manquait était les
  *dernières* entités servies, pas les moins utiles. Une installation servant
  quatre cents `light.*` avant son premier `cover.*` affichait **zéro volet** —
  et l'accueil annonçait ce zéro avec l'aplomb d'un vrai. Le choix est désormais
  explicite et PUR (`firmware/ha-remote/entsel.h`) : les entités épinglées par
  l'utilisateur (clé `entities:`, comparaison en PRÉFIXE, donc `cover.` épingle
  tout un domaine) et **l'entité affichée à l'écran**, puis celles qui répondent
  avant les `unavailable`, puis une part égale entre les quatre catégories avec
  redistribution des restes, l'ordre de Home Assistant départageant à
  l'intérieur. Testé nativement — `test/test_entsel`, 22 cas (17 suites /
  266 tests au total).
  L'autre moitié du correctif : **la troncature a cessé d'être silencieuse.**
  L'accueil affiche `64 sur 213`, chaque carte porte son `+n`, l'en-tête de
  liste aussi, une catégorie vidée dit `9 non chargees - liste trop longue` au
  lieu de « aucune entité dans cette catégorie », et le panneau de réglages
  énonce le manque à côté du bouton ACTUALISER. Un plafond qui tronque en
  silence se lit comme « tout est là ».
  Un bug trouvé en chemin : `curId` est une TRONCATURE à 47 caractères de
  l'identifiant, comparée par `strcmp` à l'identifiant complet du JSON — donc
  toute entité dont l'identifiant atteignait 48 caractères ne se reconnaissait
  jamais elle-même, et la seule entité que l'épinglage existe pour protéger
  était précisément celle qu'il ne pouvait pas protéger.
- **Le cou est réellement relâché quand un bin invité prend la main** (08-02),
  et il aura fallu TROIS couches, chacune couvrant une fenêtre que les autres
  ne peuvent pas :
  1. **Repos à 4 s** (au lieu de 15) pendant la vie du companion. Quinze était
     juste pour un mouvement *commandé* et faux pour les ajustements de posture
     autonomes qui arrivent en boucle : chaque émotion porte un biais de pitch,
     le retour au home réengage le couple pour le suivre, et le compteur repart
     — avec la roulette, le cou restait tenu en permanence. Le repos se compte
     désormais depuis la FIN de la trajectoire, plus depuis son début.
  2. **Un relâchement acquitté aux deux portes** vers un invité (`POST
     /api/bins/launch` et le launcher tactile). `releaseTorque()` ne fait que
     poster une requête ; les deux chemins avaient plusieurs secondes de travail
     avant le flash, donc cela passait par accident de calendrier, pas par
     garantie. On l'attend maintenant, borné à 400 ms, et on part quand même à
     l'expiration — même forme que l'ACK de parcage de netTask.
  3. **Le rail VM_EN est coupé.** Le point décisif : `EnableTorque(id,0)` écrit
     un registre INTERNE au SCS0009, et ces servos démarrent couple ACTIVÉ. Le
     redémarrage qui lance l'invité réasserte VM_EN et efface le relâchement
     qu'on venait de faire acquitter — molle sous le companion, verrouillée à
     nouveau sous l'invité. Le PY32 conserve son GPIO au travers de notre reset :
     couper le rail est la seule libération qu'un reboot ne peut pas défaire.
     Couple d'abord, rail ensuite : couper l'alimentation d'un servo sous
     contrainte, c'est la secousse que ce projet passe son temps à supprimer.
     Personne n'est sans recours — le companion rallume le rail au démarrage, et
     un invité qui pilote le cou le fait dans son propre init.
- **`POST /api/bins/launch` relâche le couple servo avant de remettre
  la machine** (08-02). Le launcher INTERACTIF le faisait déjà (« couple relâché
  = silence mécanique ») ; le chemin API non, alors que les deux aboutissent au
  même endroit — un firmware invité qui tourne sur cette carte. Un invité qui
  n'utilise pas les servos ne touche jamais au bus : il héritait donc de ce que
  nous laissions, tête verrouillée, tiède et immobilisable à la main, sans que
  rien sur le robot puisse l'expliquer. Le couple est un état PHYSIQUE qui
  survit à notre processus (les registres SCS le gardent au travers d'un
  redémarrage) : le relâcher relève de « nous partons », pas de « l'utilisateur
  a fait un geste ».
- **TAF : la barre « en vigueur » couvre le BULLETIN, pas seulement ses groupes
  de changement** (08-02). La ligne des conditions dominantes ne porte aucune
  plage `DDHH/DDHH` — la validité est dans l'en-tête, que le corps saute — donc
  elle ne pouvait jamais être marquée, et sitôt un TEMPO terminé l'écran
  n'affichait plus aucune barre et se lisait « rien n'est en vigueur ».
  Comparaison de plage factorisée dans `fr::ddhhRangeCovers`, partagée au lieu
  d'être recopiée, avec 5 tests natifs dont le bulletin FMEE réel. Aussi :
  `metar_icao` vaut désormais `FMEE` par DÉFAUT, en accord avec les coordonnées
  Réunion intégrées (vide + `airport` vide = aucune station, donc ni METAR ni
  TAF, sur toute carte sans SD) ; un METAR en échec est JOURNALISÉ (seul le
  succès imprimait, si bien qu'une requête ratée et une requête jamais tentée
  se ressemblaient) ; et le heartbeat porte `clock:`, parce qu'une carte sans
  RTC — le Fire — ne peut pas être interrogée autrement sur la synchronisation
  SNTP, dont dépendent en silence la barre en vigueur, le thème Nuit et les
  heures locales.
- **Un bandeau de notification est désormais une BARRE PLEINE** (08-03) : la
  couleur du message devient le fond, le **texte passe en noir, centré** — au
  lieu du texte coloré sur noir de tout ce qui l'entoure. Sur des écrans où
  chaque valeur est déjà codée par la couleur, une ligne colorée de plus parmi
  quarante se lisait comme une *donnée* et non comme un *événement*, et inverser
  le fond est le seul geste qu'on ne peut pas confondre avec une légende. Les
  Sur les écrans de lecture il couvre la bande brute EN ENTIER — ses 24 px,
  texte centré sur les deux axes. Une première version n'en faisait qu'une ligne
  de 8 px, ce qui ressemblait à un bug et en était un : le METAR y écrit jusqu'à
  TROIS lignes brutes, les deux autres restaient donc à côté du bandeau et se
  lisaient comme des pixels qu'il aurait oublié d'effacer. Et quand la fenêtre
  d'un bandeau se referme, la bande revient **à la milliseconde** : les messages
  persistants gardent la frame propre à dessein (un message de 5 minutes
  redessiné au rythme de la boucle, ce sont les 25 % de `loop()` que coûtait
  l'ancien redessin à 8 fps), donc rien ne guettait leur fin — la barre restait
  sur le texte brut jusqu'à ce que le tic de 1 s redessine. Chaque message à
  fenêtre publie désormais sa date de péremption et `loop()` arme exactement un
  redessin dessus. Les **indications** du radar restent du texte simple — une indication est du mobilier permanent, et une barre colorée
  permanente est une alarme permanente. Le noir sur les huit fonds possibles
  (`accent` et `alert` × quatre thèmes) est verrouillé par
  `scripts/gates/check-contrast.py` au seuil texte WCAG AA ; le plus juste est le rouge
  pur de Scope nuit à 5,25:1. A2.22 tient : l'assistant partagé peint le fond et
  choisit l'encre, il n'emporte **pas** le `drawString` — cela aurait vidé les
  deux symboles surveillés par `check-a222.py`, faisant taire le portail au lieu
  de le satisfaire.
- **La présence de la carte SD redevient une MESURE, plus un verdict de
  démarrage** (08-03). `board.hasSD()` était tranché une fois au boot et cru
  pour tout le reste : retirer la carte laissait `/api/status` répondre `sd:1`,
  la console proposer des fichiers disparus, et un envoi de 1,7 Mo écrire dans
  un montage périmé — gel du renderer de 3,4 s sur le timeout, et un
  `import failed` qui se lit comme un défaut de carte plutôt que comme une carte
  absente. Trouvé en le faisant : la carte a été déplacée vers l'autre carte en
  pleine session. `loop()` resonde désormais toutes les 3 s (une ouverture de
  répertoire, renderer en pause autour et UNIQUEMENT autour), et tant que la
  carte est absente le même tic tente un remontage complet `end()` + `begin()` —
  un drapeau qui ne sait que passer à faux rendrait le premier retrait définitif
  jusqu'au prochain démarrage, et une sonde qui se contenterait de regarder
  annoncerait une carte sur laquelle on ne peut toujours pas écrire. Un
  changement d'état resynchronise ce qui en dépendait : le champ `sd`, le cache
  de noms `/bins/`, les danses.
- **Un enregistrement de réglages ne coûte plus un jeton autorouter** (08-03).
  `settingSet` est appelée pour chaque champ que le formulaire POSTE, pas pour
  ceux qui ont bougé, et `notam_user` est un champ Text que le navigateur
  renvoie à chaque enregistrement — changer la LUMINOSITÉ lâchait donc le
  porteur en cache, la requête NOTAM suivante se réauthentifiait, et cela frappe
  un jeton sur une réserve de vingt par semaine. Le jeton survivait aux
  redémarrages et aux coupures, comme prévu, et mourait d'un enregistrement sans
  rapport. Les deux champs d'identifiants n'agissent plus que sur un changement
  RÉEL. Son jumeau `notam_pass` ne montrait pas le défaut : un champ Secret
  laissé vide signifie « inchangé » et n'atteint jamais le gestionnaire — c'est
  exactement ainsi qu'une branche sur deux identiques reste invisible.
- **Une carte sans SD le DIT désormais, et le stockage NVS du jeton disparaît**
  (08-03). Le repli semblait juste — flash interne, donc survivant au
  redémarrage comme à la coupure, la réponse au boot-loop capable de brûler
  vingt jetons autorouter en quelques minutes. **Il était inatteignable.**
  `notam_user`/`notam_pass` sont des réglages, les réglages vivent dans le yaml,
  il n'y a pas de yaml sans carte : après tout redémarrage ils sont vides et
  `fetchNotam` sort en `HTTP_NO_KEY` *avant* de regarder le porteur. Un jeton
  stocké que rien ne peut présenter n'est pas un cache, c'est un identifiant qui
  dort en flash pour personne. Ce que coûte une carte absente, ce n'est pas un
  jeton, c'est toute la configuration — y compris les identifiants WiFi, qui
  viennent des drapeaux de build et ne peuvent pas non plus être changés sans
  carte.
  Démarrer sans carte lève donc un avertissement plein écran, et **il attend
  d'être acquitté au lieu de décompter** : un avertissement de six secondes est
  un avertissement qu'on rate, alors que la condition, elle, n'expire pas. Deux
  sorties — `A` / moitié gauche remonte la carte qu'on vient d'insérer (et relit
  alors la configuration pour de bon : le yaml a été analysé sans carte et n'a
  rien chargé, s'en dispenser ferait du réessai un mensonge), `B`/`C` / moitié
  droite continue sans. Un réessai qui ne trouve rien le dit et reste en place.
  Le compromis est énoncé, pas caché : une coupure sans personne devant attend
  un appui. Ce qui maintient l'avertissement ensuite, c'est le pied du radar,
  qui affiche `pas de microSD : reglages non enregistres` à la place de
  l'indication de traque tant qu'il n'y a pas de carte — l'indication s'apprend
  une fois, « rien de ce que vous saisissez n'est enregistré » ne cesse jamais
  d'être vrai.
- **`flight-radar` tourne aussi en autonome sur M5Stack Fire**
  (`pio run -e flight-radar-fire`, 08-02) — même source, pas de fork. Les
  différences de carte sont des *drapeaux de capacité* déclarés dans un unique
  bloc `BOARD PROFILE` (`SCE_INPUT_BUTTONS`, `SCE_HAS_SERVO`,
  `SCE_HAS_LTR553`, `SCE_COMPANION`, `SCE_SD_*`), nommés d'après ce que la
  carte **possède** plutôt que d'après une carte. L'obstacle n'a jamais été le
  matériel K151 — c'est que le Fire a **trois boutons et pas de dalle
  tactile**, quand toute la navigation était gestuelle. Les actions ont donc
  reçu des noms : `firmware/flight-radar/input.h` porte un vocabulaire
  `UiEvent` pur et une machine à états boutons testée nativement
  (`test/test_input`, 14 cas, dont le fait qu'un appui long ne doit pas émettre
  aussi un appui court au relâchement) ; le tactile et les boutons sont deux
  **producteurs**, `applyEvent()` l'unique **consommateur**. Les deux modales
  tactiles (clavier 8×5, panneau à curseurs) ne sont pas portées mais
  **remplacées** par `http://<ip>/config`, qui gagne le seul champ qui lui
  manquait — `track` — partageant le chemin de validation exact du clavier
  plutôt qu'une seconde copie. Pas de portage sur Core Basic : aucune PSRAM,
  dont dépendent le canvas de 150 Ko et la règle 18.
  ⚠ Les deux cartes branchées, `scripts/dev/find-port.ps1` résout le port par
  **VID/PID** et refuse en cas d'ambiguïté — un `-t upload` nu écraserait le
  companion du StackChan.
- **Bin invité de démo `flight-radar`** (`firmware/flight-radar/`, env
  PlatformIO dédié) : radar d'avions temps réel ADS-B (airplanes.live par
  défaut, adsb.lol/adsb.fi compatibles, sans clé) — radar à gauche,
  **panneau d'infos du vol traqué à droite**, blips orientés au cap,
  couleur par altitude. **Tap sur un avion (blip OU callsign) = TRAQUE**
  (ré-accrochage auto par callsign, traîne locale, route via hexdb.io
  avec REPLI adsbdb.com — bases différentes, villes + aéroports en un
  appel —, DÉPART~/ARRIVÉE~ estimés en heure locale NTP — échec réseau
  RETENTÉ jusqu'à 5× avec backoff 8 s, statut route AFFICHÉ sur le
  panneau au lieu d'un « ? » muet, blip ciblé avant son callsign armé au
  poll suivant, diag 07-27). Les trois écrans forment une **PILE** verticale —
  radar, METAR, NOTAM — où un swipe vertical déplace d'UN cran et ne bascule
  jamais : l'état est un `viewLevel`, pas un booléen par vue. Un quatrième
  cran, le **TAF**, s'intercale entre le METAR et le NOTAM (07-31) et ne coûte
  AUCUNE connexion supplémentaire — `taf=1` sur la requête que le METAR fait
  déjà renvoie la prévision dans le même objet. Il est affiché BRUT,
  délibérément : un TAF est une suite de groupes conditionnels aux périodes de
  validité qui se recouvrent, le gloser reviendrait à prévoir à la place du
  pilote — les groupes sont donc MIS EN PAGE, une nouvelle ligne à chaque
  BECMG/TEMPO/PROB/FM (un PROBnn reste collé au changement qu'il qualifie) et
  la TAILLE DU TEXTE est DÉRIVÉE : taille 2 quand toute la prévision tient en
  25 caractères × 11 lignes, taille 1 sinon — rien n'est jamais coupé pour
  garder de grosses lettres. L'ordre de la pile PORTE le sens : ce qui se passe, ce
  qui est mesuré, ce qui est prévu, ce qui est hors service.
  Les trois écrans de LECTURE partagent leur habillage (07-31) : en-tête
  (ICAO · nom de station · **nom d'écran** facultatif) vient d'UNE
  implémentation, ils ne peuvent donc plus diverger — chacun s'était fabriqué sa première ligne, son pas et ses
  marges, invisible tant que des gestes distincts y menaient, flagrant dès
  qu'un seul swipe en a fait un cycle. Règle de fonte : le texte reçu verbatim
  en Font0 (pas fixe — un code ne doit jamais se recomposer), notre prose en
  efontJA_12. Il n'y a AUCUN pied de navigation : il coûtait une bande de 12 px
  sur chaque écran pour dire ce qu'un swipe apprend une fois, et ces pixels
  sont passés dans la rose (rayon 78 -> 82). Le METAR ne porte pas non plus de
  nom d'écran — la rose EST son nom — donc le nom de station redevient aligné à
  droite. Les chiffres du vent ont quitté la rose pour le haut de la marge
  DROITE, toujours dans l'accent pour que la couleur les relie à la flèche :
  cette ligne enfreignant la règle de colonne deux fois, à dessein : son
  étiquette est dans l'accent parce qu'elle PORTE un chiffre au lieu d'en
  nommer un, et sa valeur garde la taille 2 jusqu'à 7 caractères pour que le
  vent ne soit pas le petit nombre d'une carte qui parle du vent. Sur le
  pourtour ils perçaient deux trous opaques dans les graduations et
  changeaient de place à chaque observation, la rose ne se lisait jamais comme
  un seul objet. La piste a perdu ses traits d'extrémité (un rectangle fermé
  est une boîte, pas une piste) et son axe est devenu POINTILLÉ et atténué,
  tracé PAR-DESSUS le remplissage — un marquage routier, courant sur toute la
  longueur de la piste au lieu de s'arrêter là où elle commençait. La rose est
  désormais une PILE explicite de cinq couches, chacune répondant à celle du
  dessous : remplissage+bords de piste (le terrain), axe en pointillé, PUIS les
  graduations et étiquettes, puis les NUMÉROS de piste (par-dessus les traits — un numéro coupé par son propre marquage est
  la seule ambiguïté que cette carte ne peut pas se permettre), puis le vent EN
  DERNIER par-dessus toute la piste, parce que le vent est la mesure qu'on
  compare À la piste et ne doit jamais être ce qu'on masque. Les quatre forment
  un **ANNEAU parcouru par le seul swipe HAUT** ; le swipe BAS n'est **le nôtre
  à aucun cran** et veut toujours dire « retour au companion » (07-31). Une
  version antérieure n'armait cette sortie qu'au cran du bas et se servait du
  bas comme « un cran en arrière » au-dessus : ça marchait, mais notre
  navigation et le contrat invité partageaient une entrée, et ce contrat n'est
  pas à nous de plier. La sortie du companion n'est
  armée qu'au **cran du bas** (un swipe bas long est justement le geste qui
  signifie « un cran plus bas » au-dessus, laissé armé il aurait ouvert le
  dialogue de reflash au lieu de redescendre — raison pour laquelle le cycle ne
  doit pas se refermer vers le bas). Au-dessus du radar,
  tout tap y ramène directement. Swipe haut depuis le radar = **METAR de la
  station**
  (aviationweather.gov, libre, sans clé), où la **ROSE DES VENTS EST
  l'affichage** : centrée sur (160,114), rayon 78 — c'est l'anneau de la
  flèche, pas le disque, qui tient dans les 180 px laissés entre le titre et
  le bulletin brut. 36 graduations et douze étiquettes en dizaines de degrés,
  la piste dessinée en travers avec ses deux numéros — ses deux longs BORDS
  SURFACE ET BORDS courant sur toute la longueur et dépassant le pourtour de
  quelques pixels (07-31) comme la flèche de vent sort du disque, sans trait
  d'extrémité parce qu'un rectangle fermé est une boîte et non une piste (et la bande est passée SOUS la rose pour
  que graduations et étiquettes se tracent par-dessus — une bande pleine
  dessinée au-dessus avalait les étiquettes de degrés qu'elle traversait, ce
  qu'elle fait par construction dès que la piste pointe vers une trentaine) ; les numéros ont perdu leur plaque claire
  le même jour (le fond de la plaque est devenu la couleur des chiffres) — PLUS
  son axe prolongé de bord à bord (lire le cap de la piste sur les graduations, et le comparer au
  vent d'un seul coup d'œil — la composante de vent traversier), et une flèche
  creuse sur le pourtour à l'azimut d'où vient le vent, sa force et son cap
  écrits à côté d'elle plutôt que dans une pilule garée ailleurs — **et,
  depuis le 07-31, la traîne du vent** : son trajet à travers le terrain, en
  POINTILLÉ, de bord à bord en passant par le centre. La flèche seule marquait
  un POINT sur le pourtour et laissait l'œil reporter cet azimut à travers le
  disque ; pointillé et non plein parce que la forme porte le sens (la piste
  est un objet réel et se trace plein, le vent est une mesure et reçoit un
  trait de construction — deux traits pleins se liraient comme deux pistes).
  La ligne de titre est **justifiée** le même jour : code à gauche, nom de la
  station à droite, couvrant exactement ce que couvre le filet, largeur
  MESURÉE et jamais calculée depuis un pas de glyphe. La piste est
  **cherchée sur la carte SD**, `/stackchan-companion/runways.csv` — la base
  OurAirports (DOMAINE PUBLIC) filtrée par `tools/generators/make-runways.py` en ~14 200
  enregistrements de largeur fixe de 17 octets, triés par ICAO puis par
  longueur DÉCROISSANTE, ce qui permet au bin une **dichotomie sur les
  offsets** (14 sondages plus une lecture de confirmation, une fois par
  changement de station, depuis `loop()` seul — SD et LCD partagent le SPI2)
  et le fait tomber sur la piste PRINCIPALE de l'aérodrome. Un NUMÉRO de piste
  est le cap MAGNÉTIQUE arrondi à la dizaine (la 12/30 de FMEE fait en réalité
  102/282°, 18° d'écart) : c'est pourquoi le cap VRAI est cherché plutôt que
  déduit. Le réglage `metar_rwy` (forme `12/30@102`) reste PRIORITAIRE quand il
  est renseigné — pour forcer une piste secondaire ou couvrir un terrain absent
  de la base ; sans l'un ni l'autre, la rose s'affiche seule.
  Les données se replient dans les deux
  marges de 68 px que le disque laisse libres : `fltCat` codé couleur
  VFR/MVFR/IFR/LIFR de jour (seulement s'il est publié) mais encodé par le
  POIDS sous un thème nuit — contour / fond atténué / plein / plein + cadre
  intérieur — parce que l'accent de Scope nuit EST le rouge IFR et que Gundam
  nuit tient le bleu à zéro : le code international y disparaît dans
  l'interface ou casse la règle d'adaptation à l'obscurité sur laquelle la
  palette est bâtie ; le coût (pas de code couleur la nuit) est énoncé à
  l'écran et dans la doc, température/point de rosée et
  nuages à gauche, QNH, visibilité, **humidité relative DÉRIVÉE par la formule
  de Magnus** (le bulletin n'en porte pas) et l'âge de l'observation à droite —
  **une largeur de colonne commune de 60 px des deux côtés**, taille 2 quand la
  valeur y tient, taille 1 sinon, et chaque chaîne bornée pour qu'aucune ne
  morde sur la rose. Les codes de nuages portent une **glose en clair** sous
  eux (`CAVOK` → degage, `BKN` → fragmente, `///TCU` → non mesure +
  bourgeonne) : le code est ce que lit un pilote, la glose ce que lit tout le
  monde. Titre = ICAO +
  nom de la station sur une ligne, et le `rawOb` brut que lit un pilote reste
  verbatim en bas. Période propre de 10 minutes, requête uniquement quand la vue est
  ouverte ; chaque ligne n'est construite que si sa donnée existe (température,
  point de rosée, QNH ou catégorie absents ne s'affichent pas). Visibilité
  métrique en KILOMÈTRES, et un `+` final est lu comme le groupe METAR 9999 —
  « 10 km+ », jamais un 9600 m converti ; gauche = clavier (callsign à traquer OU code aéroport IATA/ICAO
  pour recentrer, persisté) ; droite = réglages (rayon 10-500 nm avec pavage
  >250, poll, source — persistés) ; bas long = retour companion. En haut de la
  pile, la **vue NOTAM** lit **autorouter.aero**, c'est-à-dire la base
  EUROCONTROL EAD (08-01). Elle est restée délibérément VIDE deux jours —
  aucun service gratuit et sans clé n'avait répondu pour cette station
  (recherche publique FAA 403, `notamapi` 401 sans clé) et analyser une réponse
  que personne n'a vue est le seul mode de défaillance qui compte sur un
  affichage aéronautique ; le parseur a été écrit contre une VRAIE réponse
  captée (`tools/probes/autorouter-notam.py`), la méthode qui a produit la vue METAR.
  Gratuite mais pas anonyme : OAuth 2.0 client_credentials réutilisant
  L'E-MAIL ET LE MOT DE PASSE du compte (il n'existe aucune clef API), et
  l'accès API est une permission accordée sur ticket support — un compte
  activé seul renvoie 403 « privileges », ce qui ressemble exactement à un mot
  de passe faux sans en être un. FMEE a répondu 27 NOTAM dont 13 en vigueur,
  champs E de 74 à 1673 caractères : la vue garde ceux EN VIGUEUR (seulement
  quand NTP rend l'horloge fiable), les trie par purpose PIB (NBO > BO > B > M,
  l'ordre aéronautique et non le nôtre) et en montre UN PAR ÉCRAN, feuilleté
  par swipes HORIZONTAUX — l'axe libre, le vertical étant pris deux fois. Un
  texte tronqué LE DIT.
  **Le bug qui se cachait derriere cette fonction** (08-01) : la vue repondait
  un simple « -1001 » alors que la meme requete passait depuis un PC.
  `HTTPClient` ne DECHUNKE que via `writeToStream()` — `getStream()` rend la
  socket brute, lignes de taille de chunk comprises, et ArduinoJson lisait ce
  cadrage avant d'abandonner. Tous les services que le bin interrogeait
  envoient un `Content-Length`, ce qui explique que ca soit reste invisible
  jusqu'a autorouter (Apache + mod_wsgi, sans Content-Length), sa premiere
  source chunkee. `fetchJson` draine desormais un corps chunke dans un puits
  PSRAM borne (regle 18) et analyse depuis la.
  **Un appui long** (700 ms, doigt immobile) sur n'importe quel ecran de
  lecture FORCE une mise a jour du METAR, du TAF et du NOTAM : tous les autres
  gestes etaient pris, et un double tap ne peut pas marcher puisque le premier
  tap part deja au radar. Il efface aussi l'espacement d'echec de 60 s, sans
  quoi la garde qui evite de marteler un service en panne avalerait la relance
  qu'on vient de demander.
  **Routes multi-étapes** résolues correctement (le tronçon qui encadre
  l'appareil, `fr::pickLeg` — testé natif), **compagnie aérienne**
  affichée, **luminosité et thème Nuit automatiques** (LTR-553 + RTC,
  actifs par défaut), **zoom à la pincée** (le rayon n'a plus de
  réglette), filtre du trafic au sol, suivi automatique du plus proche,
  **tête pointée vers le vol traqué** et **chirps**, dont le `volume` est un
  NIVEAU et non un interrupteur (0..100 %, 0 = muet et c'est le seul « off » ;
  muet par défaut ; DIVISÉ PAR DEUX la nuit sur la règle qui bascule le thème,
  et le niveau met à l'échelle l'enveloppe mesurée au lieu de la remplacer, si
  bien qu'une alarme discrète reste la même alarme ; réglé sur l'appareil par
  un CURSEUR de l'onglet RADAR, par pas de 5 %, qui joue un aperçu AU niveau en
  cours de réglage),
  recul adaptatif sur 429/5xx.
  **Unités aéro ⇄ métrique** basculables (distances, vitesses, altitudes,
  vario — stockage et requêtes API restent en unités aéronautiques),
  panneau de réglages à **table de géométrie unique** (dessin et test
  tactile lisent la même description de lignes),
  Traque MONDIALE par callsign (/v2/callsign), progression du vol
  (distances, %, barre avec marqueur), vario, squawk d'urgence,
  catégories OACI (symboles hélico/planeur/ULM/léger/militaire
  magenta + type), **4 thèmes appariés jour/nuit** : 0 Gundam, 1 Gundam
  nuit (ORANGE saturé, bleu à zéro : c'est lui qui tirait vers le jaune),
  2 Scope ATC, 3 Scope nuit (**mode ASTRO** : fond noir + rouge PUR
  monochrome, façon Stellarium/SkySafari — seuls trois niveaux de texte
  conformes existent en rouge pur, la hiérarchie passe donc par la taille ;
  le militaire se distingue par sa FORME ; seule la rampe d'altitude glisse
  vers l'ambre, faute de pouvoir séparer quatre tranches entre 3:1 et
  5,25:1). Pair =
  jour, impair = sa nuit : `auto_night` bascule vers `theme | 1` et revient
  le matin, un thème de nuit choisi à la main restant en place. Une nuit
  UNIQUE faisait perdre son identité au thème qu'on venait de choisir.
  ⚠ La numérotation a changé : `theme: 1` désignait Scope auparavant.
  **Panneau de réglages en TROIS onglets** (Affichage / Radar / Reseau) :
  huit groupes dans 240 px imposaient des cibles de 48×20 px et des libellés
  tronqués ; toute cible fait désormais au moins 97×26 px (bandeau d'onglets
  3 × 100×26 sur toute la largeur — le titre « Reglages » a cédé la place,
  trois onglets nommés disent déjà ce qu'est l'écran), les libellés sont
  entiers, et une SEULE table de géométrie (`Grid`/`gHit`) est lue par le
  dessin ET le test tactile — les bandes codées en dur des deux côtés
  auraient dérivé en passant le thème de trois à quatre entrées. L'onglet
  RESEAU accueille les **puces de source** et les diagnostics qui se
  cachaient derrière le swipe haut (IP, signal, mémoire, PSRAM, piles,
  SD/HTTP, rafraîchis chaque seconde).
  **Quatrième source : `safesky`** (api.safesky.app) — trafic FLARM/advisory
  (planeurs, UAV) qu'aucun miroir ADS-B ne voit. Elle sert des **mètres et
  des m/s**, convertis **à l'entrée** pour qu'aucun second système d'unités
  ne circule dans le code ; son paramètre `rad` plafonne à 20 km, donc
  au-delà de 10,8 nm la requête bascule sur la boîte englobante `viewport`
  (qui règle 200 nm obtient 200 nm) ; elle exige une **clef API** (`Secret`,
  jamais réaffichée, sentinelle `-` pour la révoquer) sans laquelle la puce
  refuse d'être sélectionnée et le radar nomme le réglage manquant au lieu
  d'afficher « 0 avion ». Son authentification `x-api-key` est dépréciée au
  profit d'une signature HMAC-SHA256 — dette connue, documentée dans la
  source et dans `docs/guests/FLIGHT-RADAR.md`.
  **Contrastes RGAA/WCAG 2.1 AA vérifiés par script**
  (`scripts/gates/check-contrast.py`, lecture de la table dans la source, sortie
  en erreur si un seuil casse) : texte ≥ 4,5:1, objet graphique porteur de
  sens ≥ 3:1, décoratifs exemptés par 1.4.11. Mesure faite **après
  quantification RGB565** et avec la courbe sRGB par morceaux — une palette
  calée en RGB888 avec un gamma approché donnait un faux verdict, et les
  trois thèmes échouaient en réalité avant cette passe.
  Légende complète toujours affichée dans le récap (les diagnostics ont
  déménagé dans l'onglet RESEAU des réglages),
  48 avions (« 48+ » si saturé), accents efontJA.
  **Doc dédiée : `docs/guests/FLIGHT-RADAR.md`** (mermaid).
  Concurrence : netTask = seul propriétaire TLS (demandes par génération). Config SD propre au bin (`/stackchan-companion/flightradar.yaml`,
  éditable console) ; WiFi du companion réutilisé ; arrêt distant SceGuest.
  Réseau du bin sur TÂCHE dédiée (le TLS bloquant dans loop() rendait le
  WebServer SceGuest injoignable) ; init SD CoreS3 corrigée (SPI.begin
  36/35/37/4 obligatoire) ; parseur SceGuest QUOTE-AWARE (les credentials
  sérialisés entre guillemets par le companion cassaient le STA invité).
  **Swipe bas = retour au companion sur TOUT bin invité** (détection dans
  le stub SceGuest : confirmation à l'écran puis reflash — actif par
  défaut, désactivable par les devs communautaires via
  `guest.setSwipeExit(false)` ; symétrique du swipe bas companion →
  launcher). Démontre toute la chaîne invité de `docs/guests/README.md`. Whitelist SD étendue :
  tous les `.yaml` de `/stackchan-companion/` (configs de bins invités, catégorie
  « guest » dans le gestionnaire de fichiers) **et `/companion.bin`** (le
  binaire de restauration du stop se déploie à distance).
- **Durcissement des chemins de récupération** (revue max 07-27) : la
  purge de boot RESTAURE au lieu de supprimer (`/companion.old`,
  `config.yaml.tmp`, `rules.txt.tmp` étaient les seules copies rescapées
  après une coupure entre `remove` et `rename` — le filet de retour et
  les credentials WiFi partaient à la benne) ; `/api/bins` écrit en
  `.tmp` + valide (octets reçus, magic 0xE9, 256 Ko min) avant `rename`
  — un `.bin` tronqué passait D'AUTANT MIEUX la garde « taille ≤
  partition » et flashait une partition non bootable ; le launcher
  REFUSE de lancer sans `/companion.bin` (son écran de confirmation
  promet le retour) et sans partition OTA ; les zones tactiles du
  bandeau sont les boutons DESSINÉS (un tap imprécis déclenchait un dump
  flash de 1,5 Mo) ; la sauvegarde débouncée est FORCÉE avant tout
  reboot/extinction (un réglage acquitté 200 partait à la poubelle) ;
  le self-heal du launcher REND ses pauses (`pause()` est refcomptée,
  `isPaused()` renvoie l'ACK — l'écran restait figé à vie).
- **Lobby de boot des bins invités** (`SceGuest::applyLobbyTheme`) :
  fenêtre de 2,5 s AVANT le `setup()` de l'invité — `[Companion]` /
  `[Continuer]` / décompte — au thème du launcher. C'est la seule sortie
  qui ne dépende ni du code de l'invité ni du réseau : un `.bin` qui
  plante, boucle ou casse le tactile reste récupérable sans USB. Sur
  CoreS3 la lib prend le chemin TACTILE, qui ignore le callback de dessin
  des boutons — l'écran d'attente est donc remplacé en entier
  (`setWaitForActionCb`). Pas de bouton « sauver » : l'action est inerte
  dans un invité, et sauver écraserait `/companion.bin`, l'unique filet.
- **Configuration web des bins invités** (`SceGuest`) : un invité DÉCLARE
  ses réglages (`addSetting` — Num/Bool/Text/Choice/**Secret**) et fournit
  deux accesseurs ; SceGuest rend le formulaire au thème de la console sur
  `/config`, applique et rend un verdict. Auth HTTP Basic **reprise de la console du
  companion** (section `api:` du même `config.yaml` — un seul mot de
  passe pour le robot ; `setAuth` pour imposer autre chose), valeurs échappées, champ caché anti-POST-vide, `value='1'`
  explicite sur les cases (le navigateur envoie `on`, qu'un `toInt()`
  naïf transforme en 0). L'app garde la propriété de son stockage.
  **Jeton anti-CSRF tiré au boot** dans le champ caché (une constante
  publique se reproduisait dans un POST cross-origin : en réécrivant
  `host`, une page tierce faisait envoyer le jeton domotique au serveur de
  son choix, sans même le connaître). `onSettingsBegin` déclarée **sans**
  `onSettingsSaved` est refusée — le verrou pris par la première n'a qu'un
  point de libération, et la paire incomplète gelait le robot.
  Un réglage **`Secret`** (jeton d'API, mot de passe) n'est **jamais
  réaffiché** : le champ part vide, un envoi vide vaut « inchangé », et un
  tiret seul (`-`) **révoque** la valeur.
  Un `Text` réaffiche sa valeur dans l'attribut HTML — acceptable pour un
  rayon en milles nautiques, pas pour un jeton Home Assistant, qui ouvre
  toute l'installation à qui charge la page. Si un `Secret` est déclaré
  sans mot de passe sur la page, celle-ci l'affiche en tête plutôt que de
  se bloquer : c'est aussi le seul chemin commode pour saisir le secret.
- **Pont Claude Code** (`scripts/dev/claude-statusline.ps1`) : le robot vit
  l'activité Claude (jauges, réactions) **sans Claude Desktop ni BLE** — juste
  le WiFi + l'API. Pousseur générique : `scripts/dev/statusbar-push.ps1`.

## API, console, réseau

- **Console web embarquée** (`/`, zéro CDN, interface française, design
  « Liquid Glass » à système de rayons/couleurs homogène) : header à **chips
  d'état** + graphe heap/frame ; zone **contrôle toujours visible** (émotions,
  danses, tête) ; panneaux regroupés logiquement (Bande de statut + Options
  côte à côte à dépliage lié, Caméra, **Fichiers · carte SD** unifié, Réglages
  fins, Système = réseau/VOR/sécurité/OTA/alim) ; bande en *segmented control*
  + *pills* d'icônes ; télécommande servo, calibration VOR ; **Swagger**
  (`/swagger`) + OpenAPI ; **mDNS** + **portail captif** AP.
- **Protection Basic Auth** (`config.yaml` section `api:`) : mot de passe vide
  = API ouverte (défaut), non vide = protège GLOBALEMENT console + `/api/*`
  via `AsyncAuthenticationMiddleware`, modifiable à chaud `POST /api/security`,
  persisté SD. **Les uploads (OTA/bins/CSV) vérifient l'auth EUX-MÊMES** en
  tête de callback (le middleware ne s'exécute qu'après le corps).
- **OTA** `POST /api/update` (multipart) + **`/api/reboot`** + **`/api/poweroff`**
  (différés, réponse HTTP avant l'action ; poweroff one-shot — pas de boucle
  sous USB) ; **API [Bins] + Launcher** (menu SD `/bins/`) ; `guest/SceGuest.h`.
  **Launcher UI refondue** (alignée console : cartes arrondies, accent
  cyan/indigo, boutons pleins/liserés) + **écran de flash dédié** : points
  « … » EN HAUT, titre + nom du binaire EN DESSOUS, vraie barre de
  progression + %, messages SD-Updater relégués dans une bande basse — les
  UI par défaut de la lib (DisplayUpdateUI/DisplayErrorUI) écrivaient
  par-dessus les points ; TOUS les callbacks sont remplacés (flash + SauvFW).
- **Durcissement revue max 07-26** : uploads `sd/put` ATOMIQUES pour TOUS
  les chemins (`.tmp` + validation octets écrits + rename — un upload
  interrompu ne tronque plus config.yaml ; `/companion.bin` : danse `.old`,
  l'ancien filet survit à un échec de rename ; `.tmp` purgés au boot) ;
  noms de fichiers SD échappés JSON sur TOUS les émetteurs (sd/list, bins,
  danses) ; mode launcher EXCLUSIF (Brain suspendu, couple servo relâché,
  LEDs éteintes, caméra inhibée, self-heal de la pause écran) ; échec de
  flash = retour à la face (plus de reboot défensif) ; contact tactile
  long ≥ 700 ms avalé (pouce posé ≠ wink) ; radar invité : recentrage
  aéroport par CAS + générations (plus de recentrage fantôme), un seul
  écrivain SD (loop), cache de route, panneau droit inerte au tap, traque
  préservée hors cadran ; SceGuest : hook `onBeforeStop` (suspension des
  tâches de l'app avant reflash) + lobby BtnA `checkSDUpdater` dans le bin
  de démo.
- **Import / Export SD** (`GET /api/sd/list` · `GET /api/sd/get` ·
  `POST /api/sd/put` · `DELETE /api/sd/delete`) : télécharger/remplacer/supprimer
  `config.yaml`, `rules.txt`, chorégraphies (`/dances/*.csv`) et binaires
  (`/bins/*.bin`). Chemins **whitelistés** (anti-traversée) ; import de
  config/règles → **rechargement auto**. Panneau console *« Import / Export »*
  (liens de téléchargement + import + suppression). ⚠ `config.yaml` peut
  contenir le mot de passe WiFi (protéger l'API par Basic Auth).
- **`GET /api/sensors`** : télémétrie des capteurs additionnels — jauge INA226
  (tension bus/shunt), lumière ambiante LTR-553 (`light_pct` + `light_raw`),
  cap magnétique BMM150.
- **Latence réseau maîtrisée** (mesuré : commandes ~40-60 ms même flux actif) :
  modem-sleep WiFi OFF + TX max (le power-save ajoutait 100-300 ms/requête),
  console à commandes **parallélisées** (3 en vol) avec timeouts
  auto-guérisseurs, vue caméra et poll status qui **cèdent la radio** aux
  commandes, aucune requête doublée (pas de refresh après chaque POST),
  uploads **sérialisés** (un seul actif, reprise auto >10 s).

## Capteurs additionnels K151

- **Batterie** (PMIC AXP2101, `Board::batteryPercent/isCharging/vbusPresent`)
  → `/api/status` + cellule console (rouge si ≤15 % hors charge) + bandeau
  « BATTERIE FAIBLE ».
- **Jauge INA226** (I2C 0x41, Wire1) : tension bus (batterie) et shunt
  (∝ courant) exposées en télémétrie `/api/sensors`.
- **Magnétomètre BMM150** (via `M5.Imu`, 9-axis) : cap magnétique 0–360° en
  télémétrie (référence yaw sans dérive ; fusion VOR à calibrer sur HW).
- **Lumière ambiante LTR-553** (I2C 0x23, **gain 96x + intégration 400 ms** —
  capteur quasi occulté par le boîtier K151, calibré sur mesures HW : bureau
  éclairé ≈ 50 %) : **luminosité écran automatique** option `auto_brightness`
  (lue ~2 s, appliquée par le renderer sur changement seul — jamais
  `setBrightness` par frame ; garde d'échec de lecture : la dernière valeur
  valide est conservée) ; **`dark_sleepy`** (défaut ON) : MODE NUIT par la
  ROULETTE — noir complet soutenu ~6 s → Sleepy remplace Normal dans le tirage
  (poids dominant ~66 %, le robot somnole avec de rares autres émotions),
  réflexes/danses/API prioritaires ; lumière revenue → mode jour + réveil
  blink. Hystérésis anti-flap.
- **Volume nuit** : `sound_volume_night`, du coucher au lever RÉELS du soleil
  (`sce::isNight` sur les réglages `lat`/`lon` — le même code solaire que le
  bin radar, si bien que les deux binaires ne peuvent pas diverger sur ce
  qu'est la nuit). Le companion y a gagné une horloge : **NTP** au boot en
  mode STA, et le **RTC BM8563 est désormais RÉGLÉ** depuis elle, une fois —
  la puce était lue par cette fonction et écrite par rien, ce qui explique
  que l'ancienne fenêtre fixe 22h-6h se déclenchait sur une heure arbitraire.
- **IMU étendu** : double-tap logiciel (pic `|accel|`) → Happy + wink ;
  orientation face haut/bas (`accel.z`) → Sleepy tenu.
- **Tête vers le bruit** (option `sound_track`, ES7210 stéréo) : porte
  ambiante, seuil/sens réglables, arbitrage bus I2S1 avec les chirps.
  **Sourdine pendant le mouvement** : les événements sonores sont ignorés
  pendant une trajectoire servo + 350 ms (les micros captent le bruit
  servos/engrenages — sans ce gate, la tête se poursuivait elle-même et le
  faux événement retardait le retour au home).

## Robustesse / sécurité (durcissement)

- **Config.yaml quote-aware** : `password: ""` = chaîne vide (API ouverte) et
  non les deux caractères littéraux ; credentials sérialisés entre guillemets
  (round-trip sûr pour `#`, espaces, vide) ; entrées API nettoyées (`"`, `\`,
  contrôle retirés) → pas d'injection JSON dans `/api/status` ni YAML.
- **`/api/security` et `/api/wifi` différés** : les credentials sont stagés
  puis appliqués par `loop()` — plus de mutation de `String` partagée depuis un
  callback AsyncTCP concurremment avec `SdConfig::save()`.
- **Migration de schéma** (`cfg_version`) : un yaml plus ancien voit ses clés
  recalibrées re-défaussées au chargement.
- **Durcissement concurrence & ressources** : refcount
  pause renderer sous spinlock ; ownership upload sous spinlock (timestamp
  avant publication, auto-heal hors fenêtre critique) ; caméra — deinit purge
  complète (`_initReq`, état snapshot), `_failed` dé-latché par `camera=0`,
  re-serve MJPEG 300 ms constant (crédits AsyncTCP), rendez-vous
  `waitCaptureIdle` avant tout flash, pipeline JPEG unifié streamé PSRAM
  (`fmt2jpg_cb`, zéro alloc interne) ; `?full=0` = frame live ; garde
  d'échec de lecture LTR-553 ; console — jeton de génération anti-callbacks
  tardifs, snapshot sans faux « échec », blobs révoqués, resync des toggles.

## Tests / build

- **159 tests natifs** (16 suites), `scripts/gates/test-native.ps1`, FakeClock ; les
  modules `engine/`+`behavior/` restent purs (Clock/Rng injectés). La 13ᵉ suite
  est `test_sunclock` — position solaire vérifiée contre des heures de lever et
  de coucher publiées, ce qui permet au thème nuit du radar de suivre le vrai
  soleil plutôt qu'une fenêtre horaire fixe.
- **Endurance** : run long sans reboot, heap stable, piles saines ; heartbeat
  instrumenté + `scripts/dev/endurance-log.ps1`.
- Env PlatformIO : **`companion`** (firmware), `flight-radar` et `ha-remote`
  (bins invités, déposés sur la SD — jamais flashés en USB, ils écraseraient
  le companion), `flight-radar-fire` (la même source radar en application
  autonome sur M5Stack Fire), `native` (tests PC).
  `build_unflags = -std=gnu++11` ; bus LCD 40 MHz reconfiguré AVANT
  `SD.begin()`.

## Mutualisation (revue conception 2026-07-29)

- **Un seul analyseur YAML** (`SceGuest::yamlForEach`) au lieu de quatre. Le
  format des fichiers de la carte était re-implémenté dans `SdConfig.h`,
  dans `SceGuest.h` et dans chaque bin invité, avec quatre comportements
  différents — et deux pannes en étaient sorties : des guillemets conservés
  dans le SSID (STA en échec, repli AP muet, 07-25), puis un
  `api: "adsb.fi"` que `flight-radar` ne reconnaissait plus, basculant sur
  une autre source sans un mot. L'analyseur vit dans `SceGuest.h` parce que
  ce header est le seul déjà embarqué dans les trois binaires : mutualiser
  n'y coûte pas un octet de flash. Le caractère sectionné du fichier est
  **déclaré** par l'appelant, jamais deviné. Côté companion, `SdConfig`
  garde un jumeau assumé et documenté — `parseScalar` **et** la boucle de
  lecture de `load()` : faire dépendre le companion d'un header `guest/`,
  conçu pour être copié dans des projets tiers, coûterait plus que la
  duplication. Les deux boucles doivent rester identiques ligne pour ligne
  (règle A2.23) ; elles avaient déjà divergé, voir la revue ci-dessous.
- **Allocateur PSRAM partagé et borné** (`firmware/common/PsJson.h`). Il
  était dupliqué dans les deux bins, et surtout retombait **silencieusement**
  sur `malloc()` interne quand la PSRAM manquait — prenant le tas qu'il
  existait pour épargner, au pire moment, avec pour symptôme une pile réseau
  morte plus loin sans lien visible. Désormais : gros bloc REFUSÉ (une
  analyse ratée se rejoue, un tas interne épuisé non), petits replis
  plafonnés par document, et toute PSRAM saturée laisse une trace série.
- **Ordre des routes A2.19 vérifié au démarrage.** Toute route passe par
  `WebApi::route()` ; `checkRouteOrder()` relit la liste avant
  `_server.begin()` et dénonce toute paire mal ordonnée. Le critère est
  celui du routeur lui-même (`^{uri}(/.*)?$`) et non une approximation —
  sans quoi le garde aurait crié sur `/swagger` et `/api/openapi.json`, qui
  fonctionnent. La règle a déjà rendu trois endpoints inatteignables ; elle
  ne tenait jusqu'ici qu'à la position de 38 appels dans 1400 lignes.
- **`flight-radar` : un seul chemin HTTPS+JSON** (`fetchJson`). Les quatre
  appels répétaient ouverture TLS, timeout, GET, analyse et `end()` — et
  divergeaient : trois analysaient dans le tas INTERNE au lieu de la PSRAM.
  Bilan : −1,6 Ko de flash.

## flight-radar : mode dock et phase de vol (2026-08-04)

Trois idées empruntées au guide d'interaction Aerospace Tracker (réflexion
roadmap du même jour), la troisième redessinée par l'utilisateur avant
d'avoir une heure :

- **Le panneau 6-to-12** (design utilisateur) : la ligne de séparation du
  panneau vol DEVIENT la route — DÉPART en bas, ARRIVÉE en haut, on vole
  de 6 h vers 12 h ; le segment parcouru est plein jusqu'au triangle
  avion, le reste en pointillés ; les blocs sont échangés pour suivre,
  chaque distance se tient du côté de son aéroport, et la rangée
  qu'occupait l'ancienne barre horizontale porte désormais les rangées de
  trajet. Pas de route complète = un rail tout en pointillés, sans avion.
  Rangées de trajet (user 08-04) : le `%` et la distance restante
  partagent une rangée, le code de phase porte son libellé en clair
  (`CRZ croisière`), et le temps restant se lit `ETA 0h28` — sans langue,
  au lieu du libellé traduit `left`/`reste`.

- **Mode dock** : posé sur un bureau, les vues défilent toutes seules —
  `dock_s` secondes par vue (`/config`, 0-120, 0 = coupé par défaut).
  Toute action manuelle relance l'horloge à son producteur ; les modales
  tactiles suspendent le cycle en bloquant la boucle ; l'avance passe par
  `applyEvent()` comme troisième vocabulaire d'entrée après le tactile et
  les boutons.
- **Phase de vol** : une pastille `CLB`/`CRZ`/`DES`/`GND` sur le panneau
  traqué, abréviations aviation, mêmes seuils que le correcteur de tronçon
  inversé — la phase existait dans les maths et restait invisible à
  l'écran.

## Minuteur et pomodoro dans la bande de statut (2026-08-04)

Deux modes de bande interactifs (demande utilisateur) : **4 = minuteur**,
**5 = pomodoro**. Les machines à états sont un module pur unique
(`engine/BandTimer.h`, Clock injectée, `test_bandtimer` 8 cas natifs) ;
loop() possède l'instance, publie son affichage par le tableau noir
FieldStore (`tmr`/`tmr_st`) et transforme ses événements en émotions par la
CommandQueue — le renderer ne fait que traduire un état en couleur et tendre
le texte composé à `drawDynText`, dont la détection de changement offre même
le clignotement de la sonnerie gratuitement (loop alterne le texte à 2 Hz).

- **Minuteur** : swipe vertical sur la moitié gauche de la bande = heures ±1
  (enroule 0↔23), moitié droite = minutes ±1 (0↔59) — à l'arrêt ou en pause
  seulement, un compte en course n'est volontairement pas éditable. Tap de
  bande = démarrage / pause (le reste gèle, pas de ré-ancrage horloge) /
  reprise / acquittement. À zéro : `00:00` clignote en rouge et le robot
  tire **Excited** — le visage d'alarme aux yeux étoiles — pendant 10 s,
  jusqu'à l'acquittement.
- **Pomodoro** : `cycle/total MM:SS` ; travail → **Focused**, pause →
  **Happy**, fin du dernier bloc → **Glee** (pas de pause orpheline). Les
  longueurs sont des clés de tuning (`pomo_work_min`/`pomo_break_min`/
  `pomo_cycles`, bornées dans la machine, curseurs dans la console).
- L'anneau des modes devient `0→2→3→4→5`, `/api/statusbar` et le contrôle
  segmenté de la console suivent ; un nouveau rappel
  `TouchGestures::onTapBand` garde taps de bande et taps d'yeux comme deux
  vocabulaires séparés. Détail : STATUSBAR §2b.
- **Révisé le jour même** (utilisateur : « les swipes sont durs à
  utiliser ») : le réglage du minuteur adopte le motif RÉVEIL — quand il est
  éditable, la bande est un clavier (tiers gauche = heures, tiers droit =
  minutes, tap au-dessus des chiffres = +1, en dessous = −1, centre =
  démarrer/pause), avec de petites rangées d'affordance `+`/`−` autour des
  chiffres (qui perdent une taille pour faire la place). Les swipes
  marchent toujours ; en course ou en sonnerie la bande ENTIÈRE reste
  l'action primaire — une alarme doit s'arrêter au premier tap, pas au tap
  bien visé.
- **Option `band_clock`** (utilisateur 08-04) : le mode 0 (Aucune) peut
  porter l'horloge murale au lieu du noir — HH:MM gris discret par le même
  tuyau tableau noir, vide tant que NTP n'a pas parlé (jamais 1970). Le
  robot ne connaît que l'UTC (la nuit est pilotée par le soleil, à
  dessein), donc l'affichage a son propre `tz_offset_h` (curseur console, en heures décimales comme les bins invités,
  ±14 h) — affichage seulement.

## Fusion magnétomètre/VOR, et le hold du cou fait proprement (2026-08-04)

> **Verdict HW — impraticable sur le K151, atteint honnêtement : deux fois
> faux d'abord, et le dossier garde les trois actes.** Session 1 (897
> échantillons) : norme 296-589 µT contre ~50 terrestres, cap cloué ±3°
> pendant les rotations à la main — fermé comme impraticable, en accusant
> les courants servo. Session 2 : semblait RENVERSER le verdict — régimes
> servo identiques, un balayage de tête moyenné retrouvant un signal de la
> taille du terrestre — **mais elle a tourné contre `servos=0`, où la
> tâche servo n'écrit rien : toute la chorégraphie était inerte, le
> balayage n'a jamais bougé, son « signal » de 50,9 µT était de la dérive.
> L'utilisateur l'a intercepté.** Session 3, servos réellement actifs,
> balayage alterné + contrôle de dérive : la dépendance au lacet fait
> 370 µT pour 80° — six fois la Terre, les aimants servo du CORPS vus à
> travers un gradient raide — avec ±100-175 µT d'irreproductibilité à
> consigne identique et ~50 µT de sauts d'état de couple. Une calibration
> par carte de pose meurt sur cette irreproductibilité. Seule voie
> restante : un magnétomètre externe loin des moteurs (Grove). Le code de
> fusion et ses trois tests natifs restent ; `vor_mag_alpha` reste à 0.

- **Le cap BMM150 corrige désormais la dérive de lacet du VOR** —
  l'observable que l'accéléromètre n'a pas : la gravité ne dit rien d'une
  rotation autour d'elle-même, donc la dérive de lacet n'était rattrapée
  que par le tirage au calme et les saccades de rattrapage. Un filtre
  complémentaire ancre une référence de cap et tire `offset.x` vers
  `cible − (cap − réf)·K` PENDANT LE MOUVEMENT — exactement là où
  l'accéléromètre ment — tandis que le régime calme reste la propriété du
  correcteur accéléro (qui ré-ancre la référence). Coupé pendant le
  mouvement propre (les courants servo distordent le champ qu'on lit),
  ré-ancré sur les saccades et les trous de lecture. **`vor_mag_alpha`
  vaut 0 par défaut (coupé)** : le cap n'est pas compensé en inclinaison
  et son signe face au lacet gyro n'est pas validé sur matériel — avec le
  mauvais signe la boucle double la dérive au lieu de la supprimer. La
  recette de calibration est dans CONFIG (`telemetry=1`, tourner le robot
  à la main, vérifier que cap et lacet gyro s'accordent, puis 0.02-0.10).
  Trois tests natifs épinglent la physique : une fausse rotation (le gyro
  dit tourner, le cap dit immobile) est corrigée sans aucune saccade
  parasite, une VRAIE rotation n'est pas combattue, un cap absent est
  inerte.
- **Le hold du cou au réveil est le bit propre du Brain**
  (`setHeadFollowHold`), plus un `tuning.head_follow` mis à zéro puis
  restauré : la restauration écrasait en silence un
  `POST /api/tuning?head_follow` tombé dans la fenêtre du réveil. Le
  réglage de l'utilisateur n'est jamais touché ; les deux dettes de la
  ligne de revue 08-04 sont soldées.

## Revue à huit angles — défauts trouvés et corrigés (2026-08-04)

Huit angles de revue en parallèle sur le lot du 08-03 (ligne à ligne,
comportements retirés, traçage inter-fichiers, réutilisation, simplification,
efficacité, altitude, conventions), puis correctifs. Les lourds :

- **Le claquement du boot survivait sur chaque chemin de RETOUR.** La séquence
  de réveil corrigeait la mise sous tension, mais quitter le lanceur tactile
  et un flash API refusé/échoué réengageaient le couple sur la pose COMMANDÉE
  d'avant le passage de main, tête affaissée depuis des minutes — et les trois
  branches de refus du lancement API ne réengageaient RIEN : le cou restait
  mou pour la session (`releaseTorque()` ne pose pas `_autoReleased`, donc le
  réengagement automatique ne peut jamais tirer pour un relâchement
  délibéré). Nouveau `ServoMotion::rebaseAndEngage()` : la tâche de mouvement
  — seule propriétaire du bus — re-MESURE la pose, re-vise un mouvement en
  vol depuis l'origine vraie, puis honore la demande de couple ; chaque
  chemin de retour l'appelle.
- **`homeSlowly()` ne rampait jamais vraiment** : il stockait les cibles sans
  incrémenter le compteur de séquence que la tâche surveille. Désormais par
  `moveTo()` ; et quand la mesure du réveil échoue trois fois, la rampe est
  déléguée au SERVO (WritePos-avec-durée interpole depuis sa position réelle
  — la seule chose que la rampe logicielle ne peut pas savoir en aveugle).
  `isMoving()` rapporte la rampe côté servo, et le réveil attend ce signal au
  lieu de répéter la durée.
- **La garde de première frame est là où elle ne peut pas perdre** :
  `TripleBuffer` gagne `hasEverPublished()` et le renderer ne dessine RIEN
  avant la première publication réelle — l'avance de 60 ms rendait l'éclair
  du Normal par défaut improbable ; une gigue d'ordonnancement pouvait encore
  dépenser l'application instantanée sur une face que personne n'a produite.
- **Le repli sert désormais TOUT le cycle de poll.** Les tuiles satellites et
  la requête de traque mondiale continuaient de marteler le serveur préféré
  mort après que la requête centrale s'était repliée — jusqu'à 7 délais
  d'attente de plus par cycle et un anneau extérieur qui se vidait en silence
  sous un bandeau nommant la source vivante. UNE décision `serving` par
  cycle, honorée partout ; le tuilage se règle sur la source qui a RÉPONDU
  (un repli SafeSky→v2 a désormais ses tuiles >250 nm). Les échecs d'auth
  SafeSky (401/403, clé fausse) ne basculent plus en silence : un radar qui
  marche cacherait la mauvaise configuration pour toujours.
- **resume() du Renderer publiait son drapeau d'effacement après avoir rendu
  la pause** : le renderer pouvait se réveiller, passer son point de
  consommation, et rater l'invalidation — restes de menu figés sous les yeux
  jusqu'à la pause suivante, à des minutes de là. Les deux vrais drapeaux
  sont posés sous le verrou de pause, et le drapeau relais a disparu.
- **NOTAM : les lignes vides revivent** (l'écrasement CRLF mangeait chaque
  saut de ligne consécutif, recollant les blocs que le découpeur venait
  d'apprendre à séparer) ; le compte à rebours d'expiration se mesure contre
  l'horaire de l'item D au lieu de l'écraser à un x fixe, précisément sur les
  avis urgents.
- **ButtonFsm : un relâchement échantillonné dans sa fenêtre de rebond ne
  tire plus l'action longue qu'il visait à éviter** (le niveau brut garde
  désormais le test de seuil ; test natif ajouté).
- **Hot-swap SD durci** : ni sonde ni remontage sous un téléversement actif
  (`SD.end()` sous un `_uploadFile` ouvert = corruption FAT) ; une carte
  remplacée recharge les RÈGLES et annule l'autosauvegarde de tuning en
  attente (elle allait écraser le config.yaml de la carte fraîche avec celui
  de l'ancienne) ; les deux bins reculent 10→60 s tant qu'AUCUNE carte n'a
  jamais été vue.
- Plus petits : les jauges quantifient ce qu'elles DESSINENT pour que
  l'invariant de la détection de changement soit vrai (une barre pouvait
  rester ~1,6 px périmée) ; un seul point d'appel pour la ligne de jauge
  (forme A2.22) ; limites `/bins/` partagées entre lanceur tactile et cache
  API (24/40 contre 48/50 se comportaient différemment par porte d'entrée) ;
  l'overlay debug du radar se redessine à 250 ms au lieu de ~100 Hz ;
  `upAscii()` et `zuluHhmm()` remplacent cinq et deux copies manuelles déjà
  divergentes ; le seuil sRGB de test_emocolor aligné sur le 0.04045 de
  check-contrast.py ; commentaires français traduits ; l'exception A2.6 de
  `/api/sd/get` consignée dans la piste d'audit de la règle (ROADMAP §A2.6).

Déclinés avec raisons, consignés plutôt qu'abandonnés en silence : le double
tampon demi-pixel de drawBlush reste (c'est le correctif 08-03 d'une coupure
visible par l'utilisateur, coût borné et documenté) ; le listage SD garde sa
construction en String (la fenêtre de pause est dominée par la marche FAT, et
réécrire l'échappement JSON rejoue la leçon jesc du 07-26) ; intensify reste
séparé de lerpRgb888 (arrondis différents, portes calées au LSB).

## Revue max — défauts trouvés et corrigés (2026-07-29)

Revue multi-agents du lot ci-dessus (six angles indépendants, chaque constat
soumis à une réfutation adverse avant d'être retenu).

- **Lectures de fichiers SD non bornées** (`SceGuest::yamlForEach`,
  `SdConfig::load`, `DanceStore::parseCsv`). Le plafond de 512 octets était
  testé APRÈS un `readStringUntil`, donc après que la String eut grossi : il
  ne protégeait rien de ce que son propre commentaire annonçait. Un fichier
  corrompu — ou un `.bin` renommé `.yaml`/`.csv`, sans un seul `'\n'` avant
  des méga-octets — épuisait le tas AVANT le test, et ces fichiers arrivent
  par téléversement API. Désormais : `readBytesUntil` dans un tampon de pile,
  et le reste d'une ligne trop longue est JETÉ jusqu'au saut de ligne (sinon
  le résidu repartait en fausse paire clé/valeur). `SdConfig` n'avait même
  aucun plafond : les jumeaux avaient déjà divergé.
- **`PsJson` : le refus n'était pas sûr là où il était appliqué.**
  `reallocate` refusait tout bloc de plus de 4 Ko — y compris une
  RÉDUCTION. Or ArduinoJson tient un realloc de réduction pour infaillible
  (`ARDUINOJSON_ASSERT`, effacé en release) et a déjà libéré le bloc quand
  l'allocateur rend `nullptr` : on plantait donc exactement dans le cas qu'on
  prétendait dégrader proprement. Le refus est désormais réservé à
  `allocate()` ; ce que `reallocate` concède est compté et tracé. Au passage
  le budget cumulé, qu'il ne consultait pas, est réellement appliqué.
- **Garde A2.19 aveugle aux masques combinés.** Il comparait les méthodes par
  `==` alors que le routeur décide par intersection : une route déclarée
  `HTTP_GET | HTTP_POST` le rendait muet sur ce chemin — le cas où l'ordre
  compte le plus. Il dénonce en plus les doublons chemin+méthode, dont le
  second handler est mort sans un mot.
- **`flight-radar` : un timeout qui n'existait pas.** `tls.setTimeout(ms)`
  attend des SECONDES (on lui passait 6000 s), ne touche à rien tant que la
  socket n'est pas ouverte — c'est le cas avant `GET()` — et son effet était
  écrasé juste après par `HTTPClient::connect()`. La ligne était inerte et le
  commentaire promettait une borne de lecture jamais armée. Le seul réglage
  utile est `http.setTimeout()`, qui reporte le budget sur la socket. Les
  codes d'échec locaux passent de −1/−2 à −1000/−1001/−1002 : ils
  s'affichaient à l'écran sous le même numéro que
  `HTTPC_ERROR_CONNECTION_REFUSED`, seule trace disponible sans sonde.
- **Rendus spéciaux et regard** (`EyeRig`). L'étoile d'`Excited` appliquait
  `MoveY` avec le signe inverse du reste du visage : elle DESCENDAIT quand le
  regard montait. La croix de `Dead` ignorait le regard tout court, seul
  rendu à rester cloué au centre pendant que les yeux voisins bougeaient. À
  revalider sur cible (`docs/validation/VALIDATION.md`).

## Écran gelé pendant les téléversements (2026-07-30)

- **L'écran d'attente « … » ne s'anime plus.** Les trois carrés respiraient
  chacun sur sa période — c'était joli, et c'était le problème : animer oblige
  le renderer à repasser sur SPI2 à chaque frame, or pendant un téléversement
  ces frames se disputent le bus avec la carte SD (A2.16). L'indicateur censé
  masquer les saccades les produisait. Immobile, il est dessiné **une fois**
  (bande de statut figée avec lui) puis le bus est rendu.
- **Tout téléversement gèle donc l'écran**, plus seulement l'OTA : le gel est
  armé dans `WebApi::uploadOwns` et levé dans `uploadRelease`, point
  d'entrée/sortie unique des quatre routes (bins, sd/put, dances, OTA).
  L'auto-guérison de `update()` le lève aussi — sans quoi une connexion coupée
  en pleine montée laissait le robot gelé jusqu'au reboot. Un OTA réussi, lui,
  garde le gel jusqu'au redémarrage.
- Corollaire : `setBusy` devient utilisable pendant une écriture SD, ce que sa
  propre documentation excluait jusqu'ici. `pause()` garde son rôle propre —
  lui seul attend l'acquittement du renderer, donc lui seul permet de CÉDER
  l'écran à un tiers (Launcher, SD-Updater).
- Le launcher pose désormais les points une fois dans `drawFlashScreen` au lieu
  de les repeindre à chaque callback de progression, où ils s'intercalaient
  entre les lectures SD du flash pour un résultat identique.

## Outils PC (`tools/`, `scripts/`)

- **Éditeur de chorégraphies** (`tools/choregraphies/`, HTML+JS+CSS sans
  dépendance ni serveur — un double-clic suffit) : timeline de keyframes
  (expression, yaw, pitch, `gazeY`, paupières, `servoMs`, `holdMs`),
  **simulateur d'yeux** qui rejoue la chaîne du firmware
  (`EyeRig::setEmotion` → `mirrored` → `eyegeom::normalize` →
  `EyeDrawer::Draw`) sur les **presets réels**, cube d'orientation de la
  tête, lecture aux vraies durées, import/export du CSV de `/dances/`.
  Les bornes saisies sont celles du robot (yaw ±130°, pitch −74..+6°,
  `gazeY` saturé à ±0,20, 23 keyframes).
- `tools/choregraphies/extract-presets.py` **génère** `presets.js` depuis
  `src/engine/presets/*.h`, `Emotions.h` (noms **et** `emotionToRgb`),
  `EyeRig.h` (mapping gauche/droite/`lidCenter`) et `Tuning.h`
  (`eye_color_dim`) — recopier ces données à la main les aurait fait
  diverger du firmware dès la première retouche. À relancer après toute
  modification d'un preset, d'une couleur ou du mapping.
- **Dalle rendue sans rééchantillonnage** (revue 07-29). Le canvas dessinait
  en 320×160 puis était réduit vers ~160 px par le navigateur, en
  `image-rendering:pixelated` — c'est-à-dire au plus proche voisin. Le
  facteur n'étant pas entier (0,4988), la grille d'échantillonnage dérivait
  le long de l'image : les deux bras de la croix `Dead` progressant en x en
  sens INVERSE, ils accumulaient des phases opposées et l'un des deux
  finissait raboté à un trait clair d'un pixel — alors que le contenu du
  canvas, lui, était parfaitement symétrique (26 tampons par bras, même
  rayon, même couleur). Le buffer est désormais dimensionné sur la taille
  AFFICHÉE : plus rien à rééchantillonner, à toute taille de panneau, et
  l'aperçu devient net sur un écran HiDPI (il y subissait DEUX réductions).
- **Hauteurs de la mise en page** (user 07-30) : la ligne du haut se
  dimensionne sur le PLUS GRAND de Keyframe et Aperçu — leur contenu réel — au
  lieu d'un plafond arbitraire de `72vh`, trop haut sur un grand écran et trop
  bas sur un petit. La timeline ne participe plus à ce calcul : sa liste a une
  base flex nulle, donc elle s'adapte à la hauteur trouvée et défile au lieu de
  l'imposer avec ses dix-sept keyframes. La zone CSV prend tout le reste.
- **Couture de l'étoile `Excited`** (constat user 07-30) : ses deux triangles
  partageaient exactement l'arête `y = cy`, et l'antialiasing du canvas fait
  couvrir à chacun la moitié de cette rangée de pixels — deux moitiés
  translucides ne se recomposant pas en un pixel plein, un liseré sombre
  traversait l'étoile. Le triangle du haut descend sa base d'un pixel : les
  deux se chevauchent, la couture disparaît. Le firmware n'en a pas besoin,
  `fillTriangle` y traçant des pixels pleins.
- Autres défauts de l'éditeur corrigés par la même revue : toute saisie
  ARRÊTE la lecture (sinon les curseurs écrivaient dans la keyframe qui
  défile, et le résultat était écrasé à la frame suivante) ; l'avertissement
  « dernière keyframe » teste la colonne LITTÉRALE comme `DanceStore`, donc
  il voit enfin le cas le plus courant — dernière ligne sans émotion, pour
  laquelle le robot ajoute une keyframe de sortie ; l'événement de paupière a
  sa durée propre (enveloppes de `BlinkController`) au lieu d'une fraction de
  `servoMs`, si bien qu'un clignement sans trajet servo est enfin visible ;
  le glissé sur la tête ne reste plus « collé » quand le geste est annulé
  (`releasePointerCapture` lève sur `pointercancel` et sautait tout le
  nettoyage) ; la liste n'est plus reconstruite à chaque pixel de curseur ni
  à chaque keyframe lue.
- `scripts/gates/check-contrast.py` : lit la table `THEMES[]` **dans la source**
  de `flight-radar` et échoue si un couple texte/fond passe sous le seuil
  WCAG AA, calculé sur les couleurs **quantifiées RGB565** réellement
  affichées.

## Le bin invité « space » (2026-08-04)

Un troisième bin invité, `firmware/space/` — et le premier qui CALCULE son
sujet au lieu de le recevoir. Un TLE relevé au plus une fois par jour
(Celestrak, mis en cache sur la carte) suffit à placer l'ISS au kilomètre près
et à prédire ses passages sur 48 h, sur le microcontrôleur : coupez le réseau,
tout continue sauf la liste des lancements.

- **Cinq vues sur un anneau** : planisphère ISS (terminateur jour/nuit calculé
  par colonne, trace au sol, soleil/éclipse), PASSAGES (avec une pastille
  `VISIBLE` qui signifie satellite éclairé ET ciel sombre — deux événements
  différents), LUNE (vraie ellipse de terminateur, lever/coucher, prochaines
  nouvelle/pleine), CIEL (Lune + 5 planètes à l'œil nu, pastille d'état à
  trois valeurs pour que rien n'annonce « levé » à midi), LANCEMENTS (T- qui
  tourne localement entre les relevés, sur **deux sources avec basculement**).
- **La justesse vit dans deux en-têtes PURS avec leurs tests natifs** :
  `sgp4.h` (TLE + propagateur proche-Terre) est verrouillé sur le VECTEUR
  canonique du Spacetrack Report No 3 à 1 km près ; `astro.h` (Soleil, Lune,
  planètes, géodésie, angles de vue, ombre terrestre, recherche de passages)
  sur des événements réels et datés. 27 cas ; le portail est monté à 23 suites
  / 261 tests et construit CINQ firmwares.
- **Deux sources de lancements, et le basculement est un choix de conception,
  pas un filet** : RocketLaunch.Live en primaire — sans clé, sans limite
  publiée, et chaque donnée dans un champ à elle, donc le lanceur dont dépend
  le choix de la silhouette n'est plus récupéré en découpant une chaîne
  d'affichage ; elle seule publie la **météo du pas de tir**, désormais
  affichée à côté de la date. Launch Library 2 reste le repli et garde le
  plancher de relevé à sa limite publiée de 15 requêtes/heure. Les deux
  réponses se distinguent par la FORME (`results` vs `result`), ce qui permet
  au cache SD de rester une copie verbatim du corps qui a fonctionné. Les
  conditions de RocketLaunch.Live exigent un crédit visible : la vue le porte,
  et il nomme la source qui a réellement servi.
- **Trois étiquettes d'API inscrites dans le code, pas dans les intentions** :
  Celestrak au plus une fois par jour derrière un cache SD, le relevé des
  lancements plafonné à la limite de la source la plus lente, et chaque vue
  conditionnée à une horloge synchronisée.
- **Ce qu'il refuse de faire** : un `norad` d'espace profond est rejeté plutôt
  qu'approximé (SGP4 seul n'y est pas valide), un TLE de plus de 14 jours grise
  la vue avec son âge, et un cache de lancements de plus d'une heure affiche
  « au hh:mm » au lieu de présenter un T-0 glissé comme direct.
- Spec : `docs/ROADMAP.fr.md §5`. Guide : `docs/guests/SPACE.fr.md`.
