# Lamp-Link


> Two ESP32 lamps, any distance apart — press a button on one, the other responds, over encrypted MQTT.


Press a button in one city, an LED lights up in another. No port forwarding, no static IP, no monthly cost. End-to-end latency is typically **under 100 ms**.


This repository is **Step 1** of a larger project: a pair of desk-side lamps that let two people send each other simple, ambient signals — *I'm on a call*, *I'm free*, *I'm thinking of you* — without opening a phone.


---


## Status


| Step | Feature | State |
|---|---|---|
| 1 | Button → remote LED blink over TLS-encrypted MQTT | ✅ **Working** |
| 2 | WS2812B LED strip with busy / free / miss-you patterns | Planned |
| 3 | Button grammar: single / double / long press | Planned |
| 4 | WiFi captive portal setup (no hardcoded credentials) | Planned |
| 5 | Colour-code pairing between lamps | Planned |


---


## How it works


Neither board talks to the other directly. Home internet connections have no fixed public address, and routers block incoming connections by default — so instead, **both boards dial out** to a shared server called an MQTT broker.


```
  [Board A]  ──outbound──▶  ┌───────────┐  ◀──outbound──  [Board B]
   Your home                │  BROKER   │                 Their home
                            │  (cloud)  │
                            └───────────┘
```


Because both connections are **outbound**, neither network needs any configuration. This is the same reason a messaging app works on your phone without your phone having a public address.


MQTT itself has only two verbs:


- **publish** — drop a message into a named box (a "topic")
- **subscribe** — ask the broker to forward anything dropped into that box


Topics are arbitrary strings you invent. This project uses:


