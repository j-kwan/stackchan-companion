> [English](PERSONALITIES.md) · **Français**

# Personnalités — quel caractère le robot porte

Une personnalité n'est pas un thème. C'est un jeu de dispositions : **quelles
règles le robot suit, dans quelles expressions il se repose, et de quelle
couleur il est.** En changer modifie son comportement quand il ne se passe rien
de particulier — c'est-à-dire la plupart du temps.

```
POST /api/tuning?personality=1      # 0 = défaut, 1 = Haro
```

| | `0` — défaut | `1` — Haro |
|---|---|---|
| règles | `/stackchan-companion/rules.txt` | `/stackchan-companion/rules.haro.txt` |
| expressions de repos | les 17 poids complets | six, toutes gaies |
| intervalle de tirage | 6-12 s | 8-20 s |
| couleur | la palette émotionnelle | un seul vert (`#22FF66`) |

## La ligne de partage

> Une personnalité peut changer **ce que le robot ressent et combien de temps il
> le montre**. Elle ne peut jamais changer **ce que le matériel tolère**.

Elle possède son fichier de règles, sa roulette (poids et cadence), sa couleur
d'identité, et deux cadrans de RESSENTI — la vitesse à laquelle elle passe d'une
humeur à l'autre et la force avec laquelle sa posture y penche. Elle ne possède
**rien d'autre** : en changer ne réécrit jamais vos autres réglages. Luminosité,
options servo, son, seuils survivent au changement dans les deux sens, et la
question « quelle couche a écrit cette valeur » ne se pose donc jamais.

Butées servo, budget de frame, cadences de bus et vocabulaire tactile sont par
conséquent absents de la table par construction. `test_personalities` l'affirme :
une « personnalité » capable de garer un servo contre sa butée serait un
mécanisme de dommage déguisé en costume.

## La personnalité par défaut n'est pas « l'absence de costume »

C'est un caractère à part entière, et c'est pour cela qu'elle a un index. La clé
s'appelle `personality` plutôt qu'un booléen `haro_mode` pour une raison
concrète : un booléen serait à renommer le jour où un troisième caractère
apparaît, et sur ce projet renommer une clé *est* une migration — l'ancienne
valeur revient silencieusement par la réécriture de la configuration. Le nom est
payé maintenant, tant qu'il ne coûte rien.

La personnalité 0 décline aussi de déclarer le moindre poids. Elle ne recopie pas
la table historique, elle laisse celle d'`EmotionRoulette` tranquille — et c'est
ce qui rend « une carte sans données de personnalité se comporte exactement comme
avant » vrai par construction plutôt que par recopie soigneuse. Cette
équivalence est le critère d'acceptation de tout le lot, et elle est testée.

**L'index n'est pas stable d'un rechargement à l'autre — le NOM du caractère
actif, lui, l'est.** `PersonalityStore` reconstruit la table depuis l'ordre du
répertoire de la carte à chaque rechargement, donc créer ou supprimer un
caractère peut décaler l'index d'un autre, sans rapport. Le flux
créer/modifier/supprimer de la console porte le nom du caractère actif à
travers ce rechargement et en résout le nouvel index avant de le
réappliquer, si bien que changer de caractère via `/api/tuning?personality=`
entre deux écritures est le seul moyen d'atterrir sur le mauvais — un
créer/modifier/supprimer déclenché par la console elle-même garde toujours
le caractère réellement en cours.

## L'interrupteur des humeurs aléatoires

```
POST /api/tuning?roulette=0
```

Roulette allumée, le robot tire une émotion toutes les quelques secondes. C'est
ce qui le fait paraître vivant quand rien ne se passe — et c'est aussi ce qui
empêche toute expression de **signifier** quelque chose, puisque le tirage
suivant écrase ce qu'une règle vient de dire. Éteinte, seules les règles et les
réflexes parlent : un visage Worried ne peut alors venir que de ce qui s'est
produit.

