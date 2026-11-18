> [English](LED-FLUID.md) · **Français**

# led-fluid — du liquide dans une boîte

Un `.bin` invité qui fait de la CoreS3 un liquide qu'on incline. Un fluide à
particules coule sous la vitre, sa gravité EST l'inclinaison de la carte, et il
est peint en grille de pastilles rondes — le look d'un afficheur à LED. Secouez,
ça éclabousse ; posez, ça se range.

Rien là-dedans n'a besoin du réseau : le WiFi est là parce que `SceGuest`
l'apporte, et parce qu'une page de réglages se remplit plus facilement qu'un
panneau de 320 px.

## L'écran

La zone du fluide est toute la dalle, 320×240. Chaque cellule de la grille est
une pastille, et une pastille dit deux choses à la fois :

| Ce qu'on voit | Ce que ça veut dire |
|---|---|
| Saturation — pâle à couleur franche | **Densité** : la quantité de fluide entassée dans la cellule |
| Valeur — sombre à lumineux | **Vitesse** : à quelle vitesse ça bouge là |

Une nappe épaisse et calme rend donc une couleur franche et sombre, une gerbe
fine et rapide rend pâle et lumineux — l'éclat blanc que l'œil attend déjà d'une
éclaboussure. La teinte et un **plafond** de saturation vous appartiennent
(swipe gauche) ; le fluide ne fait que se déplacer sous ce plafond.

Un réglage de **traînée** fait s'éteindre une cellule au lieu de l'effacer :
c'est ce qui donne à une goutte un sillage plutôt qu'une file de points
stroboscopiques.

## Les gestes

| Geste | Ce qui se passe |
|---|---|
| **Incliner le robot** | la gravité suit — c'est tout le principe |
| **Le secouer** | une éclaboussure ; le tourner fait tourbillonner le fluide |
| **Tap sur le fluide** | une impulsion au doigt, qui repousse le fluide |
| **Swipe droite** | le panneau de réglages : la physique, puis les interrupteurs de rendu |
| **Swipe gauche** | le rectangle de couleur : teinte, saturation, luminosité |
| **Swipe gauche ou droite dans un panneau** | retour au fluide (les deux sens marchent) |
| **Long swipe bas** | retour au companion (la sortie `SceGuest`) |

**La sortie est coupée tant qu'un panneau est ouvert.** Sinon un curseur tiré
vers le bas proposerait de reflasher le robot, ce qu'un geste mal lu peut faire
de plus cher ici.

## Les réglages

Le panneau, la page web et la carte lisent **une seule table**. Ajouter un
réglage, c'est y ajouter une ligne ; aucun des trois ne peut dériver des autres.

### Swipe droite, onglet PHYSIQUE

| Clé | Plage | Défaut | Ce que ça fait |
|---|---|---|---|
| `particles` | 60-400 | 240 | la quantité de fluide — et le coût CPU principal. Plafonné par `dot_pitch` : un grain qu'on ne peut pas tenir à l'écart n'est pas créé |
| `dot_pitch` | 8-20 px | 12 | le pas de la grille, et la taille du grain avec : petites pastilles, fluide fin, et davantage |
| `viscosity` | 0-100 | 30 | de l'eau en bas, du miel en haut |
| `gravity` | 0-200 % | 100 | 0 = apesanteur, et ça vaut le détour |
| `bounce` | 0-90 % | 25 | ce qu'un choc contre une paroi rend |
| `trail` | 0-95 % | 60 | le temps qu'une pastille met à s'éteindre |
| `gyro_gain` | 0-200 % | 100 | la force avec laquelle une rotation devient tourbillon |

Sous les curseurs, cinq **presets** — eau, miel, mercure, lave, apesanteur. Ils
posent la physique et **jamais la teinte** : un preset qui déferait le choix
fait au panneau d'à côté serait une commande qui en combat une autre.

### Swipe droite, onglet RENDU

Six interrupteurs, tous **à l'arrêt** par défaut — comme tout automatisme
intrusif de ce dépôt, et, pour les trois derniers, parce qu'à l'arrêt est le
mapping dont le robot a réellement besoin :

