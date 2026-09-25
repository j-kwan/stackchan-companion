> **English** · [Français](INSTALL.fr.md)

# Installing a release

How to put a published release on a StackChan K151 (M5Stack CoreS3), prepare
its SD card, connect it to your WiFi and keep it up to date. This page is the
same for every release: file names below use `<version>` for the release tag
(for example `v1.0.0`). No build tools are needed. To build from source
instead, see the [README](../README.md#quick-start) and
[`CONTRIBUTING.md`](../CONTRIBUTING.md).

## What you need

- A **StackChan K151** (M5Stack CoreS3 in the K151 body).
- A **USB-C cable that carries data**. A charge-only cable is the most common
  reason no serial port appears.
- A **microSD card** formatted FAT32 (32 GB or less is the safe choice). It is
  optional for the face itself, but needed to save the WiFi settings, to run
  the guest apps and to come back from them.
- To flash: **Chrome or Edge** (browser flasher, nothing to install), or
  Python with `pip install esptool`.

## The release files

| File | What it is | Where it goes |
|---|---|---|
| `companion-<version>-factory.bin` | the main firmware, **complete image** (bootloader, partition table, app) | flashed over USB at offset `0x0` |
| `companion.bin` | the same firmware, app only | SD card root, and the file for OTA updates |
| `flight-radar.bin`, `space.bin`, `ha-remote.bin`, `led-fluid.bin` | the guest apps ([what they do](guests/README.md)) | SD card, `/bins/` |
| `sdcard-<version>.zip` | a ready SD card: the template from [`sdcard/`](../sdcard/README.md) **plus** `companion.bin` and the four guest apps | unzip to the card root |
| `flight-radar-fire-<version>-factory.bin`, `space-fire-<version>-factory.bin`, `led-fluid-fire-<version>-factory.bin` | standalone apps for an **M5Stack Fire** (not the StackChan) | flashed over USB at `0x0`, see [M5Stack Fire](#m5stack-fire-standalone-apps) |
| `SHA256SUMS.txt` | checksums of every file above | [check your download](#checking-the-download) |

⚠ Never flash `companion.bin` (or a guest `.bin`) at offset `0x0`: it has no
bootloader and the board will not start. The `-factory.bin` is the one for USB.

## 1. Flash the companion over USB

Plug the robot into the computer.

**Option A: in the browser (Chrome or Edge)**

1. Open <https://espressif.github.io/esptool-js/>.
2. Baud rate `921600`, then **Connect** and pick the robot's port (on
   Windows, a *USB Serial Device* / *USB JTAG/serial debug unit*).
3. Flash address `0x0`, file `companion-<version>-factory.bin`.
4. **Program**, wait for the end, then press the robot's reset button.

**Option B: command line**

```bash
python -m esptool --chip esp32s3 --port COM6 --baud 921600 write_flash 0x0 companion-<version>-factory.bin
```

Replace `COM6` with your port (`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem…` on
macOS).

**No port shows up?** Try another cable first. Then put the board in download
mode: hold the reset button for about 3 seconds, until the small green LED
next to it lights up, and connect again.

The factory image also resets the "which slot boots" record, so the board
starts the firmware you just wrote, whatever ran before. Settings stored in
the board's own memory (for instance a network typed into a guest app) are
kept.

## 2. Prepare the SD card

Unzip `sdcard-<version>.zip` to the **root** of the FAT32 card, so that
`companion.bin` sits at the top level next to `bins/`, `dances/` and
`stackchan-companion/`. Insert the card with the robot off.

Two files matter more than the others:

- **`/companion.bin`** is how the robot comes back from a guest app: leaving
  one reinstalls the companion from this file. It must be the **same version**
  as the firmware you flashed. An older copy here would quietly bring the
  older version back the first time you leave a guest app.
- **`/stackchan-companion/config.yaml`** holds the settings, WiFi included.
  The full schema is in [`reference/CONFIG.md`](reference/CONFIG.md).

## 3. First start and WiFi

With no network configured, the robot opens its own WiFi network:

| | |
|---|---|
| Network | `StackChan-AP` |
| Password | `goodlife` |
| Console | opens by itself (captive portal), otherwise `http://192.168.4.1/` |

To join your home network, pick one:

- **From the console**: tab **System**, block **WiFi network**, enter the SSID
  and password, **Save**, then restart the robot (the **⟳ Restart** button in
  the same tab).
- **From the card**, before inserting it: fill `client_ssid` and
  `client_password` under `wifi:` in `/stackchan-companion/config.yaml`.

Either way the credentials are written to the SD card, so they need one. Once
connected, the console is at `http://stackchan.local/` (or the IP address your
router gave the robot). If the network cannot be joined within 10 seconds, the
robot falls back to its own `StackChan-AP` network again, so it always stays
reachable.

⚠ The console has **no password by default**, and the fallback network's
password is public. Before leaving the robot on a shared network, read
[`reference/SECURITY.md`](reference/SECURITY.md): it explains what is exposed
and how to set a console password and your own fallback network password.

## 4. Configure

Everything is in the console, and each change is saved to the card:

| Tab | What you set there |
|---|---|
| **Pilot** | expressions, dances, the head by hand, the reactive rules |
| **Options** | behaviour, eyes, LEDs, sound, status bar |
| **Characters** | which character the robot is ([`reference/PERSONALITIES.md`](reference/PERSONALITIES.md)) |
| **Files** | the SD card: import, download and delete files, choreographies, guest app settings |
| **System** | WiFi, console password, firmware update, which build is running, debug trace, restart |

The reference for each setting is [`reference/CONFIG.md`](reference/CONFIG.md);
the REST API behind the console is in [`reference/API.md`](reference/API.md)
(and the robot serves its own Swagger page). Dances you design in the
[choreography editor](../tools/README.md) are imported from the **Files** tab.

## 5. Guest apps

The four apps in `/bins/` replace the companion while they run and give it
back when you leave.

- **Start one**: swipe down from the top of the robot's screen to open the
  launcher, or use the 🚀 button next to the file in the console's **Files**
  tab.
- **Leave one**: swipe down again and confirm, or use the button on the app's
  own web page. The companion is reinstalled from `/companion.bin` in about ten
  seconds. During that time the robot answers ping but not the web pages:
  this is normal, wait for it.
- **Configure one**: each app has a settings file in `/stackchan-companion/`
  (`flightradar.yaml`, `space.yaml`, `ha-remote.yaml`, `ledfluid.yaml`) and its
  own page at `http://<robot>/config` while it runs. Some need a key or a
  token: `ha-remote` needs a Home Assistant token, see its page.

| App | Page |
|---|---|
| `flight-radar` | [`guests/FLIGHT-RADAR.md`](guests/FLIGHT-RADAR.md) |
| `space` | [`guests/SPACE.md`](guests/SPACE.md) |
| `ha-remote` | [`guests/HA-REMOTE.md`](guests/HA-REMOTE.md) |
| `led-fluid` | [`guests/LED-FLUID.md`](guests/LED-FLUID.md) |

## 6. Updating to a new release

No cable needed. Download the new release, then:

1. Console, **System** tab, block **Firmware · OTA update**: choose the new
   `companion.bin`, **Flash**. The robot restarts on it.
2. Console, **Files** tab, **⬆ Import**: type **companion.bin (restore)**, the
   same `companion.bin`. **Do not skip this step**: otherwise the next guest
   app you leave puts the previous version back.
3. Same tab, type **binary (.bin)**: import each new guest app over the old
   one.

Your `config.yaml`, rules, characters and dances stay on the card. The USB
method of step 1 also works for an update, as long as you then refresh
`/companion.bin` on the card too.

The **System** tab shows which OTA slot started and the fingerprint of the
running build, and the reason for the last restart (`panic`, `task_wdt` or
`brownout` mean a crash).

## M5Stack Fire standalone apps

`flight-radar`, `space` and `led-fluid` also exist as standalone applications
for an **M5Stack Fire** (three buttons, no touch screen, no StackChan). They
are flashed the same way, with the other chip name:

```bash
python -m esptool --chip esp32 --port COM7 --baud 921600 write_flash 0x0 space-fire-<version>-factory.bin
```

⚠ With both boards plugged in, check the port twice: flashing a Fire image on
the StackChan erases its companion.

The Fire has no companion to borrow a network from. On first start it opens
the `SCE-Guest` network (password `goodlife`); join it and the settings page
opens by itself (`http://192.168.4.1/config`). Enter your WiFi in its
**Network** block. An SD card with the same `/stackchan-companion/*.yaml` files
is read as well, but is not required.

## Checking the download

`SHA256SUMS.txt` lists a checksum per file.

```powershell
Get-FileHash .\companion-<version>-factory.bin -Algorithm SHA256   # Windows
```

```bash
sha256sum -c SHA256SUMS.txt --ignore-missing                       # Linux / macOS
```

## Troubleshooting

| Symptom | Cause, and what to do |
|---|---|
| no serial port | charge-only cable, or the board needs download mode (reset button held ~3 s) |
| the browser flasher stops midway | lower the baud rate to `115200` and start again |
| after an update, the old version is back | `/companion.bin` on the card is older: import the new one (step 6.2) |
| the robot pings but the console does not load, right after leaving a guest app | the companion is being reinstalled from the card, wait up to a minute |
| "companion.bin missing or invalid" in a guest app | the card has no valid `/companion.bin`: copy the release's one to the root |
| WiFi settings forgotten after a restart | no SD card, or the card is not FAT32 |
| everything is wrong and you want a clean start | `python -m esptool --chip esp32s3 --port COM6 erase_flash`, then step 1. This also wipes settings kept in the board's memory |

Still stuck: the serial console (115200 baud) tells what the robot is doing at
every step once the debug trace is on (**System** tab). The hardware notes in
[`hardware/`](hardware/README.md) explain the limits behind most of these
symptoms.
