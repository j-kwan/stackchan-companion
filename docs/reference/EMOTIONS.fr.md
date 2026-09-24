> [English](EMOTIONS.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# Les 30 expressions

Le catalogue. Ces noms sont ce qu'accepte `POST /api/emotion?name=…`, ce qu'une
règle de `rules.txt` nomme comme action, et ce qu'une keyframe de danse met
dans sa colonne `emotion` — cette page est donc la liste dont vous avez besoin
avant d'écrire l'une de ces trois choses. Comment une expression est
*construite* et *animée* se trouve dans [`EYES.fr.md`](EYES.fr.md) ; la source
de vérité pour les noms est `src/engine/Emotions.h`.

Les noms sont comparés **sans tenir compte de la casse** par l'API.

## Les dix-huit d'origine

Portées d'esp32-eyes, et la base sur laquelle l'essentiel de la géométrie du
rig a été réglé.

| Nom | Se lit comme | Notes |
|---|---|---|
| `Normal` | neutre, éveillé | le visage de repos, et celui que la roulette tire le plus — sauf dans le noir, où il est retiré du tirage |
| `Angry` | colère | bords supérieurs inclinés |
| `Glee` | jubilation | yeux joyeux, plus vifs |
| `Happy` | contentement | fermeture ancrée en bas, pas de mode centré |
| `Sad` | tristesse | s'accompagne d'une tête baissée (`pitchBiasFor`) |
| `Worried` | inquiétude | légère asymétrie |
| `Focused` | concentration | cadence de clignement divisée par deux |
| `Annoyed` | agacement | |
| `Surprised` | surprise | le **plus grand** œil du jeu (112 px) — la référence de luminosité des LED ; les yeux restent grands ouverts 2 à 4 s avant que les clignements reprennent |
| `Skeptic` | doute, d'un seul côté | asymétrie statique à l'entrée |
| `Frustrated` | frustration | |
| `Unimpressed` | désapprobation plate | |
| `Sleepy` | lutte contre le sommeil | fermeture asynchrone œil par œil, et le tirage dominant dans le noir |
| `Suspicious` | méfiance | |
| `Nervous` | plissement nerveux | le seul preset avec un `OffsetX` non nul (`Nervous_Alt`, +20 px) |
| `Furious` | fureur | transition la plus rapide, clignement le plus sec |
| `Scared` | frayeur | **le visage du réflexe de secousse** — préempte tout |
| `Awe` | émerveillement | grands ouverts, pente basse douce |

## Les douze extensions

Ajoutées pour ce firmware, sur l'identité Wall-E/Cozmo plutôt que sur le jeu
d'origine.

| Nom | Se lit comme | Notes |
|---|---|---|
| `Excited` | excitation | rendu spécial : une **étoile**, dessinée seule sur du noir |
| `Questioning` | interrogation | asymétrique par construction |
| `Frozen` | figé | cadence de clignement fortement réduite |
| `Scary` | menaçant | cadence de clignement réduite |
| `Curious` | curiosité | asymétrique ; **le visage du réflexe de soulèvement** |
| `Doubt` | hésitation | |
| `Contempt` | mépris | asymétrie statique à l'entrée |
| `Disgust` | dégoût | |
| `Smug` | autosatisfaction | |
| `Dead` | hors service | rendu spécial : une **croix** ; la seule expression où le clignement est bloqué net, et où tout le reste — regard, VOR, respiration, squash — est figé aussi |
| `Blush` | embarras attendri | yeux de `Happy` plus un **calque** de joues roses |
| `Squint` | plissement concentré | base `Focused`, pente basse, hauteur réduite |

## Ce qui en choisit une

Quatre sources, et elles n'ont pas le même rang.

| Source | Comment elle l'emporte |
|---|---|
| **Réflexes** | secousse → `Scared`, soulèvement → `Curious`. Ils **préemptent tout**, y compris une danse en cours (règle A2.5) |
| **API / règles** | `POST /api/emotion`, ou une action de `rules.txt`. L'emporte sur la roulette tant qu'elle tient |
| **Danses** | la colonne `emotion` d'une keyframe ; une colonne vide laisse l'expression inchangée |
| **La roulette** | le tirage au repos, toutes les 6 à 12 s, et le plus bas rang des quatre |

Les poids de la roulette et son comportement nocturne sont dans
[`EYES.fr.md`](EYES.fr.md) ; la chaîne des réflexes est dessinée dans
[`../architecture/WORKFLOWS.fr.md`](../architecture/WORKFLOWS.fr.md) §4.

## Les utiliser

```bash
# tenir une expression 3 s, puis rendre la main à la roulette
curl -X POST "http://<ip>/api/emotion?name=Surprised&ms=3000"
```

```
# dans rules.txt — se déclenche quand le contexte Claude passe 90 %
# enable | field | op | value | sustainMs | cooldownMs | action
claude | ctx | ge | 90 | 3000 | 60000 | SetEmotion Worried 4000
```

Deux comportements à connaître avant d'utiliser l'un ou l'autre :

- **Un nom inconnu est refusé**, avec `404 unknown emotion` — jamais ramené en
  silence à `Normal`. Une faute de frappe est alors une requête qui échoue
  visiblement plutôt qu'un robot qui fait discrètement autre chose. Idem dans
  une règle : une ligne qui ne s'analyse pas est simplement absente de la table
  chargée, ce que `GET /api/rules` vous montrera.
- **Une émotion explicite interrompt une danse en cours.** Sans quoi la
  keyframe suivante de la danse réappliquerait sa propre expression et
  écraserait la vôtre une fraction de seconde plus tard. Sans `ms`, elle tient
  10 s.