**Ce n'est pas un gel.** Les émotions minutées reviennent au repos toutes
seules, et clignements, saccades, respiration et regard gyroscopique sont
pilotés ailleurs et continuent. Ce qui s'arrête, c'est le CHOIX aléatoire, rien
d'autre.

Une personnalité étroite obtient la même chose plus doucement. Haro se repose
dans six expressions gaies et ne tire jamais rien de sombre : un visage Worried
y reste informatif sans toucher à l'interrupteur.

## Deux cadrans de ressenti, pas cent valeurs par émotion

```
POST /api/tuning?transition_scale=1.6&pitch_bias_scale=0.4
```

`transitionFor()` et `pitchBiasFor()` portent environ cent valeurs réglées à la
main — une durée et une inclinaison de tête pour chacune des 30 émotions.
Rendre chacune éditable par caractère ouvrirait une surface énorme pour très
peu de ressenti : une personnalité reçoit donc deux cadrans sur toute la table,
plutôt que cent clés dedans :

| Cadran | Plage | 1,0 veut dire | 0 veut dire |
|---|---|---|---|
| `transition_scale` | 0,3 – 3,0 | non mis à l'échelle — les durées compilées, inchangées | *(inatteignable — 0,3 est le plancher : des transitions instantanées paraîtraient cassées, pas vives)* |
| `pitch_bias_scale` | 0,0 – 2,0 | non mis à l'échelle — l'inclinaison de tête compilée par émotion | stoïque — la tête ne penche jamais selon l'humeur, quel que soit le mouvement qui l'y a menée |

Les deux restent des **échelles**, jamais des valeurs de remplacement : elles
multiplient ce que la table compilée dit déjà, donc un caractère plus vif reste
reconnaissablement le même caractère, seulement rythmé autrement. La borne vit
une seule fois, dans `Personalities.h`, en fonction pure — `Brain` lui-même ne
peut pas être testé nativement (son constructeur ouvre une file FreeRTOS et
épingle une tâche), donc la borne qui garde une transition entre 40 et 3000 ms
doit être démontrable là où un binaire PC peut l'atteindre, et
`test_personality_feel` la balaie, NaN et infini compris, plutôt que de faire
confiance à quelques points d'échantillon.

## Écrire les règles d'une personnalité

Le format est celui de [PLUGINS.fr.md](PLUGINS.fr.md), avec deux pièges qu'il
vaut mieux redire, parce qu'un fichier de règles doit pouvoir se lire seul.

**Un champ absent vaut zéro.** `RuleEngine` lit `getF(field, 0.0f)` sans tester
l'existence : toute règle `lt`/`le` avec un seuil supérieur à 0 est donc **vraie
tant que le champ n'existe pas**. Sur un champ optionnel (`build`, `ctx`, tout ce
qu'un script PC publie), comparer à un code NON NUL avec `eq` :

```
| build | eq | 2 | 0 | 10000 | PlayDance | happy     # 2 = succès
```

**Le plafond est silencieux.** Le moteur tient 24 règles, dont 3 natives, et
refuse les suivantes sans un mot. Chaque personnalité dispose des 21 restantes
pour elle seule — un seul fichier est chargé à la fois, et c'est exactement
pourquoi les règles n'ont besoin d'aucun gate de personnalité : **le fichier est
le gate.**

Deux champs trompent. `night` n'est pas le crépuscule solaire (celui-là ne vit
que dans `/api/sensors`) : c'est le mode nuit du capteur de lumière, et tant
qu'il est actif la roulette met déjà Sleepy en dominante. Et `tmr_st` est un
**code couleur**, pas un état ordonné : `Idle=0 Run=1 Paused=2 Ring=3 Work=1
Break=4 Hydrate=4 Done=4`. Pour les phases, utiliser `tmr_ph`, qui est l'enum
brut (`Idle=0 Run=1 Paused=2 Ring=3 Work=4 Break=5 Done=6 Hydrate=7`).

