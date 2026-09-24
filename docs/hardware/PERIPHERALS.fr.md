> [English](PERIPHERALS.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# Les pièces, une par une

Chaque entrée ci-dessous dit à quoi sert la pièce, comment on l'atteint, ce
qu'il a fallu étalonner ou contourner, et **comment elle échoue** — ce dernier
point étant celui qui coûte le plus de temps quand il manque.

Pour ce que coûte le fait de *parler* à l'une d'elles, voir
[`BUSES.fr.md`](BUSES.fr.md).

## Servos SCS0009 — la nuque

Deux servos série sur UART2, **ID 1 = lacet**, **ID 2 = tangage**. Étalonnage
mesuré sur le robot :

| Axe | Valeur | Source |
|---|---|---|
| Centre lacet | **166°** (raw 463) | mesuré — tête face à l'observateur |
| Amplitude lacet | **±130°** | dérivée, voir ci-dessous |
| Amplitude lacet *réflexe* | **±40°** | délibérée, voir ci-dessous |
| Tangage HOME | **93°** | mesuré ; était 103, relevé pour laisser 10° de marge pour BAISSER la tête |
| Tangage min | **19°** = 85° officiel | spec M5Stack |
| Tangage max | **99°** = 5° officiel | spec M5Stack |

### Pourquoi ±130° alors que la tête tourne physiquement sur 360°

C'est le nombre le plus contre-intuitif du robot, et la réponse est que **rien
de mécanique ne l'arrête — c'est notre propre chemin de commande qui le fait.**

Trois faits, dans l'ordre où ils comptent :

1. **Le servo n'a aucune limite horizontale.** La page StackChan de M5Stack le
   dit franchement : *« No angle restriction is required for the X-axis »*. La
   restriction qu'ils énoncent, et haut et fort, porte sur l'axe Y — et
   celle-là s'accompagne d'un avertissement de calage et de dommage permanent.
   Le SCS0009 est spécifié en 360° continus sur l'axe horizontal.

2. **Mesuré couple relâché, la tête fait plusieurs tours complets.** Aucune
   résistance, aucune butée. Le câble ne croise **pas** l'axe de lacet ; une
   note antérieure de ce projet supposait le contraire, et cette supposition
   était tout simplement fausse.

3. **Ce qui borne réellement, c'est `writeDeg`.** Notre chemin de commande
   mappe une plage `0–300°` sur le registre de position 10 bits du SCS0009 et
   borne là :

   ```cpp
   if (deg < 0) deg = 0; if (deg > 300) deg = 300;
   uint16_t pos = (uint16_t)(1023.0f - deg * (1023.0f / 300.0f));
   ```

   Avec le centre à 166, l'enveloppe atteignable est donc **+134 / −166**. La
   plus large plage *symétrique* qui y tienne est **±134**.

Donc ±130, c'est `±134` moins **quatre degrés de marge**, pour qu'un extrême
commandé n'atterrisse jamais exactement sur la borne de conversion. Il n'y a
aucun risque de calage ici — rien n'est pressé contre une butée, contrairement
aux limites de tangage — la marge ne fait que garder l'arithmétique honnête.

**Pour aller plus loin il faut quitter le mapping 0–300 tout entier**,
c'est-à-dire piloter le mode multi-tours du servo. C'est un autre chemin de
protocole, pas une constante plus grande : relever `YAW_RANGE` au-delà de 134
n'achète rien, puisque `writeDeg` borne avant. Les 360° que vous pouvez faire à
la main sont bien réels, ils ne sont simplement pas adressables par le registre
de position que nous utilisons.

### Pourquoi les réflexes n'ont droit qu'à ±40°

`YAW_REFLEX_RANGE` est une **seconde borne, plus étroite**, et elle existe
parce qu'élargir la première a cassé quelque chose. Deux chemins réflexes
parcourent le lacet *relativement*, sans borne propre — le suivi sonore et le
demi-tour post-sursaut font tous deux `moveTo(yawDeg() + step, ...)`. À ±40, une
telle marche s'arrêtait après un pas ou deux. À ±130, un bruit soutenu hors axe
emporte la tête à 130° du centre, écran tourné à l'opposé de la personne qu'il
est censé regarder.

Une danse *demande* une grande amplitude ; un réflexe doit seulement **regarder
vers** quelque chose. Les commandes absolues — danses, `POST /api/servo` —
gardent la plage complète.

### Les limites de tangage, elles, sont dangereuses

La spec officielle M5Stack dit *« Y-axis recommended within 5 ~ 85°. Operating
at extreme angles may cause servo stall and permanent damage. »* Leur repère
est l'inverse de notre repère brut mesuré (`raw ≈ 104 − officiel`), ce qui
donne nos 19..99. Les **anciennes bornes 15/103 tenaient la butée mécanique**
pendant les danses NOD, SHY et cry — c'est un servo calé, et un servo calé
meurt.

Ne jamais tenir une butée de tangage. `PITCH_DOWN_MAX` (= 6) est dérivé de ces
bornes et ne doit jamais être écrit en dur ailleurs : au-delà, la cible dépasse
`PITCH_MAX`, `ServoMotion` la borne, et la condition de repos tête-au-home ne
converge jamais — une boucle de re-commande du servo.

### Le couple, et la libération qu'un redémarrage ne peut pas défaire

`EnableTorque(id, 0)` est un registre **dans** le servo, et les SCS0009
démarrent couple **activé**. Une libération ne survit donc que jusqu'au reset
suivant : confiez le robot à un bin invité, le redémarrage réaffirme VM_EN, les
servos reviennent alimentés, et la libération soigneusement acquittée est
effacée.

La seule libération qui tienne est de couper **VM_EN** sur le PY32, parce que
le PY32 conserve l'état de ses GPIO à travers notre reset.

## BMI270 — l'IMU

Lu via `M5.Imu.getImuData()` — **jamais** `getAccel()`. `hal/ImuReader.h`
livre `headVel` déjà exprimé en °/s dans les **axes écran, avec des signes
viewer-centric** : le VOR n'a plus aucun mapping à faire.

Le mapping est **validé sur matériel**, et chaque axe et signe est réglable à
chaud sans reflasher (`gyro_yaw_axis/sign`, `gyro_pitch_axis/sign` ; la console
a des boutons « calibration VOR » et la télémétrie expose `gX/gY/gZ`).

| Capteur | Axe écran | Défaut validé |
|---|---|---|
| `accel.x` | X (inclinaison gauche/droite) | +(accX − ligne de base de repos) × `TILT_SENS_X` 0,67 |
| `accel.y` | Y (avant/arrière, porte la gravité au repos) | +(accY − ligne de base de repos) × `TILT_SENS_Y` 0,86 |
| *direction physique de ces deux-là* | **+X = la droite de l'observateur, +Y = le haut de l'écran** | mesuré le 22-08-2026 avec `led-fluid` : un liquide tombe là où pointe la gravité, donc le vecteur gravité en pixels écran vaut `(-accel.x, +accel.y)` |
| `gyro.y` | rotation de lacet (verticale du monde = Y capteur) | + (`gyro_yaw_axis=1`, `sign=+1`) |
| `gyro.x` | rotation de tangage | + (`gyro_pitch_axis=0`, `sign=+1`) |
| `accel.z` | normale à l'écran (= axe de roulis du gyro) | brut, seuil ±0,75 g → face haut/bas (`ImuReader::FaceOrient`), signe validé sur HW |

⚠ **Les SIGNES de l'accéléromètre dans le plan n'avaient jamais été lus
directement** avant cette mesure, et la distinction compte. `accel.z` en a
toujours eu un, venu du geste face contre table. Les deux autres étaient validés
comme *comportement* — « inclinaison statique tenue »
(`validation/VALIDATION.fr.md`), c'est-à-dire que la cible d'inclinaison du
companion reste en place sur une pente au lieu de retomber à zéro — ce qui ne dit
rien de sa direction : une cible tenue à l'envers est tenue tout aussi fermement.
Regarder de quel côté tombe un liquide est le premier test de ce robot qui les
lise directement.

Ce que cela tranche, et ce que cela ne tranche pas : les axes physiques sont
désormais connus, donc `ImuReader::_tilt` peut être raisonné au lieu d'être
deviné. Savoir si ses signes sont ceux que le *comportement* veut est une autre
question — une vraie réponse otolithique contre-tourne (pencher à droite → yeux à
gauche), une réponse cartoon dérive avec la chute, et les deux veulent des signes
opposés. Les deux axes y répondaient autrefois différemment : X contre-tournait
pendant que Y suivait la chute, une asymétrie que rien ne pouvait attraper tant
que les directions physiques restaient inconnues. **Les deux contre-tournent
désormais**, ce que la documentation de cette classe a toujours affirmé et ce que
fait le réflexe vestibulo-oculaire. Penché en arrière, le regard descend avec la
tête.

Les deux axes du plan portent aussi une **ligne de base de repos**, et X n'en
avait aucune — invisible justement parce qu'`accel.x` vaut nominalement zéro à la
verticale, si bien que tout biais de montage en X passait directement dans le
regard sous forme d'un décentrage permanent qu'aucune calibration ne pouvait
retirer. La ligne de base se mesure sur des ticks **calmes** au démarrage (un
robot démarré dans une main ne calibre plus la main), puis n'est suivie que tant
que la lecture reste *près* d'elle : la dérive thermique erre autour de la pose
de repos, une inclinaison tenue s'en éloigne, seule la première est suivie. Un
robot qui *repose* sur une pente se calibre toujours sur cette pente, et c'est la
bonne réponse — cette pente est sa pose de repos. `/api/sensors` publie
`imu_cal` : `0` calibration en cours (le canal d'inclinaison lit zéro), `1`
calibré, `2` calibré **sur l'échéance** — le repli de vingt secondes s'est
déclenché faute de tick calme, donc la pose de repos est une estimation faite en
mouvement. Un regard décentré trouve son explication là plutôt que dans le VOR.

⚠ **`gyro.z` est la normale à l'écran, donc le ROULIS.** Y mapper le lacet rend
le VOR muet en rotation — la mauvaise configuration classique sur cette carte.

Efférence servo : le VOR reste actif pendant les danses et le head-follow.
Seule la *détection de secousse* est inhibée tant que le mouvement est
auto-généré (la garde `selfMotion`) — sans quoi chaque danse déclencherait
`Scared`.

**Le double-tap est détecté en logiciel** (un pic de `|accel|`) : M5Unified
n'expose pas l'interruption tap matérielle du BMI270.

## BMM150 — le magnétomètre, et pourquoi il ne sert pas

Atteint par le même `M5.Imu` (`imu_data_t.mag`). `ImuReader::headingDeg()`
publie un cap absolu 0–360° en télémétrie.

**Il n'est pas fusionné dans le VOR, et ne le sera pas.** Le verdict est ❌ et
il a été mesuré, pas supposé : le capteur voit surtout les **aimants des servos
du corps** à travers un gradient abrupt — **370 µT de dépendance au lacet de
tête pour 80° de mouvement**, six fois le champ terrestre. Pire, la lecture est
**irreproductible à pose commandée identique** (±100–175 µT entre répétitions :
jeu et hystérésis des servos), avec des marches d'environ 50 µT selon l'état du
couple, et danser triple le bruit.

Un étalonnage par carte de poses ne peut pas converger sur une telle
irreproductibilité. Le code et ses trois tests natifs restent, `vor_mag_alpha`
reste à **0**, et la seule voie restante est un magnétomètre **externe** monté
loin des moteurs (Grove).

## AXP2101 — le PMIC

Atteint via `M5.Power` sur le bus *interne*, pas `Wire1`. Il alimente l'écran
et la carte SD, ce qui explique qu'il doive venir en premier au démarrage.

- `getBatteryLevel()` — 0–100, ou −1 quand le PMIC ne peut pas répondre.
- `isCharging()` / `getVBUSVoltage()` — distinguent secteur et USB d'une simple
  lecture de niveau. VBUS au-dessus de 3000 mV compte comme présent ; absent,
  cela lit ~0–100 mV.
- `setExtOutput(true)` — requis sur le K151, qui n'a pas de base takao.

⚠ **Ne jamais appeler `setBrightness()` par frame.** C'est une transaction I2C
sur le bus partagé. La luminosité a exactement une cible, calculée en un seul
endroit (capteur *ou* manuel) puis postée au renderer.

## INA226 — la jauge de batterie

`Wire1`, adresse `0x41`, détecté par l'identifiant fabricant Texas Instruments
(`0x5449`). Il mesure la **tension de bus** au nœud batterie (1,25 mV/LSB) et
la **tension de shunt** (2,5 µV/LSB, signée, proportionnelle au courant).

La tension de bus est exploitable **sans étalonnage**, contrairement au
registre de courant : c'est donc elle qui est exposée — comme une lecture qui
*complète* l'estimation de l'AXP2101 plutôt qu'elle ne la remplace. L'AXP2101
reste l'autorité sur le niveau de batterie ; la tension INA226 s'affaisse en
charge et n'est qu'indicative.

**Convention de panne : `busVoltage()` rend −1, jamais 0.** Un appelant doit
pouvoir distinguer « pas de lecture » de « batterie à plat ».

## LTR-553ALS — la lumière ambiante

`In_I2C`, adresse `0x23`. Si cette pièce mérite une section entière, c'est
qu'elle est **quasiment occultée par le boîtier du K151** : un bureau éclairé
lit **0 count au gain 1×** et **2 counts au gain 8×**.

L'étalonnage qui la rend utilisable, ce sont trois nombres qui ne fonctionnent
qu'ensemble :

| Réglage | Valeur | Signification |
|---|---|---|
| `ALS_CONTR` | `0x1D` | gain **96×**, actif |
| `ALS_MEAS_RATE` | `0x1B` | intégration 400 ms sur une cadence de 500 ms |
| courbe de niveau | `ln(4096)` | normalisée sur la plage que cet init produit réellement |

Changez-en un et les deux autres sont faux. C'est pourquoi l'étalonnage vit une
seule fois dans `firmware/common/Ltr553.h`, partagé par le companion et les
bins invités.

Ce qui n'est délibérément **pas** partagé, c'est la politique de transaction,
parce qu'elle diffère vraiment : le companion lit un registre par `Guard` pour
que le Brain puisse glisser ses lectures IMU à 100 Hz entre les octets, tandis
qu'un bin invité — qui n'a ni Brain ni verrou de bus — lit les quatre octets de
données en une rafale.

**Convention de panne : −1, jamais 0**, parce que 0 est une lecture légale qui
signifie l'obscurité. S'être trompé là-dessus a un jour endormi le robot en
plein jour : une lecture échouée prise pour « sombre », et `dark_sleepy` a fait
le reste.

## Si12T — le tactile de tête

`Wire1`, adresse `0x68`, 3 zones, porté depuis le firmware du fabricant. La
sensibilité est un niveau 0–7 (défaut 3).

`OUTPUT1` (`0x10`) empile les trois zones sur 2 bits chacune — arrière, milieu,
avant — avec les valeurs 0 = aucune, 1 = faible, 2 = moyenne, 3 = forte. La
position est un barycentre :

```
pos = (-100 × ch0 + 0 × ch1 + 100 × ch2) / (ch0 + ch1 + ch2)
```

soit −100 à l'arrière, 0 au milieu, +100 à l'avant, ce qui transforme une
caresse en `SWIPE_FORWARD` / `SWIPE_BACKWARD`.

**L'ordre d'init est obligatoire** : `Wire1.begin(12,11)` → PY32 VM_EN
(300 ms) → servo → Si12T. `hal/Board.h` s'en charge.

`Wire Error 263` sur la console série est un **timeout Si12T récurrent** et il
est bénin — bruit de log connu, pas une panne à poursuivre.

## PY32 — expandeur d'E/S : le rail servo et les LED

`Wire1`, adresse `0x6F`. Il fait deux métiers sans rapport.

**VM_EN (PY32 GPIO 0)** est le rail d'alimentation des servos : direction →
pull-up → sortie HAUTE, puis 300 ms de stabilisation. Il doit être actif avant
`servo.begin()`. La puce démarre lentement (~200 ms), donc `detect()` réessaie
la lecture de version jusqu'à 1,2 s avant de la déclarer absente — une version
à `0x00` ou `0xFF` signifie « pas là ».

**Douze LED WS2812C** pendent au GPIO 13 du PY32 (bit 5 des registres `_H`). Le
protocole n'est pas documenté publiquement ; il a été lu dans le firmware du
fabricant :

| Registre | Rôle |
|---|---|
| `0x24` `REG_LED_CFG` | bits 0–5 = nombre de LED, bit 6 = REFRESH |
| `0x30+` `REG_LED_RAM` | 2 octets par LED, **RGB565 petit-boutiste** |

Séquence : `setLedCount(12)` → écrire les couleurs → `refreshLeds()`.

**Les deux barres sont PERPENDICULAIRES à l'écran** : les six LED de chacune
s'échelonnent de l'avant vers l'arrière, pas dans la largeur du robot. C'est le
fait contre lequel toute animation par LED doit être conçue — l'axe disponible
est la PROFONDEUR. La direction du regard et la course d'une paupière sont
gauche/droite et haut/bas : ni l'une ni l'autre n'a où s'inscrire ici. Ce qui
s'y inscrit, c'est un balayage le long du corps, un niveau, un avancement, ou
une inclinaison avant/arrière. Cela décide aussi de qui voit quoi : de côté les
six se lisent, de face elles se superposent en une seule luminosité.

**Chacune des douze prend sa propre couleur**, et l'a toujours pu : la RAM
couleur fait douze entrées, une par LED. Seul le pilote prétendait le
contraire, n'offrant que les douze-pareilles et **deux barres de six** (0–5 à
gauche, 6–11 à droite, viewer-centric) — les barres étant ce dont le companion
a besoin, puisqu'il cale la luminosité de chaque barre sur la hauteur de l'œil
correspondant. `setLedsRaw()` écrit les douze individuellement ; les deux
aides ci-dessus sont désormais deux appelants de cette fonction. L'invité
`led-fluid` s'en sert pour découper l'écran en douze tranches verticales et
donner à chaque LED la couleur de la cellule la plus dense de sa tranche.

Un protocole antérieur `[0xAA, r, g, b, lum]` était deviné et tout simplement
faux — il était **acquitté en I2C sans rien allumer**, ce qui vaut d'être
retenu : sur ce bus, une écriture acquittée prouve seulement qu'une puce est
là.

La couleur des LED est toujours `Renderer::eyeColorRgb()` — la couleur
réellement affichée, transition comprise — jamais recalculée côté LED.

**Une WS2812 mémorise, donc l'anneau est AFFIRMÉ au démarrage et pas seulement
piloté.** Elle garde sa dernière couleur jusqu'à ce que quelque chose en écrive
une autre, à travers un redémarrage comme à travers un reflash — il n'existe
aucun état par défaut à l'allumage. Le chemin d'exécution n'éteint les LED que
s'il les allumait lui-même, ce qui ne dit rien de ce que le firmware
*précédent* a laissé : un companion qui démarre avec l'option `leds` à 0
n'écrirait jamais l'anneau, et une couleur gravée par un bin invité y
resterait. Le companion écrit donc l'état connu une fois au démarrage, quoi que
dise l'option. C'est lui qui possède le matériel, et on ne peut pas compter sur
un invité pour nettoyer derrière lui — il peut être un binaire tiers, ou un
binaire qu'un chien de garde a abattu avant que son propre nettoyage ait pu
tourner.

## GC0308 — la caméra

Capteur VGA sur le bus DVP, contrôle en SCCB. Deux obstacles matériels ont dû
être levés avant qu'une seule image n'arrive.

**1. Le bus SCCB est le bus de l'IMU.** M5Unified pilote les broches 11/12 avec
sa propre implémentation de registres (`m5gfx::i2c`), *pas* le pilote I2C
ESP-IDF qu'esp32-camera attend pour le SCCB — d'où un timeout (erreur 263).
`firmware/companion/sccb_m5.cpp` **redéfinit** les fonctions `SCCB_*`
d'esp32-camera pour router le bus de contrôle par `M5.In_I2C`, lié avec
`-Wl,--allow-multiple-definition`. Caméra et IMU se sérialisent alors sur le
même i2c et le VOR survit au streaming.

**2. Il n'y a pas de JPEG matériel.** Le GC0308 ne sort que du RGB565/YUV ;
demander `PIXFORMAT_JPEG` rend `ESP_ERR_NOT_SUPPORTED` (262). La capture est
donc en RGB565 et l'encodage JPEG se fait en logiciel (`frame2jpg`) au moment
de servir.

Broches : `XCLK` est nominalement G2 mais **inutilisée** — le capteur tourne
sur un quartz externe de 20 MHz. Les données sont `D0–D7` sur
39/40/41/42/15/16/48/47, avec `VSYNC` 46, `HREF` 38, `PCLK` 45. Aucune broche
`PWDN` ni `RESET` n'est câblée, ce qui explique que l'init commence par un reset
logiciel (`0xfe, 0x80`) : l'état des registres du capteur **persiste** à travers
notre redémarrage.

Le renderer est mis en pause autour du remplissage DMA d'une image. Sans cela,
le SPI-DMA de l'écran et le GDMA de la caméra s'arbitrent mal et la FIFO caméra
déborde en milieu d'image.

**Discipline anti-brick** : éteinte par défaut (clé de tuning `camera`), init
différée jusqu'à la demande (jamais au boot), échec **verrouillé** pour qu'il
n'y ait pas de boucle de reprise, et dé-init après inactivité pour libérer la
PSRAM et laisser le bus au repos.

## Microphones ES7210 et haut-parleur AW88298

Capture stéréo par `M5.Mic.record(..., stereo = true)` ; restitution par
`M5.Speaker`. Ils partagent I2S1 — les règles d'arbitrage sont dans
[`BUSES.fr.md`](BUSES.fr.md) §3 et elles ne sont pas optionnelles.

L'option `sound_track` tourne la tête vers le bruit, et se met délibérément en
sourdine pendant que les servos bougent : sinon le robot poursuit son propre
bruit d'engrenages. Désactivée par défaut.

## BM8563 — la RTC

`M5.Rtc.isEnabled()` / `getTime()`, même bus interne. Elle est là pour un seul
métier : connaître l'heure réelle afin que la baisse de volume nocturne et le
thème nuit suivent le **vrai coucher et le vrai lever du soleil** (calculés
depuis latitude/longitude par `firmware/common/SunClock.h`) plutôt qu'une
fenêtre horaire fixe.

## Non implémentés

| Pièce | Adresse / broches | État |
|---|---|---|
| ST25R3916 (NFC) | `0x50` | broches et adresse connues ; nécessite une bibliothèque dédiée et une session matérielle |
| IRM56384 (IR) | RX G10, TX G5 | idem |

Les deux sont des opportunités plutôt que des manques : rien dans le firmware
n'en dépend, et leurs spécifications sont rédigées dans `ROADMAP.md` pour que
les pièges soient payés d'avance.
