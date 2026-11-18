> [English](README.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# sdcard/ — modèle du contenu de la carte SD

Copier le CONTENU de ce répertoire à la racine de la carte SD (FAT32).

```
/companion.bin               binaire de restauration SD-Updater — NON fourni ici :
                             généré par [SauverFW] du launcher (swipe bas), ou
                             copier .pio/build/companion/firmware.bin renommé.
                             Le zip SD d'une release l'inclut déjà, avec les
                             applis invitées dans /bins/ (docs/INSTALL.fr.md)
/bins/*.bin                  binaires lançables (launcher tactile + API [Bins])
/dances/*.csv                chorégraphies personnalisées (docs/reference/CHOREGRAPHIES.md §6
                             — exemple fourni ; gérables par la console/API)
/sounds/*.wav                samples optionnels (remplacent les chirps — futur)
/stackchan-companion/config.yaml  configuration (schéma complet : docs/reference/CONFIG.md)
/stackchan-companion/rules.txt    règles réactives (docs/reference/PLUGINS.md) — NON
                             fourni ici : le firmware écrit lui-même un modèle
                             commenté la première fois qu'il n'en trouve pas
                             (src/app/RuleStore.h). rules.txt.example, à côté de
                             ce fichier, est un MIROIR de ce modèle, lisible sans
                             posséder le robot — les deux sont comparés par
                             scripts/gates/check-mirrors.py
/stackchan-companion/ha-remote.yaml    config du bin invité ha-remote : hôte, jeton
                             Home Assistant (docs/guests/HA-REMOTE.md)
/stackchan-companion/flightradar.yaml  config du bin invité flight-radar : lat/lon OU
                             airport (IATA/ICAO), radius_nm 10..500, poll_s,
                             api, tz_offset_h (docs/guests/README.md)
/stackchan-companion/space.yaml        config du bin invité space : lat/lon/alt_m de
                             l'observateur, satellite NORAD suivi, min_pass_el,
                             theme, auto_bright (docs/guests/SPACE.fr.md)
/stackchan-companion/runways.csv  base des pistes de la rose METAR de flight-radar —
                             OurAirports (DOMAINE PUBLIC), enregistrements de
                             largeur fixe (17 o) cherchés par dichotomie. À
                             régénérer par python tools/generators/make-runways.py
                             (~236 Kio ; docs/guests/FLIGHT-RADAR.fr.md)
```

Notes :
- Tout est optionnel — sans SD ou sans clé, les défauts compilés s'appliquent.
- ⚠ `config.yaml` porte les mots de passe WiFi et API **en clair**, et la carte
  n'est pas chiffrée. Ce que cela implique pour le robot sur votre réseau :
  [`docs/reference/SECURITY.fr.md`](../docs/reference/SECURITY.fr.md).
- `config.yaml` est RÉGÉNÉRÉ entièrement par le firmware à chaque POST
  /api/tuning ou /api/wifi : seules les sections `wifi:` et `tuning:` sont
  relues au boot, les commentaires manuels sont perdus à la 1re sauvegarde.
- Upload d'un .bin sans retirer la SD : `POST /api/bins` (multipart) puis
  `POST /api/bins/launch?name=xxx.bin`.