| Clé | Ce que ça fait |
|---|---|
| `leds` | les douze WS2812 du K151 répètent le fluide — douze secteurs angulaires, chacun prenant sa cellule la plus dense, via le même passage à la couleur que les pastilles |
| `sound` | un petit son à l'éclaboussure |
| `auto_bright` | la luminosité de la dalle suit le capteur de lumière LTR-553 |
| `tilt_swap` | échanger les deux axes de l'accéléromètre |
| `tilt_inv_x` | inverser l'horizontal |
| `tilt_inv_y` | inverser le vertical |

Les trois interrupteurs d'inclinaison sont ici plutôt qu'enterrés dans la carte parce que
celui qui voit de quel côté part le fluide **tient le robot**, il ne lit pas
`/config` sur un portable. L'incliner et toucher un interrupteur, c'est un seul
geste ; l'alternative, c'était un redémarrage par hypothèse.

Les deux puces sont **sondées au démarrage**, jamais supposées. Sur une carte où
l'une manque ou est morte, son interrupteur reste disponible et ne fait rien, et
la trace série à 115200 dit laquelle des deux a répondu.

**L'anneau est éteint trois fois plutôt qu'une**, et chacune correspond à un cas
où il est resté allumé : une WS2812 mémorise sa dernière couleur et la garde à
travers un redémarrage comme à travers un reflash, donc un bin qui se contente
d'arrêter d'écrire laisse douze LED allumées sur un robot passé à autre chose.
Elles sont donc éteintes quand l'option est **décochée** (et pas seulement
ignorée), quand le bin **rend la main** au companion par n'importe laquelle de
ses sorties, et une fois au **démarrage** — cette dernière parce que configurer
la ligne de données de l'expandeur suffit à faire latcher du bruit à une chaîne
jamais écrite, ce qui s'est vu sur le robot sous la forme de douze LED bloquées
en blanc.

### Swipe gauche, le rectangle de couleur

X est la teinte, Y le **plafond** de saturation, saturé en haut. Dessous, la
luminosité de la dalle, et une rangée de six pastilles d'exemple montrant ce que
le choix donnera vraiment en fluide — d'une cellule vide à une cellule pleine.
Le rectangle montre le choix ; les pastilles montrent la conséquence.

### Sur aucun panneau

`hue`, `sat` et `bright` appartiennent au rectangle de couleur. Elles sont quand
même sauvegardées et présentes sur `/config` — elles n'ont simplement pas de
second widget sur le panneau de réglages, parce que deux commandes qui écrivent
une même valeur se contredisent à l'écran dès qu'on n'en bouge qu'une. C'est
toute la règle d'appartenance à cette liste : **ce qui a déjà un widget**.

### Le mapping d'inclinaison

**Les trois sont à faux, et un robot correct les montre à faux.** Le mapping dont
le K151 a réellement besoin — `gx = -accel.x`, `gy = +accel.y` — a été mesuré sur
le robot debout, et il vit dans le code qui lit le vecteur, pas dans le défaut
d'un interrupteur. Porté par `tilt_inv_x = 1`, il se comportait à l'identique et
se lisait très mal : le paramétrage s'ouvrait sur un robot qui fonctionnait
correctement tout en annonçant qu'un axe avait été inversé, si bien que le seul
état honnête de la machine avait l'air d'un état déjà bricolé. Un interrupteur
doit vouloir dire *s'écarter de ce qui est juste*.

Ils restent réglables parce qu'un K151 n'est pas le seul corps où ceci peut
finir : si le fluide part du mauvais côté sur le vôtre, c'est une ligne de yaml,
pas une recompilation.

Le projet avait déjà un mapping d'accéléromètre
([`hardware/PERIPHERALS.fr.md`](../hardware/PERIPHERALS.fr.md)) — ce qu'il
n'avait pas, c'est une vérification des **signes** dans le plan contre une
direction physique connue. Seul `accel.z` en portait une, venue du geste
face contre table. `accel.x` et `accel.y` étaient validés comme *comportement* —
la cible d'inclinaison du companion tient sur une pente au lieu de retomber à
zéro — ce qui est une autre affirmation : une cible tenue dans le mauvais sens
est tenue tout aussi fermement. Regarder de quel côté tombe un liquide est le
premier test de ce robot qui lise ces deux signes directement.

