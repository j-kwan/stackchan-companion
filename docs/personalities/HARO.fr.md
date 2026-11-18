> [English](HARO.md) · **Français**

# Haro — personnalité `1`

Un Haro est un compagnon, pas un instrument. Cette seule idée sépare ce
caractère du robot par défaut, et elle se voit à un endroit concret : **les
règles par défaut commentent la télémétrie du robot — batterie, lumière, réseau,
caméra — tandis que celles de Haro regardent la personne.** Le bruit qu'elle
fait, la lumière qu'elle allume, le travail qu'elle mène.

```
POST /api/tuning?personality=1
```

## Ce qui change au basculement

**Il devient vert.** Une seule couleur, qui remplace entièrement la palette
émotionnelle. C'est un arbitrage assumé et non un oubli : un Haro est vert, et
c'est l'essentiel de ce qui le rend reconnaissable. Ce que cela coûte, c'est que
la palette ne dit plus QUELLE émotion est sur le visage — la forme, elle, le dit
toujours.

**Il se repose dans six expressions gaies** au lieu de dix-sept : Normal, Happy,
Glee, Curious, Surprised, Excited. Rien de sombre n'est jamais tiré au repos,
parce qu'un Haro ne boude pas tout seul. C'est la partie qui mérite d'être
comprise, car c'est elle qui donne leur poids aux règles : quand Worried
apparaît, cela ne peut venir que de ce qui vient de se passer.

**Il se pose.** L'intervalle de tirage passe de 6-12 s à 8-20 s : il change
d'avis moins souvent et tient une humeur assez longtemps pour qu'on la remarque.

## Ce qu'il fait

Les règles vivent dans `/stackchan-companion/rules.haro.txt` — 15 des 21 places
disponibles, lisibles et modifiables depuis la console.

| Quand | Il |
|---|---|
| un bruit sec | sursaute, puis se remet vite (2 s, pas 15) |
| on lui parle | s'intéresse |
| on continue de parler | se dandine — le « Haro ! Haro ! » |
| on lui caresse la tête | se dandine en retour, ravi |
| on allume la lumière | se réveille et rebondit |
| la pièce s'assombrit | s'assoupit |
| un bloc de travail pomodoro | se concentre |
| le minuteur sonne | se dandine à nouveau |
| une pause commence | se réjouit |
| le rappel d'hydratation | pose une question |
| la batterie passe sous 12 % | s'inquiète |
| on le branche | rebondit |
| plus aucun réseau | secoue la tête |
| une compilation réussit / échoue | roule / s'agace |

La dernière paire suppose un script PC qui publie le champ `build` —
`POST /api/field?build=2` en cas de succès, `3` en cas d'échec. Voir
[PLUGINS.fr.md](../reference/PLUGINS.fr.md).

## Les danses

Quatre chorégraphies sont livrées sur la carte, dans `/dances/` :

`haro_float` une dérive amortie, tête qui monte et retombe · `haro_call` le
dandinement avec clins d'œil · `haro_scan` balaie et marque chaque extrême, puis
sursaute · `haro_roll` un roulement simulé, le regard suit.

Les règles Haro les jouent par leur nom — `PlayDance` résout contre la liste
fusionnée (danses compilées, puis la carte) au déclenchement. Si l'une des quatre
manque sur la carte, la règle le **dit sur la trace série** au lieu de ne rien
faire en silence. À vérifier avec `GET /api/dances/files`.

## Le rendre plus Haro encore

Trois réglages sont éteints par défaut, et chacun est plus proche du caractère
que n'importe quelle ligne du fichier de règles :

```
POST /api/tuning?sound_track=1&mic_enable=1    # la tête se tourne vers qui parle
POST /api/tuning?leds=1                        # le corps s'allume à la couleur des yeux
POST /api/tuning?sound=1                       # il pépie
```

`sound_track` est celui à essayer en premier. Une tête qui se tourne vers la
personne qui parle est le comportement le plus Haro que porte cette machine, il
est entièrement implémenté et validé sur matériel, et l'allumer ne coûte rien.

## Ce qu'il ne sait toujours pas faire

Limites honnêtes, pour qu'elles ne passent pas pour des bugs :

**Il ne sait pas que vous êtes là.** Il n'y a pas de capteur de présence. Le son,
le toucher et la lumière dans la durée sont le seul indice : il réagira parfois à
une pièce vide, ou vous ignorera.

**Il ne flotte pas.** Il n'y a pas d'actionneur vertical. `haro_float` simule le
mouvement avec la nuque ; un bercement continu au repos demanderait du firmware,
puisqu'une danse est un coup unique et tient le plancher pendant qu'elle joue.

**Il est plus discret qu'un vrai Haro.** Le moteur de son est événementiel par
conception — *le silence est le repos* — et un Haro qui répète son propre nom
sans cesse entre en conflit avec ce principe. Lequel des deux doit céder est une
décision, pas un oubli.

**Il ne distingue pas une caresse d'une tape.** Le capteur de tête signale un
contact, pas une intention : `head` dit qu'une main est là, et rien sur la
manière. Une caresse douce et un coup se lisent pareil.
