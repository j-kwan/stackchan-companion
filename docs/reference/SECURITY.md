> **English** · [Français](SECURITY.fr.md)

# What the robot exposes, and what that costs

This is a device with a **camera, two microphones and a motorised head**, and
by default its API is **open on the local network**. That is a deliberate
default for a desk toy on a home LAN, and it is the wrong default for some
networks. This page says exactly what is reachable, so the choice is yours
rather than a surprise.

Nothing here is a claim of hardening. The firmware has no threat model beyond
"a trusted LAN", and pretending otherwise would be worse than saying so.

## What is reachable with no password

Everything. All 47 routes of [`API.md`](API.md), including:

| Route | What an unauthenticated caller can do |
|---|---|
| `/api/camera/still.jpg`, `/api/camera/stream` | **look through the camera** — if `camera=1`. Off by default |
| `/api/sd/get`, `/api/sd/list` | read the card, **including `config.yaml`** and therefore the WiFi password stored in it |
| `/api/sd/put`, `/api/sd/delete` | write or delete anything on the card |
| `/api/bins/launch`, `/api/update` | **flash the robot** with any binary they upload |
| `/api/servo` | move the head |
| `/api/poweroff` | switch it off |

The microphone is not directly readable — there is no endpoint that returns
audio — but `mic_enable` is a tuning key like any other, and the sound levels
appear in `/api/status`.

## Turning authentication on

Basic Auth, applied to the console **and** every `/api/*` route, changeable at
runtime and persisted to the card.

```bash
curl -X POST "http://<ip>/api/security?username=admin&password=chooseone"
```

An **empty password disables it again** — that is the documented way off, not
an oversight. The credentials live in `config.yaml` under `api:`, in clear
text, on a card anybody holding the robot can read. Treat them as protection
against the other devices on your network, never against someone with the
robot in their hands.

Basic Auth over plain HTTP sends the password reversibly encoded on every
request. There is **no HTTPS**: TLS on an ESP32-S3 that is already spending
20 ms of every frame pushing pixels was never on the table, and a self-signed
certificate would train you to click through warnings.

## CORS is off, and turning it on is a real decision

`cors=1` adds `Access-Control-Allow-Origin: *` — after which **any web page you
visit** can call your robot's API from your browser, silently. There is one
legitimate use, and it is why the switch exists: the choreography editor is a
local `file://` page and the browser discards its answers otherwise.

Two properties worth knowing:

- The header list is **global to the server and add-only**, so the decision is
  read once at boot. Turning `cors` off takes a reboot.
- The robot **says so on the serial console** when it starts with CORS open.

Turn it on while you author a dance, turn it off afterwards.

## The network itself

`begin()` resolves the network from four sources, first match wins: NVS (what
someone typed on `/config`), then `config.yaml`, then the compiled default,
then its own access point.

That fallback AP is the part to look at. It runs with a **known SSID and a
known password** — `StackChan-AP` / `goodlife` in the shipped sample — and
serves a captive portal with the full console behind it. Anyone in radio range
who knows the defaults is on the robot. Change them in `config.yaml` if the
robot lives anywhere public.

**And the portal is not passive.** Both sides answer an unknown path with a
redirect to their own page — the companion to `/`, a guest bin to `/config` —
which is what makes the phone's own connectivity probe pop a sign-in sheet the
moment it joins. That is the intended behaviour, and it is also the thing to be
aware of: joining the AP does not merely *allow* reaching the console, it
*offers* it, unprompted, to whoever is in range. On the guest side the redirect
is registered **only in AP mode**, so a bin that joined a real network answers a
missing route with an honest 404 and volunteers nothing.

## Secrets on the card, and in the logs

| Secret | Where it lives |
|---|---|
| WiFi password | `config.yaml`, clear text |
| API password | `config.yaml`, clear text |
| Home Assistant long-lived token | `ha-remote.yaml`, clear text |
| SafeSky key, autorouter account | `flightradar.yaml`, clear text |

The card is unencrypted and the firmware does not pretend otherwise. What it
*does* guarantee is that **secrets never reach a log**: the debug trace cuts
URLs at the query string, tokens are reported as `present`/`ABSENT` rather than
printed, and the settings pages return an empty string for a `Secret` field
instead of echoing it back — so a screenshot of `/config` cannot leak one.

⚠ `sdcard/stackchan-companion/config.yaml` is **tracked in git**. The sample ships
with every credential empty; if you fill it in for your own robot, do not
commit that file back.

## If you want it locked down

In rough order of effect:

1. **Put it on an IoT VLAN** with no route to anything you care about. This is
   the only measure on this list that survives a firmware bug.
2. **Set a password** (`/api/security`) — stops casual access from other
   devices on the LAN.
3. **Leave `camera=0`** unless you are using it. Init is on demand, so an off
   camera is genuinely off, not idling.
4. **Change the fallback AP credentials** in `config.yaml`.
5. **Leave `cors=0`** except while using the choreography editor.

## Reporting something

This is a hobby firmware with no security contact and no advisory process. If
you find something, an issue on the repository is the whole of it — and please
weigh that against the fact that the intended deployment is a desk on a home
network.
