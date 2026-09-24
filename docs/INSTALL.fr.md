> [English](INSTALL.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# Installer une release

Comment installer une release publiée sur un StackChan K151 (M5Stack CoreS3),
préparer sa carte SD, le connecter à votre WiFi et le tenir à jour. Cette page
est la même pour toutes les releases : les noms de fichiers ci-dessous
utilisent `<version>` pour l'étiquette de la release (par exemple `v1.0.0`).
Aucun outil de compilation n'est nécessaire. Pour compiler depuis les sources,
voir le [README](../README.fr.md#démarrage-rapide) et
[`CONTRIBUTING.fr.md`](../CONTRIBUTING.fr.md).

## Ce qu'il faut

- Un **StackChan K151** (M5Stack CoreS3 dans le corps K151).
- Un **câble USB-C qui transporte les données**. Un câble de charge seule est
  la cause la plus fréquente d'un port série introuvable.
- Une **carte microSD** formatée en FAT32 (32 Go ou moins, par prudence).
  Facultative pour le visage lui-même, mais nécessaire pour enregistrer le
  WiFi, lancer les applis invitées et en revenir.
- Pour flasher : **Chrome ou Edge** (flasheur dans le navigateur, rien à
  installer), ou Python avec `pip install esptool`.

## Les fichiers de la release

| Fichier | Ce que c'est | Où il va |
|---|---|---|
| `companion-<version>-factory.bin` | le firmware principal, **image complète** (bootloader, table de partitions, application) | flashé en USB à l'adresse `0x0` |
| `companion.bin` | le même firmware, application seule | racine de la carte SD, et fichier des mises à jour OTA |
| `flight-radar.bin`, `space.bin`, `ha-remote.bin`, `led-fluid.bin` | les applis invitées ([ce qu'elles font](guests/README.fr.md)) | carte SD, `/bins/` |
| `sdcard-<version>.zip` | une carte SD prête : le modèle de [`sdcard/`](../sdcard/README.fr.md) **plus** `companion.bin` et les quatre applis invitées | à décompresser à la racine de la carte |
| `flight-radar-fire-<version>-factory.bin`, `space-fire-<version>-factory.bin`, `led-fluid-fire-<version>-factory.bin` | applications autonomes pour un **M5Stack Fire** (pas le StackChan) | flashées en USB à `0x0`, voir [M5Stack Fire](#applications-autonomes-m5stack-fire) |
| `SHA256SUMS.txt` | les empreintes de tous les fichiers ci-dessus | [vérifier le téléchargement](#vérifier-le-téléchargement) |

⚠ Ne jamais flasher `companion.bin` (ni un `.bin` invité) à l'adresse `0x0` :
il n'a pas de bootloader et la carte ne démarrera pas. Le `-factory.bin` est
celui de l'USB.

## 1. Flasher le companion en USB

Branchez le robot à l'ordinateur.

**Option A : dans le navigateur (Chrome ou Edge)**

1. Ouvrez <https://espressif.github.io/esptool-js/>.
2. Vitesse `921600`, puis **Connect** et choisissez le port du robot (sous
   Windows, un *Périphérique série USB* / *USB JTAG/serial debug unit*).
3. Adresse `0x0`, fichier `companion-<version>-factory.bin`.
4. **Program**, attendez la fin, puis appuyez sur le bouton reset du robot.

**Option B : en ligne de commande**

```bash
python -m esptool --chip esp32s3 --port COM6 --baud 921600 write_flash 0x0 companion-<version>-factory.bin
```

Remplacez `COM6` par votre port (`/dev/ttyACM0` sous Linux,
`/dev/cu.usbmodem…` sous macOS).

**Aucun port n'apparaît ?** Essayez d'abord un autre câble. Sinon, passez la
carte en mode téléchargement : maintenez le bouton reset environ 3 secondes,
jusqu'à ce que la petite LED verte à côté s'allume, puis reconnectez.

L'image factory remet aussi à zéro l'indication du slot à démarrer : la carte
démarre donc le firmware que vous venez d'écrire, quoi qu'il tournait avant.
Les réglages gardés dans la mémoire de la carte (par exemple un réseau saisi
dans une appli invitée) sont conservés.

## 2. Préparer la carte SD

Décompressez `sdcard-<version>.zip` à la **racine** de la carte FAT32, de
sorte que `companion.bin` soit au premier niveau, à côté de `bins/`, `dances/`
et `stackchan-companion/`. Insérez la carte robot éteint.

Deux fichiers comptent plus que les autres :

- **`/companion.bin`** est le chemin du retour depuis une appli invitée : en
  quitter une réinstalle le companion depuis ce fichier. Il doit être de la
  **même version** que le firmware flashé. Une copie plus ancienne ramènerait
  discrètement l'ancienne version à la première sortie d'appli invitée.
- **`/stackchan-companion/config.yaml`** contient les réglages, WiFi compris.
  Le schéma complet est dans [`reference/CONFIG.fr.md`](reference/CONFIG.fr.md).

## 3. Premier démarrage et WiFi

Sans réseau configuré, le robot ouvre son propre réseau WiFi :

| | |
|---|---|
| Réseau | `StackChan-AP` |
| Mot de passe | `goodlife` |
| Console | s'ouvre toute seule (portail captif), sinon `http://192.168.4.1/` |

Pour rejoindre votre réseau, au choix :

- **Depuis la console** : onglet **Système**, bloc **Réseau WiFi**, saisissez
  le SSID et le mot de passe, **Enregistrer**, puis redémarrez le robot (bouton
  **⟳ Redémarrer** du même onglet).
- **Depuis la carte**, avant de l'insérer : remplissez `client_ssid` et
  `client_password` sous `wifi:` dans `/stackchan-companion/config.yaml`.

Dans les deux cas les identifiants sont écrits sur la carte SD, qui est donc
nécessaire. Une fois connecté, la console est à `http://stackchan.local/` (ou
à l'adresse IP donnée par votre box). Si le réseau ne répond pas en 10
secondes, le robot revient à son réseau `StackChan-AP` : il reste toujours
joignable.

⚠ La console n'a **pas de mot de passe par défaut**, et celui du réseau de
secours est public. Avant de laisser le robot sur un réseau partagé, lisez
[`reference/SECURITY.fr.md`](reference/SECURITY.fr.md) : ce qui est exposé, et
comment poser un mot de passe sur la console et changer celui du réseau de
secours.

## 4. Configurer

Tout se fait dans la console, et chaque changement est enregistré sur la
carte :

| Onglet | Ce qu'on y règle |
|---|---|
| **Pilotage** | expressions, danses, la tête à la main, les règles réactives |
| **Options** | comportement, yeux, LEDs, son, bandeau |
| **Caractères** | le caractère du robot ([`reference/PERSONALITIES.fr.md`](reference/PERSONALITIES.fr.md)) |
| **Fichiers** | la carte SD : importer, télécharger et supprimer des fichiers, chorégraphies, réglages des applis invitées |
| **Système** | WiFi, mot de passe de la console, mise à jour du firmware, build en cours, trace de debug, redémarrage |

La référence de chaque réglage est [`reference/CONFIG.fr.md`](reference/CONFIG.fr.md) ;
l'API REST derrière la console est dans [`reference/API.fr.md`](reference/API.fr.md)
(et le robot sert sa propre page Swagger). Les danses créées avec
l'[éditeur de chorégraphies](../tools/README.fr.md) s'importent depuis
l'onglet **Fichiers**.

## 5. Applis invitées

Les quatre applis de `/bins/` remplacent le companion pendant qu'elles
tournent et le rendent quand on les quitte.

- **En lancer une** : glissez vers le bas depuis le haut de l'écran du robot
  pour ouvrir le lanceur, ou utilisez le bouton 🚀 à côté du fichier dans
  l'onglet **Fichiers** de la console.
- **En sortir** : glissez de nouveau vers le bas et confirmez, ou utilisez le
  bouton de la page web de l'appli. Le companion est réinstallé depuis
  `/companion.bin` en une dizaine de secondes. Pendant ce temps le robot
  répond au ping mais pas aux pages web : c'est normal, attendez.
- **En configurer une** : chaque appli a un fichier de réglages dans
  `/stackchan-companion/` (`flightradar.yaml`, `space.yaml`, `ha-remote.yaml`,
  `ledfluid.yaml`) et sa propre page `http://<robot>/config` pendant qu'elle
  tourne. Certaines demandent une clé ou un jeton : `ha-remote` demande un
  jeton Home Assistant, voir sa page.

| Appli | Page |
|---|---|
| `flight-radar` | [`guests/FLIGHT-RADAR.fr.md`](guests/FLIGHT-RADAR.fr.md) |
| `space` | [`guests/SPACE.fr.md`](guests/SPACE.fr.md) |
| `ha-remote` | [`guests/HA-REMOTE.fr.md`](guests/HA-REMOTE.fr.md) |
| `led-fluid` | [`guests/LED-FLUID.fr.md`](guests/LED-FLUID.fr.md) |

## 6. Passer à une nouvelle release

Pas besoin de câble. Téléchargez la nouvelle release, puis :

1. Console, onglet **Système**, bloc **Firmware · mise à jour OTA** :
   choisissez le nouveau `companion.bin`, **⚡ Flasher**. Le robot redémarre
   dessus.
2. Console, onglet **Fichiers**, **⬆ Importer** : type **companion.bin
   (restauration)**, le même `companion.bin`. **Ne sautez pas cette étape** :
   sinon la prochaine sortie d'appli invitée remet la version précédente.
3. Même onglet, type **binaire (.bin)** : importez chaque nouvelle appli
   invitée par-dessus l'ancienne.

Votre `config.yaml`, vos règles, personnages et danses restent sur la carte.
La méthode USB de l'étape 1 marche aussi pour une mise à jour, à condition de
rafraîchir ensuite `/companion.bin` sur la carte.

L'onglet **Système** indique quel slot OTA a démarré, l'empreinte du build en
cours et la raison du dernier démarrage (`panic`, `task_wdt` ou `brownout`
signalent un plantage).

## Applications autonomes M5Stack Fire

`flight-radar`, `space` et `led-fluid` existent aussi en applications
autonomes pour un **M5Stack Fire** (trois boutons, pas d'écran tactile, pas de
StackChan). Elles se flashent de la même façon, avec l'autre nom de puce :

```bash
python -m esptool --chip esp32 --port COM7 --baud 921600 write_flash 0x0 space-fire-<version>-factory.bin
```

⚠ Avec les deux cartes branchées, vérifiez le port deux fois : flasher une
image Fire sur le StackChan efface son companion.

Le Fire n'a pas de companion auquel emprunter un réseau. Au premier
démarrage il ouvre le réseau `SCE-Guest` (mot de passe `goodlife`) ;
rejoignez-le et la page de réglages s'ouvre toute seule
(`http://192.168.4.1/config`). Saisissez votre WiFi dans son bloc **Réseau**.
Une carte SD avec les mêmes fichiers `/stackchan-companion/*.yaml` est lue
aussi, mais n'est pas obligatoire.

## Vérifier le téléchargement

`SHA256SUMS.txt` donne une empreinte par fichier.

```powershell
Get-FileHash .\companion-<version>-factory.bin -Algorithm SHA256   # Windows
```

```bash
sha256sum -c SHA256SUMS.txt --ignore-missing                       # Linux / macOS
```

## Dépannage

| Symptôme | Cause, et que faire |
|---|---|
| pas de port série | câble de charge seule, ou la carte a besoin du mode téléchargement (bouton reset maintenu ~3 s) |
| le flasheur du navigateur s'arrête en route | baissez la vitesse à `115200` et recommencez |
| après une mise à jour, l'ancienne version revient | `/companion.bin` sur la carte est plus ancien : importez le nouveau (étape 6.2) |
| le robot répond au ping mais la console ne charge pas, juste après la sortie d'une appli invitée | le companion est en cours de réinstallation depuis la carte, attendez jusqu'à une minute |
| « /companion.bin absent ou invalide » dans une appli invitée | la carte n'a pas de `/companion.bin` valide : copiez celui de la release à la racine |
| réglages WiFi oubliés après un redémarrage | pas de carte SD, ou carte pas en FAT32 |
| rien ne va et vous voulez repartir de zéro | `python -m esptool --chip esp32s3 --port COM6 erase_flash`, puis l'étape 1. Cela efface aussi les réglages gardés dans la mémoire de la carte |

Toujours bloqué : la console série (115200 bauds) raconte ce que fait le robot
à chaque étape une fois la trace de debug activée (onglet **Système**). Les
notes matérielles de [`hardware/`](hardware/README.fr.md) expliquent les
limites derrière la plupart de ces symptômes.
