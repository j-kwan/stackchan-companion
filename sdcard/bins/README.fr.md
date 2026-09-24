> [English](README.md) · **Français** — l'anglais est la version de référence.
> Ce fichier peut être en retard d'une mise à jour : en cas de divergence, l'anglais fait foi.

# /bins/ — binaires lançables

Déposer ici des `.bin` ESP32-S3 (app OTA). Lancement :
- tactile : swipe haut→bas → launcher → sélectionner → [Lancer]
- API : `POST /api/bins/launch?name=mon_app.bin`

Retour au companion :
- .bin quelconque : reset + BtnA maintenu (SD-Updater standard, recharge /companion.bin)
- .bin maison : embarquer `src/guest/SceGuest.h` → `POST /api/bins/stop` distant

Générer un bin invité depuis ce repo (jamais `-t upload` : cela flasherait le
companion par USB — un invité se déploie dans `/bins/`, jamais sur la puce) :
```
pio run -e flight-radar
copy .pio\build\flight-radar\firmware.bin sdcard\bins\flight-radar.bin
```
Idem pour `space` et `ha-remote` (`docs/guests/`).
Garde-fou firmware : un .bin plus gros que la partition OTA est refusé au lancement.

Les routes derrière tout cela — envoi, lancement, arrêt — sont dans
[`docs/reference/API.fr.md`](../../docs/reference/API.fr.md), et la chaîne
qu'elles pilotent (lancement, hall, arrêt coopératif, retour) est dessinée dans
[`docs/architecture/WORKFLOWS.fr.md`](../../docs/architecture/WORKFLOWS.fr.md) §12.