## Le fichier de configuration

`/stackchan-companion/ledfluid.yaml`, plat, une clé par ligne — les mêmes clés que la
table ci-dessus. Le bin **réécrit le fichier entier** à chaque sauvegarde : les
valeurs survivent, les commentaires écrits à la main non.

```yaml
particles: 240
dot_pitch: 12
viscosity: 30
gravity: 100
bounce: 25
trail: 60
gyro_gain: 100
hue: 200
sat: 85
bright: 120
leds: 0
sound: 0
auto_bright: 0
tilt_swap: 0
tilt_inv_x: 0
tilt_inv_y: 0
```

Compiler, déposer, lancer — le robot tournant sous son companion :

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e led-fluid
curl -X POST "http://<ip>/api/bins" -F "file=@.pio/build/led-fluid/firmware.bin;filename=led-fluid.bin"
curl -X POST "http://<ip>/api/sd/put?path=/stackchan-companion/ledfluid.yaml" -F "file=@sdcard/stackchan-companion/ledfluid.yaml"
curl -X POST "http://<ip>/api/bins/launch?name=led-fluid.bin"
curl -X POST "http://<ip>/api/bins/stop"      # une fois l invite demarre
```

Sans carte, les réglages s'appliquent et disparaissent au redémarrage suivant ;
l'écran « pas de carte » le dit avant que le fluide ne dessine quoi que ce soit.

## La physique

Le solveur est un **fluide viscoélastique à particules** (double relaxation de
densité). Il ne porte aucun solveur de pression itératif, reste stable à grand
pas de temps, et prend la viscosité comme paramètre explicite plutôt que comme
effet de bord — ce qui compte, puisque la viscosité est le bouton dont ce bin
parle. Les voisins viennent d'une grille uniforme, donc le coût est linéaire en
nombre de particules.

**Un grain est une pastille, et deux grains ne tiennent jamais la même place.**
Le diamètre d'une particule EST le pas de la grille, donc un grain allume une
LED ; le fluide se tient écarté par une passe de non-pénétration qui s'exécute
APRÈS les parois, dernier mot sur la position d'un grain. La pression seule ne
peut pas tenir cette promesse — c'est une force de rappel, elle cède donc
toujours un peu sous la gravité, et « un peu » à 240 grains, c'est une colonne de
pastilles empilées les unes sur les autres. Un écart d'un diamètre est une
contrainte, et la gravité n'a rien à lui opposer.

Cette contrainte ne peut pas inventer de place : le **nombre de grains est donc
plafonné par le pas**. Une grille grossière veut dire moins de grains, et le
curseur du panneau est un souhait auquel `count()` répond par ce que l'écran
peut réellement tenir écarté. À 20 px, le maximum de 400 devient 153.

Dans le solveur, toute longueur est un multiple du pas et toute raideur est une
accélération en px/s², si bien que chacune se compare au seul nombre qui décide
si un liquide se tient debout : la gravité, 900 px/s² à 100 %. Rien n'y est
exprimé dans les unités de l'article dont il vient.

Le solveur est un en-tête pur, sans Arduino dedans, et il est testé sur PC
(`test/test_fluid`) : les particules ne sortent jamais de la boîte, une secousse
violente ne traverse pas une paroi, dix mille pas ne produisent aucun NaN, la
même graine rejoue le même film, plus de viscosité dissipe plus d'énergie, le
fluide posé garde un **volume** — un corps épais de plusieurs pastilles, pas une
ligne le long d'un bord — et deux grains ne se rapprochent jamais à moins des
trois quarts d'un diamètre.

**Le coût d'un pas est plafonné**, et ce n'est pas une optimisation. Une grille
de voisinage ne borne le nombre de paires que tant que les particules sont
étalées, et la gravité passe sa vie à faire l'inverse : dès qu'un tas tient dans
une seule cellule, toutes les paires redeviennent voisines et le pas redevient
quadratique. Les voisins examinés par particule sont donc plafonnés à 24 —
environ deux fois un voisinage plein à la densité de repos, donc un fluide
ordinaire ne s'en aperçoit pas, tandis qu'un tas effondré est approché plutôt
qu'intégré exactement.

## Le budget de frame

Deux surfaces, volontairement. Le fluide est peint **directement à l'écran**,
cellule par cellule *changée* : une cellule dont l'apparence quantifiée n'a pas
bougé n'est pas redessinée, et c'est ce qui fait tenir la frame. Les panneaux
sont composés dans un sprite PSRAM poussé d'un coup, parce qu'ils changent
rarement et qu'un sprite est ce dans quoi l'aide au texte différé sait peindre.

Mesuré sur le matériel au pas le plus fin (8 px, grille 40×30), gravité pilotée
en cercle pour que le fluide ne se pose jamais :

| | |
|---|---|
| Peinture pire | 16-18 ms |
| Boucle pire | 26 ms, avec une pointe occasionnelle à 38 ms venue de la pile réseau |
| Frames peintes | 50 par seconde |

C'est pour cela que le plancher du pas est à 8 px et non 6 : à 6 px la grille
fait 2120 cellules au lieu de 1200 et le budget casse.

La simulation tourne sur le **cœur 0** et ne touche aucun bus — toute
transaction I2C de ce bin se fait dans `loop()` sur le cœur 1, ce qui lui permet
de se passer entièrement du verrou de bus partagé plutôt que d'ajouter un verrou
de plus à tenir correctement. Elle **cède la main sans condition** : un pas qui
déborde resynchronise son échéance et sert un plancher d'un tick, car une tâche
qui ne cède que lorsqu'elle a fini à temps cesse de céder dès la première fois
où elle n'a pas fini — et emmène avec elle la tâche inactive de son cœur.

## Notes de conception

- **Trois en-têtes purs, testés nativement** comme `engine/` et `behavior/`
  (règle 7) : `fluid.h` (le solveur), `look.h` (couleurs et préréglages),
  `panels.h` (géométrie des panneaux, hit-test, calcul des curseurs).
  `main.cpp` ne garde que ce qui touche le matériel.
- **Une seule table `PARAMS[]`, quatre consommateurs.** Clé, libellé EN/FR,
  type, bornes et pas vivent une fois dans `panels.h` et sont lus par le dessin
  du panneau, le hit-test tactile, le chargement/l'écriture yaml et les appels
  `addSetting()` de `/config` : un paramètre ne peut pas exister sur le panneau
  et manquer sur la page web.
- **La simulation passe ses frames à `loop()` par une boîte aux lettres à deux
  tampons** : un producteur, un consommateur, la discipline Brain/Renderer
  d'A2.5 réécrite dans le bin (un invité n'inclut pas `src/engine/`).
  `sce::CoopStop` est câblé sur cette tâche avec un `windowMs` court (2000) :
  un pas dure moins d'une milliseconde, il n'y a pas de longue requête à
  vider.
- **Repeindre avec hystérésis** : une cellule n'est redessinée que si sa
  couleur quantifiée a changé, avec une marge pour qu'elle ne clignote pas
  entre deux valeurs. `fillCircle`, jamais une primitive anti-aliasée, un seul
  point d'appel par forme (A2.22, vérifié dans l'ELF par `check-a222.py`).
- **La sortie par glissement vers le bas est neutralisée tant qu'un panneau
  est ouvert** : les gestes du bin vivent sous `EXIT_PX` (`common/Gesture.h`),
  et sans cette garde un curseur tiré vers le bas reflasherait le robot.
- **L'écho WS2812** demande un adressage par LED. La moitié pure (`scale565`,
  la charge 12×RGB565) est `firmware/common/Py32Leds.h`, partagée avec le
  companion ; la transaction `Wire` reste de chaque côté (le companion sous
  `i2cbus::Guard`, le bin sans, tout son I2C étant dans une seule tâche). Même
  découpe que la règle 17 : le calcul partagé, l'E/S bornée locale.
- **Les préréglages ne règlent que la physique**, jamais la teinte : un
  préréglage qui défait le panneau couleur voisin serait un contrôle qui en
  combat un autre.
- **L'écriture des réglages est synchrone**, contrairement au radar : elle est
  courte, ne tient aucun verrou, et la simulation tourne sur l'autre cœur, donc
  `onSettingsSaved` rend le vrai résultat et une carte pleine donne un
  `?ko=1` honnête.

Questions ouvertes :

- Le passage des axes de l'accéléromètre à ceux de l'écran est validé pour les
  vitesses du gyro (`CONVENTIONS §3`) mais pas pour le vecteur gravité, d'où
  les trois clés `tilt_*` réglables à chaud. Confirmer les signes demande
  quelqu'un qui tient le robot.
- Un pas coûte plus de temps réel que son arithmétique ne l'explique
  (25-30 ms pour 8 000-10 000 évaluations de paires). La cause probable est la
  préemption : la tâche est en priorité 1 sur le cœur 0, sous le WiFi et lwIP.
  Sans conséquence (50 fps), mais le chiffre ne doit pas être cité comme un
  coût tant que les deux ne sont pas séparés.
- Le plafond de 400 particules est borné par le plafond de voisins plutôt que
  mesuré pour lui-même.

## Seconde carte

`led-fluid-fire` — la **même source**, en application autonome sur un M5Stack
Fire : pas de K151, pas de companion, trois boutons au lieu d'une dalle
tactile.

```powershell
pio run -e led-fluid-fire -t upload --upload-port (.\scripts\dev\find-port.ps1 -Board fire)
```

Ce bin a longtemps été annoncé non portable *parce que son interface est bâtie
sur le glissement* — un swipe ouvre un panneau, un drag déplace un curseur, une
tape éclabousse le liquide à l'endroit touché. C'était vrai de l'**interface**,
pas du bin. Le portage ne rejoue donc pas les mêmes gestes sur des boutons : il
redit ce que chaque geste **voulait dire** et le place sur ce que possède un
Fire. Le vocabulaire est
[`firmware/led-fluid/input.h`](../../firmware/led-fluid/input.h), pur et testé
nativement (`test_fluidinput`), et `applyEvent()` dans `main.cpp` en est
l'unique consommateur — le tactile et les boutons sont deux producteurs, comme
sur le radar et le bin spatial.

Le mapping ne dépend pas de l'écran où l'on se trouve, et c'est ce qui le rend
mémorisable :

| | appui court | appui long |
|---|---|---|
| **A** | précédent / diminuer | ouvrir le rectangle de couleur |
| **B** | agir | retour au fluide |
| **C** | suivant / augmenter | ouvrir le panneau de physique |

« Parcourir » et « agir » sont des rôles, et chaque écran les dépense une fois :
sur le panneau de réglages A et C déplacent la valeur sous le curseur pendant
que B passe à la ligne suivante (et bascule d'onglet en bout de liste — sans
bouton d'onglet dédié, ce passage *est* la commande d'onglet) ; sur le rectangle
de couleur A et C tournent la teinte et B fait avancer la saturation ; sur le
fluide B l'éclabousse. Un build à boutons dessine un cadre autour de la ligne
pointée : trois boutons agissant sur une sélection invisible ne sont pas une
commande.

**Une seule chose diffère vraiment entre les deux cartes, et le vocabulaire le
dit au lieu de le masquer** : éclabousser le liquide demande un point, et un
bouton n'en a pas. Une tape éclabousse là où le doigt s'est posé ; un appui
éclabousse le centre.

Ce que le Fire a et n'a pas : il a bien un IMU (MPU6886), donc l'inclinaison —
tout le propos du bin — fonctionne. Il est monté autrement que celui du CoreS3,
ce à quoi servent exactement les trois cases `tilt_*`. Il n'a **pas d'anneau
WS2812** : celui-ci vit sur le corps K151, derrière un expandeur PY32, et le
profil déclare son absence par `SCE_HAS_PY32=0` au lieu de le sonder. C'est la
seule capacité ici qui soit un *drapeau* et non une sonde, et la raison est
nette — sonder impose d'appeler `Wire1.begin(12, 11)` d'abord, or sur un ESP32
classique les GPIO 6-11 sont la **flash SPI**. La broche 11 n'y est pas une
broche libre, c'est celle depuis laquelle le programme est lu. Une capacité
qu'on ne peut pas interroger sans danger se déclare. Le capteur de lumière,
lui, reste sondé (`M5.In_I2C`), comme dans le bin spatial.
