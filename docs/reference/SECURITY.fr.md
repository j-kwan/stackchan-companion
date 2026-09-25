> [English](SECURITY.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# Ce que le robot expose, et ce que ça coûte

C'est un appareil avec une **caméra, deux microphones et une tête motorisée**,
et par défaut son API est **ouverte sur le réseau local**. C'est un défaut
délibéré pour un jouet de bureau sur un LAN domestique, et c'est le mauvais
défaut sur certains réseaux. Cette page dit exactement ce qui est atteignable,
pour que le choix soit le vôtre plutôt qu'une surprise.

Rien ici n'est une prétention de durcissement. Le firmware n'a pas de modèle de
menace au-delà de « un LAN de confiance », et prétendre le contraire serait
pire que de le dire.

## Ce qui est atteignable sans mot de passe

Tout. Les 47 routes de [`API.fr.md`](API.fr.md), dont :

| Route | Ce qu'un appelant non authentifié peut faire |
|---|---|
| `/api/camera/still.jpg`, `/api/camera/stream` | **regarder par la caméra** — si `camera=1`. Éteinte par défaut |
| `/api/sd/get`, `/api/sd/list` | lire la carte, **y compris `config.yaml`**, donc le mot de passe WiFi qui s'y trouve |
| `/api/sd/put`, `/api/sd/delete` | écrire ou supprimer n'importe quoi sur la carte |
| `/api/bins/launch`, `/api/update` | **flasher le robot** avec n'importe quel binaire envoyé |
| `/api/servo` | bouger la tête |
| `/api/poweroff` | l'éteindre |

Le microphone n'est pas lisible directement — aucun point d'entrée ne rend de
l'audio — mais `mic_enable` est une clé de tuning comme une autre, et les
niveaux sonores apparaissent dans `/api/status`.

## Activer l'authentification

Basic Auth, appliquée à la console **et** à toutes les routes `/api/*`,
modifiable à chaud et persistée sur la carte.

```bash
curl -X POST "http://<ip>/api/security?username=admin&password=choisissez"
```

Un **mot de passe vide la désactive** — c'est la porte de sortie documentée,
pas un oubli. Les identifiants vivent dans `config.yaml` sous `api:`, en clair,
sur une carte que quiconque tient le robot peut lire. À considérer comme une
protection contre les autres appareils de votre réseau, jamais contre quelqu'un
qui a le robot en main.

Basic Auth sur du HTTP en clair envoie le mot de passe encodé de façon
réversible à chaque requête. Il n'y a **pas de HTTPS** : TLS sur un ESP32-S3
qui dépense déjà 20 ms par frame à pousser des pixels n'a jamais été
envisageable, et un certificat auto-signé vous entraînerait à cliquer à travers
les avertissements.

## CORS est fermé, et l'ouvrir est une vraie décision

`cors=1` ajoute `Access-Control-Allow-Origin: *` — après quoi **n'importe
quelle page web que vous visitez** peut appeler l'API de votre robot depuis
votre navigateur, en silence. Il existe un usage légitime, et c'est pour lui
que l'interrupteur existe : l'éditeur de chorégraphies est une page `file://`
locale, et le navigateur jette ses réponses sans cela.

Deux propriétés à connaître :

- La liste d'en-têtes est **globale au serveur et en ajout seul** : la décision
  est donc lue une fois au démarrage. Refermer `cors` demande un redémarrage.
- Le robot **le dit sur la console série** quand il démarre avec CORS ouvert.

Ouvrez-le le temps d'écrire une danse, refermez-le ensuite.

## Le réseau lui-même

`begin()` résout le réseau depuis quatre sources, la première qui répond
l'emporte : NVS (ce que quelqu'un a saisi sur `/config`), puis `config.yaml`,
puis le défaut compilé, puis son propre point d'accès.

C'est ce point d'accès de repli qu'il faut regarder. Il tourne avec un **SSID
et un mot de passe connus** — `StackChan-AP` / `goodlife` dans l'exemple livré
— et sert un portail captif avec la console complète derrière. Quiconque est à
portée radio et connaît les défauts est sur le robot. Changez-les dans
`config.yaml` si le robot vit dans un lieu public.

**Et ce portail n'est pas passif.** Les deux côtés répondent à un chemin inconnu
par une redirection vers leur propre page — le companion vers `/`, un bin invité
vers `/config` — et c'est ce qui fait surgir la fenêtre de connexion dès qu'un
téléphone rejoint le réseau, via sa propre sonde de connectivité. C'est le
comportement voulu, et c'est aussi ce dont il faut avoir conscience : rejoindre
le point d'accès ne fait pas que *permettre* d'atteindre la console, cela la
*propose*, sans qu'on ait rien demandé, à qui est à portée. Côté invité la
redirection n'est posée qu'**en mode AP** : un bin qui a rejoint un vrai réseau
répond à une route absente par un 404 honnête et ne propose rien.

## Les secrets, sur la carte et dans les logs

| Secret | Où il vit |
|---|---|
| Mot de passe WiFi | `config.yaml`, en clair |
| Mot de passe API | `config.yaml`, en clair |
| Jeton longue durée Home Assistant | `ha-remote.yaml`, en clair |
| Clé SafeSky, compte autorouter | `flightradar.yaml`, en clair |

La carte n'est pas chiffrée et le firmware ne prétend pas le contraire. Ce
qu'il garantit en revanche, c'est qu'**aucun secret n'atteint un journal** : la
trace de debug coupe les URL à la query string, les jetons sont rapportés en
`present`/`ABSENT` plutôt qu'imprimés, et les pages de réglages rendent une
chaîne vide pour un champ `Secret` au lieu de le réafficher — une capture
d'écran de `/config` ne peut donc pas en fuiter un.

⚠ `sdcard/stackchan-companion/config.yaml` est **suivi par git**. L'exemple livré a
tous ses identifiants vides ; si vous le remplissez pour votre propre robot, ne
le recommittez pas.

## Si vous voulez verrouiller

Par ordre d'effet décroissant :

1. **Le mettre sur un VLAN IoT** sans route vers ce qui compte. C'est la seule
   mesure de cette liste qui survive à un bug du firmware.
2. **Poser un mot de passe** (`/api/security`) — coupe l'accès désinvolte
   depuis les autres appareils du LAN.
3. **Laisser `camera=0`** sauf usage. L'init est à la demande : une caméra
   éteinte est vraiment éteinte, pas au ralenti.
4. **Changer les identifiants du point d'accès de repli** dans `config.yaml`.
5. **Laisser `cors=0`** sauf pendant l'usage de l'éditeur de chorégraphies.

## Signaler quelque chose

C'est un firmware de loisir, sans contact sécurité ni processus d'avis. Si vous
trouvez quelque chose, une issue sur le dépôt est tout ce qu'il y a — et
pesez-le au regard du fait que le déploiement visé est un bureau sur un réseau
domestique.