## Les danses

Toutes les chorégraphies vivent ensemble dans `/dances/`, partagées par toutes
les personnalités ; c'est le fichier de règles qui décide lesquelles un caractère
emploie. Les quatre danses Haro sont livrées avec la carte :

| Fichier | Ce qu'elle fait |
|---|---|
| `haro_float.csv` | une dérive amortie, tête qui monte et retombe, sans temps mort entre les pas |
| `haro_call.csv` | le dandinement « Haro ! Haro ! », avec clins d'œil |
| `haro_scan.csv` | balaie à gauche et à droite, marque chaque extrême, puis sursaute |
| `haro_roll.csv` | un roulement simulé — lacet et tangage combinés, le regard suit |

Une règle les joue par leur nom comme n'importe quelle autre : `PlayDance`
résout contre la liste fusionnée — les danses compilées d'abord, puis la carte —
au moment où la règle SE DÉCLENCHE, et non quand le fichier est analysé. Cet
ordre est contraint et non choisi : au démarrage les règles sont lues avant
`danceStore.reload()`, donc la banque est vide pendant l'analyse, et
`DanceStore` se recharge à chaud, donc un index mémorisé désignerait une autre
danse après un téléversement. Voir [PLUGINS.fr.md](PLUGINS.fr.md).

`haro_float.csv` mérite d'être lu pour un piège : `holdMs` est la durée TOTALE
de la keyframe, pas un supplément. Écrire `servo 300 / hold 600` signifie
« bouger 300 ms, puis rester immobile 300 ms » — la moitié de la danse figée, ce
qui se lit comme une suite de hochements et non comme un flottement. Poser
`hold == servo` donne un mouvement continu.

## L'onglet Caractères

Tout ce qui suit se fait depuis la console — `http://<ip>/#perso`. Elle modifie
une COPIE et n'écrit qu'au moment d'**Enregistrer** : un curseur à moitié tiré
n'atteint jamais la carte.

**Essayer** est l'exception assumée : elle écrit, applique, et le dit. Il n'y a
pas d'aperçu qui garderait un caractère en RAM seulement — il disparaîtrait au
rechargement suivant et passerait pour un bug, et une couleur comme une cadence
ne se jugent pas sur un formulaire.

**Dupliquer** déverrouille le champ du nom et n'écrit rien tant qu'on n'a pas
enregistré : un doublon auquel on renonce ne laisse aucune trace. **Supprimer**
est grisé plutôt que simplement refusé quand un caractère ne peut pas partir —
le robot dit non dans les deux cas, et un bouton qui a l'air actif puis vous
gronde est une plus mauvaise façon d'apprendre une règle qu'un bouton jamais
offert.

**La console porte la peau du caractère actif, et suit la pièce où elle se
trouve.** `theme:` est apposé sur la racine de la page ; le réglage du
navigateur (`prefers-color-scheme`) choisit ensuite la face SOMBRE ou CLAIRE de
ce thème, sans interrupteur dans la console à chercher — le robot n'a pas
d'avis sur la pièce où vous êtes, et interroger le système une fois est plus
honnête qu'ajouter un bouton que personne ne trouve. Un thème PEUT redessiner
son propre fond, son encre et son verre, pas seulement ses accents, parce
qu'une page qui ne ferait que changer la couleur d'un lien au nom d'un
caractère n'aurait pas vraiment l'air d'une autre console. Ce qui ne bouge pas,
c'est la GARANTIE : chaque surface qu'un thème déclare, dans chaque mode qu'il
déclare, franchit toujours le même seuil WCAG que la page a toujours tenu —
`check-contrast.py` mesure chaque couple (thème, mode) contre le panneau sur
lequel il est réellement dessiné, et un mode non mesuré est la seule panne
qu'on ne peut pas réparer depuis la page.