| Board | Subscribes to (its inbox) | Publishes to (peer's inbox) |
|---|---|---|
| A | `lamplink/lampA/cmd` | `lamplink/lampB/cmd` |
| B | `lamplink/lampB/cmd` | `lamplink/lampA/cmd` |


**Each board publishes to the *other* board's topic.** That single detail is why pressing your button doesn't change your own lamp — and it's the foundation the pairing model in Step 5 builds on.


---


## Hardware


| Item | Notes |
|---|---|
| ESP32 dev board ×2 | Any ESP32 with WiFi. Tested on ESP32-D0WD-V3 |
| Tactile push button ×2 | Or just touch a jumper wire to GND for testing |
| USB **data** cable | Charge-only cables will not work — a common time sink |


### Wiring — identical on both boards


```
   Button leg 1  ──────  GPIO 4
   Button leg 2  ──────  GND


   LED           ──────  GPIO 2   (onboard blue LED on most DevKits)
```


No resistors required. The firmware enables the ESP32's internal pull-up, which holds GPIO 4 at 3.3 V; pressing pulls it to ground. So **released = HIGH, pressed = LOW** — the logic reads backwards, which is normal.


> ⚠️ **Do not move the button to GPIO 0, 2, 12 or 15.** The ESP32 samples those pins at boot to decide how to start up. A held button on one of them can prevent the board booting entirely.


If your board has no onboard LED: `GPIO 2 → 330 Ω resistor → LED anode`, and `LED cathode → GND`.


---


## Setup


### 1. Create a free MQTT broker


Sign up at [EMQX Cloud](https://www.emqx.com/en/cloud/serverless-mqtt) and create a **Serverless** deployment. Choose the region closest to you.


Serverless is TLS-only — it accepts port 8883 and refuses unencrypted 1883, which is exactly what you want.


From the deployment Overview page, collect:


- **Connection address** — e.g. `y61fb3f1.ala.asia-southeast1.emqxsl.com`
- **Port** — `8883`
- **CA certificate** — download this file


Then create **two separate users** (one per lamp) under Access Control → Client Authentication. Don't share one login across both boards — if a board is lost or compromised, you want to revoke it alone.


> **Avoid public test brokers** like `broker.emqx.io` or `test.mosquitto.org`. They require no signup, which means anyone can subscribe to your topics and read or spoof your messages. The free account-based tier costs the same (nothing) and is private.


### 2. Configure secrets


```bash
cp include/secrets.example.h include/secrets.h
```


Fill in `include/secrets.h` with your WiFi credentials, broker host, per-board username and password, and the CA certificate.


`secrets.h` is gitignored and never committed. `secrets.example.h` is the template that *is* committed, so anyone cloning the repo knows what to provide.


### 3. Flash both boards


Both boards run **identical code**. The only difference is two lines:


```c
// Board A                        // Board B
#define MY_ID    "lampA"          #define MY_ID    "lampB"
#define PEER_ID  "lampB"          #define PEER_ID  "lampA"
```


Also set `MQTT_USER` / `MQTT_PASS` to that board's own broker account.


```bash
pio run -e lampA -t upload      # with board A's secrets.h in place
# edit secrets.h for board B, then:
pio run -e lampB -t upload
```


> Only one `secrets.h` exists at a time, so edit it between flashes. Keeping `secretsA.txt` / `secretsB.txt` notes **outside** the repo makes swapping copy-paste rather than retyping.


### 4. Verify


```bash
pio device monitor
```


Press the board's **EN/RST** button to see the boot sequence from the start:


```
=== LAMP-LINK step 1 ===
I am 'lampA', my peer is 'lampB'
[WiFi] Connected. IP = 192.168.29.3
[Time] Clock set: Wed Sep 16 22:31:04 2026
[MQTT] Connecting as lamp-lampA-6f0ff0a4 ... connected
[MQTT] Subscribed to lamplink/lampA/cmd
```


The LED blinks 3× on successful connection — that's your "I'm online" signal.


**Press the button on A → B blinks 3×.** That's the whole concept, proven.


---


## Diagnostics


`tools/diagnostic.cpp` is a standalone sketch that runs the entire connection chain one layer at a time and **stops at the first failure with an explanation**. Swap it in as `src/main.cpp` when something misbehaves.


| # | Test | Catches |
|---|---|---|
| 1 | Board info + config sanity | Empty SSID, trailing spaces, `mqtts://` in hostname, unreplaced cert placeholder |
| 2 | WiFi scan | What the radio can *actually* see |
| 3 | Find network | Missing SSID, wrong capitalisation, weak signal, channel > 11 |
| 4 | WiFi connect | Wrong password vs. network vanished — decoded |
| 5 | Internet reachability | Connected to router but no internet; captive portals |
| 6 | NTP clock sync | The root cause of most TLS failures |
| 7 | DNS lookup | Typo'd broker hostname |
| 8 | Raw TCP to 8883 | Port blocked by network/firewall |
| 9 | TLS handshake | Bad CA certificate, **isolated** |
| 10 | MQTT connect | Wrong credentials, ACL rules |
| 11 | Publish/subscribe loopback | Full round trip, no second board needed |


The ordering is the point: by the time test 9 runs, tests 6 and 8 have already proven the clock is right and the port is open — so a TLS failure can **only** be the certificate. Each test eliminates a variable for the ones after it.


---


## Troubleshooting


### Read the serial log top to bottom


Each line tells you how far you got.


| Last line shown | Problem area |
|---|---|
| Nothing at all | Baud rate (must be 115200), or you missed the boot messages — press EN/RST |
| Stuck on `[WiFi] Connecting...` | SSID/password, or a 5 GHz network |
| Stuck on `[Time] Syncing...` | On WiFi but no real internet |
| `FAILED, state = N` | Broker — see codes below |


### MQTT state codes


| Code | Meaning | Most likely cause |
|---|---|---|
| **-4** | Timeout | Hostname wrong — check you removed `mqtts://` and `:8883`. Or outbound 8883 blocked |
| **-2** | Connect failed | **TLS.** Clock didn't sync, or the certificate is mangled |
| **2** | Bad client ID | Two boards using the same `MY_ID` |
| **4** | Bad credentials | Wrong `MQTT_USER` / `MQTT_PASS` |
| **5** | Not authorised | Credentials fine, but an ACL rule blocks that topic |


### Common gotchas


| Symptom | Cause |
|---|---|
| Serial monitor blank after upload | Boot messages already printed — press EN/RST |
| Endless `Connecting......` dots | SSID typo, or 5 GHz network. Run the diagnostic |
| Board publishes but never receives | `mqtt.loop()` not called every pass |
| One press → several blinks | Contact bounce; raise `DEBOUNCE_MS` |
| Board won't boot with button wired | Button on a strapping pin |
| Browser MQTT client won't connect | Browsers need **WebSocket port 8084**, not 8883 |
| Both boards connect then drop repeatedly | Duplicate client ID |
| Upload hangs at `Connecting....` | Hold **BOOT** while uploading |


---


## Security


| Layer | What it provides |
|---|---|
| **TLS on port 8883** | Encrypts all traffic. `setCACert()` also verifies you're talking to *your* broker, not an impostor |
| **Per-device credentials** | One account per lamp — a lost board is revoked alone |
| **Topic ACLs** | Each lamp may only publish to its peer's topic and subscribe to its own |
| **Gitignored secrets** | WiFi password and broker credentials never reach GitHub |


**Never call `setInsecure()`.** It skips certificate verification, silently removing the identity check that TLS exists to provide. Traffic stays encrypted, but anyone able to intercept it can impersonate your broker and collect your credentials.


### Why the clock matters


TLS certificates carry validity dates, so the ESP32 must know today's date to verify one. On power-up it believes it's 1 January 1970 — from which every certificate on Earth looks "not yet valid." That's why NTP sync runs *before* any TLS attempt, and why a failure there surfaces as the cryptic `state = -2`.


---


## Cost


**₹0 / month.**


EMQX Serverless's free tier includes 1 million session-minutes and 1 GB of traffic per month, with no card required. Two boards connected 24/7 for a full month uses roughly **86,400 session-minutes — about 8% of the allowance**. Messages here are 5 bytes, so traffic is negligible.


You could add six more lamps and stay comfortably inside the free tier. Free tiers carry no uptime guarantee, which for an ambient mood lamp is entirely acceptable.


---


## Project structure


```
lamp-link/
├── platformio.ini            two build targets, one codebase
├── .gitignore                blocks secrets.h
├── README.md
├── include/
│   ├── secrets.example.h     template — committed
│   └── secrets.h             your real values — NOT committed
├── src/
│   └── main.cpp              the firmware
└── tools/
    └── diagnostic.cpp        11-step connection diagnostic
```


---


## Roadmap


**Step 2 — LED strip.** FastLED driving 10 vertical segments. Three looks: busy (red), free (green), miss-you (red/pink/white cycle).


**Step 3 — Button grammar.**


| Action | Effect | Scope |
|---|---|---|
| Single press | Cycle lighting pattern | Local only |
| Double press | Send "I miss you" — 60 s, then revert | Peers only |
| Long press (~1.5 s) | Toggle status: busy ↔ free | Peers only |
| Hold 5 s | Enter pairing mode | Setup |


The single press is **context-aware**: if a remote message is currently displayed, a press *clears* it (acknowledged) rather than cycling patterns. A slow pulse on the bottom LED distinguishes the two modes.


**Step 4 — Captive portal.** WiFiManager, so a lamp can be set up from a phone with no re-flashing.


**Step 5 — Pairing.** Hold the button to generate a 5-colour code displayed up the strip, fading as a countdown. The peer enters the same sequence, lighting each colour as it's chosen. Match → both flash green; mismatch → both flash red. The offer is published under a *hash* of the code, so an eavesdropper on the pairing topic learns nothing.


### Two design rules carried forward


**Status is sticky; events are not.** Publish busy/free with `retained = true`, so a rebooting lamp immediately learns the current state. Publish "I miss you" with `retained = false` — otherwise it replays on every power-up.


**Pairing is an edge, not a group.** Each lamp keeps its own peer list. If A–B and B–C are paired, pressing B reaches A and C, but pressing A reaches only B. Never relay a *received* command onward — only physical button presses should send — or the pairing model quietly becomes a mesh. If you later add relaying, every message needs a UUID and a TTL, or pairing A–B–C–A creates an infinite storm.


---


## Built with


- [PlatformIO](https://platformio.org/) — build system
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT client
- [EMQX Serverless](https://www.emqx.com/en/cloud/serverless-mqtt) — broker


## License


MIT




