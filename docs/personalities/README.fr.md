> [English](README.md) · **Français**

# `personalities/` — une page par caractère

Le MÉCANISME — ce qu'une personnalité possède, ce à quoi elle ne touche jamais,
comment écrire ses règles et comment en ajouter une — vit dans
[`reference/PERSONALITIES.fr.md`](../reference/PERSONALITIES.fr.md). Ce
répertoire tient les CARACTÈRES eux-mêmes : ce que chacun est, à vivre avec.

| Caractère | Index | Page |
|---|---|---|
| Défaut | `0` | *(pas de page — c'est le robot que décrit tout le reste de la documentation)* |
| Haro | `1` | [`HARO.fr.md`](HARO.fr.md) |

La personnalité par défaut n'a pas de page, et ce n'est pas parce qu'elle
compterait moins. Tous les autres documents la décrivent déjà : c'est ELLE, le
robot dont `EMOTIONS.fr.md` liste les émotions, dont `PLUGINS.fr.md` explique les
règles, dont `EYES.fr.md` dessine le visage. Lui donner une page propre en
ferait un second récit, plus mince, de tous ceux-là — et les deux divergeraient.

Une page de caractère répond à ce que la référence ne peut pas dire : **comment
est celui-ci ?** Ce qu'il remarque, ce qu'il ignore, ce à quoi il est mauvais.
Une table de réglages n'est pas la description d'un compagnon.

## Ajouter un caractère

Trois choses, dont la première seule demande une recompilation :

1. une entrée dans `src/behavior/Personalities.h` — nom, fichier de règles,
   couleur, cadence de roulette, et les poids dans lesquels il se repose ;
2. un fichier de règles à côté de `rules.txt` sur la carte ;
3. une page ici, dans les deux langues, qui dise à quelqu'un comment il est.

Le nom et le chemin de règles doivent être uniques — `test_personalities`
vérifie les deux, car deux caractères partageant un fichier de règles les
remettraient en concurrence pour les mêmes 21 places que le fichier par
personnalité sert justement à séparer.
