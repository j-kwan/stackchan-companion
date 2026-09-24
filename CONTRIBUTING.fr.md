> [English](CONTRIBUTING.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# Contribuer

Ce projet a des partis pris qui vont vous surprendre, et la plupart sont tenus
par un portail plutôt que par une relecture. C'est délibéré — chacune de ces
règles a été payée une fois — mais cela signifie qu'une première contribution
peut échouer pour des raisons qui semblent arbitraires tant qu'on n'en connaît
pas la cause.

Cette page est ce que vous apprendriez sinon en échouant. Les règles
elles-mêmes sont dans [`docs/ROADMAP.fr.md`](docs/ROADMAP.fr.md) §A2.

## Obtenir une compilation

Il faut [PlatformIO](https://platformio.org/) et Python 3. Aucun matériel n'est
nécessaire pour l'essentiel du travail : les modules purs sont testés
nativement.

```bash
git clone <ce depot> && cd stackchan-companion

# les suites natives — sans carte, ~20 s
pio test -e native

# le portail complet : docs, portails, tests, sept builds, A2.22 dans l'ELF
./scripts/check-all.sh          # Windows : .\scripts\check-all.ps1
```

`check-all` est ce que ferait tourner une CI, et c'est ce que vous devriez
avoir au vert avant d'ouvrir quoi que ce soit. `-Fast` / `--fast` saute la
compilation des firmwares et la passe A2.22 quand vous ne touchez que la
documentation ou du code natif.

Installez-le une fois en hook de pre-push et n'y pensez plus :

```bash
./scripts/check-all.sh --hook   # Windows : .\scripts\check-all.ps1 -Hook
```

## Les huit portails, et ce que chacun vous dira

| Portail | Il échoue quand |
|---|---|
| `check-doc-parity` | un `.md` a changé sans son jumeau `.fr.md` — **structure** (titres, lignes de tableau, marqueurs d'état) *et* **volume** (une traduction ne peut pas être en retard de plus de ~10 %) |
| `check-contrast` | une couleur de thème, ou la palette d'interface partagée, échoue au seuil de lisibilité sur la dalle |
| `check-vendored` | l'une des cinq copies vendorées dans `SceGuest.h` a dérivé de son original dans `firmware/common/` |
| `check-mirrors` | un fait écrit deux fois a cessé de s'accorder — bornes servo, planchers de tests, identifiants USB, palette partagée, `CLAUDE.md` |
| `check-console` | une clé `Tuning` n'a pas de contrôle dans la console embarquée |
| `check-guest-config` | un réglage d'invité est sorti de l'une des quatre listes où il doit figurer |
| `check-doc-coverage` | `API.md` ou `EMOTIONS.md` ne correspond plus à ce que le code définit |
| `check-a222` | un compte d'appels de dessin a changé **dans le binaire lié** |

Aucun n'est consultatif. Si l'un échoue, ce qu'il faut corriger est le code ou
la doc — pas le portail. Assouplir un portail est un changement qui demande son
propre argument dans le message de commit.

## Les règles qui piègent

**La documentation est bilingue, et l'anglais fait référence.** Chaque
`docs/*.md` a un jumeau `.fr.md`, mis à jour dans le même commit. Les
commentaires de code sont **toujours en anglais**, quelle que soit la langue du
document.

**Les docs décrivent le système tel qu'il est.** Pas de dates, pas de « avant
on… », pas de récit. L'histoire appartient au `CHANGELOG.md`. Les deux
exceptions sont `validation/VALIDATION.md` et `validation/PLAYBOOK-HW.md`, qui
portent des verdicts datés exprès.

**Un point d'appel de dessin par forme.** GCC 8.4 Xtensa peut supprimer le
*second* de deux appels de dessin similaires dans un même corps. C'est
invisible dans la source, donc `check-a222` compte les appels dans l'ELF. Si
vous ajoutez une forme dessinée depuis une boucle, attendez-vous à y ajouter
une entrée — et lisez le compte dans le binaire plutôt que de compter les
`canvas.` de votre source.

**Rien de lourd dans un callback AsyncTCP.** Un handler peut poster une
commande ou écrire un champ de tuning. Écritures SD, flashs et redémarrages
sont différés vers `loop()`.

**Un producteur, un consommateur, un propriétaire de l'écran.** Le Brain écrit
`FaceState` ; le renderer le lit ; seul le renderer touche `M5.Display`.

**Aucun lissage hors du Brain.** Rampes et filtres vivent en un seul endroit.
Un second en aval filtre des valeurs déjà lissées, et le symptôme est subtil —
un clignement réduit à quelques pixels.

## Ajouter des choses

**Une clé de tuning** — l'ajouter à `Tuning::table()`, lui donner un contrôle
dans la console, et `check-console` confirmera que vous avez fait les deux.

**Un réglage d'invité** — il doit figurer dans quatre listes : `addSetting`,
`settingGet`, `settingSet` et `saveConfig`. La quatrième est la dangereuse :
`saveConfig` **tronque** le fichier, donc une clé lisible depuis le yaml mais
absente de la sauvegarde est *détruite* à la première sauvegarde depuis
`/config`. `check-guest-config` existe parce que c'est arrivé.

**Un en-tête partagé** dans `firmware/common/` — si `SceGuest.h` en a besoin,
il doit y être vendoré sous `__has_include`, avec les marqueurs, et enregistré
dans `check-vendored`.

**Une route** — l'enregistrer par `WebApi::route()`, jamais `_server.on()`, et
placer le chemin spécifique **avant** son préfixe. Puis l'ajouter à
[`docs/reference/API.fr.md`](docs/reference/API.fr.md), ce que
`check-doc-coverage` exigera.

## Tests

`test/test_*/` tient les suites natives — 37 suites, pour 427 cas. Tout ce qui est
dans `engine/`, `behavior/` et `firmware/common/` est pur (Clock et Rng
injectés) précisément pour être testable sans carte. Si vous ajoutez de la
logique là et qu'elle n'est pas testable nativement, c'est en général le signe
qu'elle est dans la mauvaise couche.

Les planchers de `check-all` sont assertés, pas décoratifs : une suite qui
cesserait d'être collectée passerait sinon en silence. Relevez-les en ajoutant
des cas — `check-mirrors` tient tous les endroits où ces nombres sont cités.

## Changements matériels

Lisez d'abord [`docs/hardware/`](docs/hardware/README.fr.md), et en particulier
[`LIMITS.fr.md`](docs/hardware/LIMITS.fr.md) : il liste ce qui a déjà été tenté
sur cette carte et a **échoué**, avec la mesure qui a clos chaque dossier.
Plusieurs d'entre eux avaient l'air de marcher au début.

Si vous validez quelque chose sur le robot, consignez-le dans
[`docs/validation/VALIDATION.fr.md`](docs/validation/VALIDATION.fr.md) avec ce
que vous avez mesuré. « Ça avait l'air bon » n'est pas un verdict que ce projet
accepte — la plupart des bugs coûteux d'ici avaient l'air bons.

## Commits

Expliquez **pourquoi**, pas quoi — le diff dit déjà quoi. Nommez la panne que
le changement évite quand il y en a une. Citez la règle (`A2.x`) que vous
respectez ou amendez.

Les messages de commit évitent les accents : `git commit -m` sous PowerShell
les mange.

⚠ Jamais de `git add -A` aveugle : `sdcard/stackchan-companion/config.yaml` est
suivi et peut contenir de vrais identifiants WiFi sur un robot en service.

## Licence

AGPL-3.0. Certains fichiers portés (esp32-eyes, ESP32_Faces) conservent leur
en-tête AGPL-3.0 d'origine ; ils compilent dans le même binaire, donc
l'AGPL-3.0 régit la distribution du firmware complet. En contribuant, vous
acceptez que votre travail soit distribué sous cette licence.
