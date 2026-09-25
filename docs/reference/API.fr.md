> [English](API.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# L'API REST

Toutes les routes que sert le companion, à quoi elles servent, et les quatre
conventions qui valent pour toutes. La description **exhaustive et toujours à
jour** est l'OpenAPI que le robot génère depuis son propre code —
`http://<ip>/swagger` pour la lire, `http://<ip>/api/openapi.json` pour la
donner à un outil. Cette page existe parce que celle-là exige un robot allumé,
et parce qu'une liste de routes ne dit pas les règles auxquelles elles
obéissent toutes.

Le tableau ci-dessous est tenu face à la table de routage du firmware par
`scripts/gates/check-doc-coverage.py` : une route ajoutée sans ligne ici fait
échouer le portail, et une ligne ici nommant une route inexistante le fait
échouer aussi.

## Quatre conventions

**Tout passe en paramètre de requête.** Pas de corps JSON, sauf pour les envois
de fichiers, en `multipart/form-data`. Un `POST` avec ses paramètres dans la
query string est la norme et non un raccourci : cela garde chaque appel
atteignable depuis `curl`, depuis une barre d'adresse, et depuis le
`rest_command` de Home Assistant sans template.

**Les erreurs sont bilingues et structurées.** `{"error":"…"}` avec un vrai
code : 400 pour un paramètre manquant ou invalide, 404 pour un nom qui
n'existe pas, 409 quand le robot ne peut pas dans son état courant (pas de
carte, mode AP), 503 quand un travail occupe déjà le créneau.

**Tout ce qui est lourd est différé.** Un callback AsyncTCP n'a le droit que de
poster une commande ou d'écrire un champ de tuning (règle A2.6) : les écritures
SD, les flashs, les redémarrages et les resynchronisations NTP répondent
*immédiatement* et se font au passage suivant de `loop()`. Un `202` signifie
donc « accepté », jamais « fait » — et pour celles qui comptent, un point
d'entrée séparé permet de relire le résultat (`GET /api/rules` après un
rechargement, `GET /api/firmware` après une mise à jour).

**L'ordre des routes est une vraie contrainte.** ESPAsyncWebServer avale
`/api/x/y` dans le handler de `/api/x` si `/api/x` est enregistrée en premier.
Toute route passe par `WebApi::route()`, et `checkRouteOrder()` dénonce un
mauvais ordre sur la console série au démarrage. Trois points d'entrée étaient
cassés ainsi avant que ce contrôle existe.

## Authentification

Optionnelle et désactivée par défaut : sans mot de passe, l'API est ouverte sur
le LAN. Lire [`SECURITY.fr.md`](SECURITY.fr.md) avant de décider que c'est
acceptable sur votre réseau — c'est un robot avec une caméra et un microphone.

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/security` | POST | Basic Auth à chaud, console et API. Un mot de passe vide la désactive. Persisté sur la carte |

## État et télémétrie

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/status` | GET | l'état général : émotion, regard, batterie, WiFi, SD, uptime |
| `/api/sensors` | GET | les extras du K151 : batterie, tension de bus et shunt INA226, lumière LTR-553, cap BMM150, état de calibration de la pose de repos de l'IMU (`imu_cal` : 0 en cours / 1 fait / 2 fait sur l'échéance), horloge NTP, indicateur de nuit. Valeurs en cache — aucune I2C dans le callback |
| `/api/firmware` | GET | **quel build tourne** : `slot`, `sha`, `console`, `reset`. À lire une fois, pas à interroger en boucle |
| `/api/clock` | GET | l'horloge murale : epoch, plausibilité, et si NTP a réellement répondu |
| `/api/clock/sync` | POST | forcer une resynchronisation NTP. 409 en mode AP |
| `/api/servo/pos` | GET | la pose **mesurée** de la tête, par opposition à celle commandée. Valide seulement quand `servos=0` — le bus servo est en écriture seule en service |

## Expression et mouvement

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/emotion` | POST | tenir l'une des 30 expressions ([`EMOTIONS.fr.md`](EMOTIONS.fr.md)). Interrompt une danse en cours |
| `/api/animation` | POST | `blink`, `winkLeft`, `winkRight` |
| `/api/dances` | GET | les danses disponibles, intégrées et venant de la carte |
| `/api/dance` | POST | en jouer une, ou arrêter |
| `/api/servo` | POST | piloter la tête : `yaw`/`pitch` absolus ou `dyaw`/`dpitch` relatifs, en degrés |

## La bande de statut

Le contrat `sources → champs → widgets` ; voir [`STATUSBAR.fr.md`](STATUSBAR.fr.md).

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/field` | POST | pousser un ou plusieurs champs sur le tableau noir. C'est par là que toute source extérieure nourrit la bande |
| `/api/statusbar` | POST | le mode de la bande |
| `/api/say` | POST | une notification défilante, qui interrompt ce que la bande affichait |
| `/api/timer` | POST | les modes minuteur et pomodoro |
| `/api/rules` | GET | la table des règles **telle que chargée**, ce qui n'est pas le contenu de `rules.txt` : une ligne qui ne s'analyse pas est simplement absente |
| `/api/rules/reload` | POST | relire `rules.txt` depuis la carte |