## Des caractères qui vivent sur la carte

Poser un fichier dans `/stackchan-companion/personalities/` et le robot a un nouveau
caractère au démarrage suivant — sans recompiler :

```yaml
# /stackchan-companion/personalities/zaku.yaml
name: zaku
rules: /stackchan-companion/rules.txt   # peut réutiliser les règles d'un autre
color: 0xFF4422
theme: gundam                      # une peau COMPILÉE, choisie par son nom
roulette:
  enabled: 1
  min_ms: 5000
  max_ms: 11000
transition_scale: 0.7              # plus vif que le défaut compilé (1.0)
pitch_bias_scale: 1.3              # penche un peu plus dans l'humeur qu'à l'habitude
weights:                           # toute émotion omise n'est jamais tirée
  Normal: 1.0
  Focused: 0.5
  Suspicious: 0.3
  Annoyed: 0.2
```

**Le nom du fichier est l'identifiant, et il décide modifier ou créer.**
`haro.yaml` correspond au Haro compilé et se superpose à lui — c'est ainsi
qu'une modification d'un caractère livré par le firmware survit à un
redémarrage. Un nom que personne ne porte prend le premier slot libre et devient
un nouveau caractère.

**Il dit des différences, pas une déclaration.** Chaque clé est facultative et
une clé absente garde la valeur du dessous — celle du caractère compilé quand on
en modifie un, les défauts quand on en crée un. Un fichier à moitié écrit
dégrade donc vers « presque le défaut » et non vers un caractère troué. Seule
exception, `weights:` : déclarer la section REMPLACE la table, car une fusion ne
pourrait jamais exprimer « ce caractère ne fait pas Sad » — on pourrait ajouter
une émotion, jamais en retirer une.

**Une clé qu'il ne comprend pas est refusée et nommée** sur la ligne série,
jamais ignorée. Idem pour une émotion ou un thème inconnus ; le reste du fichier
se charge quand même, parce qu'une mauvaise ligne ne fait pas un mauvais
fichier.

**Les thèmes sont compilés et choisis par leur nom.** Une palette écrite sur la
carte ne pourrait pas être vérifiée en contraste à la compilation, et une
console illisible est la seule chose qu'on ne peut pas réparer depuis la
console. Aujourd'hui : `default` et `gundam`.

Huit caractères tiennent à la fois (`MAX_PERSONALITIES`), les deux premiers
étant les compilés. Le chargement est idempotent — la table revient aux
caractères du firmware avant de relire le répertoire — donc un fichier supprimé
retire vraiment son caractère, et une modification n'est jamais appliquée deux
fois.

**Un 9e caractère est refusé à voix haute, pas plafonné en silence.** Table
pleine, un fichier qui voudrait CRÉER un nouveau slot est ignoré et journalisé
(`[perso] '<id>' ignoree : table pleine`, compté dans le total de problèmes du
rechargement) ; un fichier qui MODIFIE un caractère déjà chargé s'applique
normalement, quelle que soit sa position dans l'ordre du répertoire — la table
pleine ne bloque que les créations, jamais la lecture du reste du répertoire.

## En ajouter une dans le code

Deux choses la font exister : une entrée dans `src/behavior/Personalities.h`
et un fichier de règles à côté de `rules.txt`. Aucun enum à tenir en phase,
aucun switch à étendre. Le nom et le chemin de règles doivent être uniques —
`test_personalities` vérifie les deux, car deux caractères partageant un fichier
de règles réintroduiraient la dispute des 21 places que le fichier par
personnalité sert justement à supprimer.

Une troisième la rend *documentée* : une page sous
[`docs/personalities/`](../personalities/), dans les deux langues, disant à
quoi ressemble la vie avec ce caractère — non vérifiée par un portail, donc à
la charge de l'auteur, pas de `check-all`. Voir
[`docs/personalities/README.md`](../personalities/README.fr.md#ajouter-un-caractère).
