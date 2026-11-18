> **English** · [Français](README.fr.md)

# /bins/ — launchable binaries

Drop ESP32-S3 `.bin` files here (OTA app). Launching:
- touch: swipe top→bottom → launcher → select → [Lancer]
- API: `POST /api/bins/launch?name=my_app.bin`

Getting back to the companion:
- any .bin: reset + BtnA held (standard SD-Updater, reloads /companion.bin)
- home-made .bin: embed `src/guest/SceGuest.h` → remote `POST /api/bins/stop`

Building a guest bin from this repo (never `-t upload`: that would flash the
companion over USB — a guest is deployed to `/bins/`, never to the chip):
```
pio run -e flight-radar
copy .pio\build\flight-radar\firmware.bin sdcard\bins\flight-radar.bin
```
Same for `space` and `ha-remote` (`docs/guests/`).
Firmware safeguard: a .bin larger than the OTA partition is refused at launch.

The routes behind all of this — upload, launch, stop — are in
[`docs/reference/API.md`](../../docs/reference/API.md), and the chain they
drive (launch, lobby, cooperative stop, return) is drawn in
[`docs/architecture/WORKFLOWS.md`](../../docs/architecture/WORKFLOWS.md) §12.