## Réglages

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/tuning` | GET | toutes les clés de tuning et leur valeur |
| `/api/tuning` | POST | écrire des clés à chaud. Persisté sur la carte **seulement quand une valeur change réellement** |
| `/api/personalities` | GET | la table des caractères TELLE QUE LE ROBOT LA TIENT — les entrées compilées et celles lues dans `/stackchan-companion/personalities/*.yaml`, avec pour chacune sa couleur, son thème de console, sa cadence de roulette et ses poids de repos. C'est aussi le seul moyen de voir de l'extérieur si un fichier de la carte a été pris en compte : le chargeur ne le dit sinon que sur la ligne série, qu'on ne peut pas ouvrir sans redémarrer la carte |
| `/api/personalities` | POST | créer ou modifier UN caractère — `name` plus, au choix, `color`, `theme`, `rules`, `roulette`, `min_ms`, `max_ms`, `w`. Le caractère voyage d'un bloc : le robot ne fusionne jamais deux demi-mises à jour, et un champ omis garde sa valeur. `w=Normal:1,Happy:.6` est une DÉCLARATION — effacer une émotion, c'est l'omettre. Écrit sur la carte depuis `loop()`, jamais depuis le callback réseau (A2.6) |
| `/api/personalities` | DELETE | supprimer un caractère par `name`. Refuse celui par défaut, et refuse l'ACTIF — en changer d'abord, sinon la suppression ramènerait silencieusement `personality` à 0 |
| `/api/config` | POST | les options d'exécution qui ne sont pas des clés de tuning, dont la langue de l'interface |
| `/api/config/reload` | POST | relire `config.yaml` depuis la carte |
| `/api/wifi` | POST | les identifiants réseau, persistés |

## La caméra

Éteinte par défaut (`camera=0`), initialisée à la demande, arrêtée après
inactivité — voir [`../hardware/PERIPHERALS.fr.md`](../hardware/PERIPHERALS.fr.md).

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/camera/still.jpg` | GET | un instantané JPEG ; `?full=1` pour la pleine qualité VGA |
| `/api/camera/stream` | GET | un flux MJPEG, pour Frigate ou un navigateur |

## La carte SD

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/sd/list` | GET | lister les fichiers connus de la carte, par catégorie |
| `/api/sd/get` | GET | en télécharger un |
| `/api/sd/put` | POST | en envoyer un (`multipart/form-data`) |
| `/api/sd/delete` | DELETE | en supprimer un |
| `/api/dances/files` | GET | les CSV de danses présents sur la carte |
| `/api/dances/file` | POST | en écrire un |
| `/api/dances/file` | DELETE | en supprimer un |
| `/api/dances/reload` | POST | relire `/dances/` |

⚠ Deux envois de 1,7 Mo à la suite échouent. Les espacer, et attendre que
`/api/status` réponde entre les deux.

## Binaires invités et firmware

La chaîne que ces routes pilotent est dessinée dans
[`../architecture/WORKFLOWS.fr.md`](../architecture/WORKFLOWS.fr.md) §12.

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/api/bins` | GET | les `.bin` lançables dans `/bins/` |
| `/api/bins` | POST | en envoyer un |
| `/api/bins` | DELETE | en supprimer un |
| `/api/bins/launch` | POST | flasher un invité et redémarrer dessus. Différé, répond 202 |
| `/api/bins/stop` | POST | depuis un **invité** : reflasher `/companion.bin` et revenir |
| `/api/update` | POST | OTA du companion lui-même |
| `/api/reboot` | POST | redémarrage propre, différé d'environ 1 s pour que la réponse parte d'abord |
| `/api/poweroff` | POST | extinction complète par le PMIC. Ne se rallume pas tout seul |

## La console et sa propre documentation

| Route | Méthode | Ce qu'elle fait |
|---|---|---|
| `/` | GET | la console embarquée, servie pré-compressée |
| `/swagger` | GET | le navigateur d'API |
| `/api/openapi.json` | GET | le document OpenAPI, généré depuis le firmware |
