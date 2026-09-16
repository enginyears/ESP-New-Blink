# Lamp-Link · Complete Setup Guide for Beginners


From "I have two ESP32 boards in a drawer" to "I pressed a button and a light blinked somewhere else."


No prior knowledge assumed. Every term is explained the first time it appears.


---


## Part 0 — What a CA certificate actually is


This is the one concept that confuses everyone, so let's kill it properly before you touch anything.


### The problem it solves


Your ESP32 wants to talk to a server on the internet. Two things can go wrong:


1. **Someone reads your messages** as they cross the internet.
2. **Someone pretends to BE the server**, so your ESP32 happily sends its username and password to a stranger.


Encryption alone only fixes problem 1. If you encrypt a conversation with an impostor, you have a beautifully secure conversation with the wrong person.


### The passport analogy


Imagine you're meeting someone you've only spoken to online. They show up and say "I'm Ravi." How do you know?


They hand you a **passport**. You don't personally know Ravi, but you *do* trust the government that issued that passport. You recognise the government's seal, the hologram, the signature. So you trust the passport, and therefore you trust that this is Ravi.


- The **passport** = the server's certificate. Your broker hands this to your ESP32 on every connection.
- The **government that issued it** = the Certificate Authority, or **CA**.
- **Recognising the government's seal** = having the CA certificate stored on your ESP32 in advance.


The CA certificate is *not* secret. It's not a password. It's the reference copy of the official seal, so your device can check whether the passport it was handed is genuine. Publishing it publicly is completely fine and normal — it only lets people *verify*, never impersonate.


### Why your ESP32 needs it explicitly


Your laptop and phone ship with a few hundred CA certificates preinstalled. That's why browsing to a website "just works" and you see a padlock — your browser silently checked the site's passport against its built-in list.


An ESP32 has no such list. It's a small chip with limited memory and no preloaded trust store. So **you** hand it the one CA certificate it needs, by pasting it into your code. That's the entire reason this step exists.


### What happens if you skip it


You'd call `setInsecure()`, which means "accept any certificate without checking." Your traffic is still encrypted, but you've removed the identity check. Anyone able to intercept your connection — a compromised router, hostile public WiFi — can impersonate your broker, collect your credentials, and control your lamps.


**Never call `setInsecure()`.** For a device that lives in someone else's home, this matters.


### Why the clock matters (this causes ~80% of failures)


Every certificate says "valid from this date until that date." To check a certificate, the ESP32 must know **today's date**.


On power-up, an ESP32 believes it's 1 January 1970. Every certificate on Earth looks "not yet valid" from 1970, so verification fails and your connection dies with error `state = -2`.


That's why the code asks an internet time server for the date *before* attempting the secure connection. If you ever see `-2`, check the clock first.


### What it looks like


A CA certificate is a plain text file. Open it in Notepad and you'll see:


```
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
... about 20 more lines of this ...
-----END CERTIFICATE-----
```


That scrambled text is the CA's public key plus its details. You don't need to understand a single character of it. You copy it, paste it into your code between the markers, and you're done.


**Formatting rules that will bite you:**


- Keep both the `-----BEGIN CERTIFICATE-----` and `-----END CERTIFICATE-----` lines. They're structural, not decoration.
- Don't add spaces or indentation.
- Don't re-wrap or join the lines. The line breaks are meaningful.
- Don't let an editor "helpfully" reformat it.


A mangled certificate doesn't give you a clear error. It gives you `state = -2`, same as a wrong clock — which is why people lose hours here.


---


## Part 1 — Install the tools


### 1.1 Visual Studio Code


Download from [code.visualstudio.com](https://code.visualstudio.com/) and install with all defaults.


### 1.2 The PlatformIO extension


PlatformIO is what turns VS Code into an ESP32 development environment. It downloads the compiler, the ESP32 toolchain, and all your libraries automatically.


1. Open VS Code
2. Click the **Extensions** icon in the left sidebar (four squares, one detached)
3. Search for `PlatformIO IDE`
4. Click **Install**


**This first install takes 5–15 minutes** and downloads several hundred MB. Watch the bottom status bar — it'll show progress. Don't interrupt it. When it's done it may ask you to reload VS Code; let it.


You'll know it worked when an **alien head icon** 👽 appears in the left sidebar.


### 1.3 USB drivers


Your computer needs a driver to see the ESP32 as a serial port. Most boards use one of two chips:


| Chip on your board | Driver needed |
|---|---|
| CP2102 / CP2104 | Silicon Labs CP210x VCP driver |
| CH340 / CH9102 | WCH CH341SER driver |


Look at the small square chip near the USB port on your board — the part number is usually printed on it. Download that driver from the manufacturer's site and install it.


**On Windows 11 and recent macOS these are often already included.** Easiest test: plug the board in and skip ahead to §5.1. If a new port appears, you don't need to install anything.


### 1.4 A cable that actually carries data


This one wastes more beginner hours than any other single thing.


Many USB cables — especially ones bundled with phone chargers and power banks — contain only power wires, no data wires. The board will light up and look perfectly alive, but your computer will never see a port.


**If no port appears, try a different cable before debugging anything else.**


---


## Part 2 — Create your free MQTT broker


Recap: the broker is the server in the middle. Both lamps dial *out* to it, so neither needs a public address or any router configuration.


### 2.1 Sign up


Go to [emqx.com/en/cloud](https://www.emqx.com/en/cloud/serverless-mqtt) and create an account. No credit card is required for the Serverless free tier.


### 2.2 Create a Serverless deployment


In the console, create a new deployment and choose the **Serverless** type.


- **Region** — pick the one physically closest to you. From India, Singapore or Mumbai. Closer region = lower latency.
- **Name** — anything. `lamp-link` is fine.
- **Spend limit** — leave it at the free allowance. This is your safety net: it caps usage at the free tier so you can never be surprised by a bill.


Creation takes a minute or two. Wait for the status to show as running.


### 2.3 Collect three values from the Overview page


Open your deployment. The overview/connection area gives you what you need:


| What | Looks like | Goes into |
|---|---|---|
| **Connection address** | `abc123.ala.ap-southeast-1.emqxsl.com` | `MQTT_HOST` |
| **Port (TLS)** | `8883` | `MQTT_PORT` |
| **CA certificate** | a download link | `MQTT_CA_CERT` |


**Important:** copy the hostname *only*. If the console shows `mqtts://abc123...:8883`, strip the `mqtts://` prefix and the `:8883` suffix. Leaving them in causes a confusing timeout (`state = -4`).


> **Why port 8883?** 1883 is plain, unencrypted MQTT. 8883 is MQTT wrapped in TLS. Serverless deployments are TLS-only — they won't even accept 1883, which is a feature, not a limitation.


### 2.4 Create two users — one per lamp


Find the access control / authentication section of your deployment and add **two** users:


| Username | Password |
|---|---|
| `lampA` | a long random string |
| `lampB` | a different long random string |


**Why two, not one?** If a board is lost, given away, or compromised, you delete that one account. The other keeps working. One shared login means one problem forces you to re-flash everything.


Generate real random passwords — 20+ characters. You'll paste them into code once and never type them again, so there's no reason to pick something short.


**Write them down now.** Most brokers won't show you a password again after creation.


### 2.5 Restrict what each user can do (recommended)


In the authorization / ACL section, you can limit each user to specific topics. The principle: **a lamp should be able to message its peer and listen to itself — nothing more.**


| User | Permission | Topic |
|---|---|---|
| `lampA` | subscribe | `lamplink/lampA/cmd` |
| `lampA` | publish | `lamplink/lampB/cmd` |
| `lampB` | subscribe | `lamplink/lampB/cmd` |
| `lampB` | publish | `lamplink/lampA/cmd` |


Without this, a compromised lamp could subscribe to everything and read all traffic. With it, the damage is contained.


If the rules give you trouble on the first attempt, skip this step, get the project working end-to-end, then come back and add it. A rule that's subtly wrong produces `state = 5` (not authorised), which is easy to misdiagnose while you're still debugging everything else at once.


### 2.6 Download the CA certificate


Download the CA file from the Overview page. You'll get a file ending in `.crt` or `.pem`.


Open it in a **plain text editor**:


- Windows: right-click → Open with → Notepad
- macOS: open with TextEdit
- Or drag it straight into VS Code — easiest option


**Never open it in Word.** Word will add formatting, curly quotes, and line-wrapping that silently corrupts it.


You should see the `-----BEGIN CERTIFICATE-----` block described in Part 0. Leave it open; you'll copy it in Part 3.


---


## Part 3 — Configure the project


### 3.1 Open the project


Unzip `lamp-link-step1.zip`, then in VS Code: **File → Open Folder** → select the `lamp-link` folder.


**Open the folder, not individual files.** PlatformIO needs to see `platformio.ini` at the root to recognise it as a project. If the alien icon shows no project, this is why.


On first open PlatformIO will download the ESP32 platform and the PubSubClient library. First build takes a few minutes; subsequent ones are seconds.


### 3.2 Create your secrets file


In the `include/` folder you'll find `secrets.example.h`. **Copy it** and name the copy `secrets.h`, in the same folder.


Right-click `secrets.example.h` → Copy, then right-click the `include` folder → Paste, then rename.


Why two files?


- `secrets.example.h` — the **template**. Committed to git. Shows what fields exist, with fake values.
- `secrets.h` — your **real values**. Blocked by `.gitignore`, never uploaded.


This is the same pattern you already use for your ESP32 projects. It means you can publish this repo publicly and your WiFi password stays private.


### 3.3 Fill it in


Open `include/secrets.h` and edit each section.


**Identity — the only lines that differ between your two boards:**


```c
#define MY_ID    "lampA"
#define PEER_ID  "lampB"
```


Start with board A. You'll swap these before flashing board B.


**WiFi:**


```c
#define WIFI_SSID      "YourNetworkName"
#define WIFI_PASSWORD  "YourWiFiPassword"
```


Exact spelling and capitalisation. The ESP32 is **2.4 GHz only** — if your router broadcasts 2.4 and 5 GHz under one name it usually still finds the 2.4 band, but if it refuses to connect, that's your first suspect.


**Broker:**


```c
#define MQTT_HOST  "abc123.ala.ap-southeast-1.emqxsl.com"
#define MQTT_PORT  8883
#define MQTT_USER  "lampA"
#define MQTT_PASS  "the-long-random-password-you-made"
```


**Certificate** — this is the part people get wrong.


In your certificate file, select **everything**: from the first dash of `-----BEGIN CERTIFICATE-----` through the last dash of `-----END CERTIFICATE-----`. Copy it.


In `secrets.h`, replace the placeholder line so it looks like:


```c
static const char *MQTT_CA_CERT = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
-----END CERTIFICATE-----
)EOF";
```


Checklist:


- ✅ Both BEGIN and END lines present
- ✅ `R"EOF(` before and `)EOF";` after — these are C++ raw string markers, meaning "take everything between these literally." They're why you don't need `\n` on every line.
- ❌ No indentation added to the certificate lines
- ❌ No lines joined together
- ❌ No characters deleted


If your file contains **two or more** certificate blocks stacked one after another, paste all of them. That's a certificate chain and all parts are needed.


### 3.4 Save


`Ctrl+S` / `Cmd+S`. PlatformIO doesn't auto-save, and building a stale file produces baffling results.


---


## Part 4 — Wire the hardware


Identical on both boards:


```
   Button leg 1  ──────  GPIO 4
   Button leg 2  ──────  GND
```


That's genuinely all. GPIO 2 is the onboard blue LED — already connected internally.


**Why no resistor?** The code enables the ESP32's internal pull-up resistor, which holds GPIO 4 gently at 3.3 V. Pressing connects it to GND. So released = HIGH, pressed = LOW. The logic reads backwards — that's normal and expected.


**A tactile button's four legs:** the legs are wired in pairs internally. Diagonal corners are always a switched pair, so if you're unsure, use two diagonally opposite legs.


**No button yet?** Touch a jumper wire between GPIO 4 and GND. Electrically identical to a press. Perfect for a first test.


**Don't move the button to GPIO 0, 2, 12 or 15.** The ESP32 reads those pins during boot to decide how to start. A held button on one of them can stop the board booting at all — a maddening fault to diagnose.


---


## Part 5 — Flash board A


### 5.1 Plug in and find the port


Connect board A by USB. Check it was detected:


- **Windows** — Device Manager → Ports (COM & LPT) → look for `Silicon Labs CP210x (COM5)` or similar
- **macOS/Linux** — terminal: `ls /dev/tty.*` or `ls /dev/ttyUSB*`


Nothing appeared? In order: try another cable (§1.4), another USB port, then install the driver (§1.3).


### 5.2 Choose which board you're flashing


Look at the **blue status bar at the very bottom** of VS Code. There's a project environment selector showing something like `Default (lamp-link)`.


Click it and choose **`env:lampA`**.


Both environments compile the exact same `main.cpp`. The two entries exist purely so you can keep the two boards straight in your head.


### 5.3 Upload


Click the **right-arrow (→) icon** in the bottom status bar. That's Upload — it compiles and flashes in one action.


Watch the terminal panel. First build downloads dependencies and takes a few minutes. You want to end with:


```
Writing at 0x00010000... (100 %)
Hash of data verified.
Leaving... Hard resetting via RTS pin...
========== [SUCCESS] Took 23.41 seconds ==========
```


**If it hangs on `Connecting........_____`:** some boards can't auto-reset into flash mode. Hold the **BOOT** button on the board, and while holding it, click Upload. Release when you see "Writing at...". Not a defect — just a board design difference.


### 5.4 Watch it run


Click the **plug icon** 🔌 in the bottom status bar to open Serial Monitor.


Press the board's **EN** (or **RST**) button to restart it, so you see the boot messages from the beginning.


Expected:


```
=== LAMP-LINK step 1 ===
I am 'lampA', my peer is 'lampB'
[WiFi] Connecting to MyNetwork....
[WiFi] Connected. IP = 192.168.1.42
[Time] Syncing with NTP...
[Time] Clock set: Tue Sep 15 15:04:11 2026
[MQTT] Connecting as lamp-lampA-a4f3c2 ... connected
[MQTT] Subscribed to lamplink/lampA/cmd
```


The onboard LED blinks 3× on connect. **That blink is your success signal.**


Read the lines in order — they tell you exactly how far you got. WiFi line missing? WiFi problem. Time line stuck? NTP problem. Both fine but MQTT failed? Broker problem. This ordering is the whole debugging strategy.


### 5.5 Prove it works before touching board B


Don't flash the second board yet. Confirm the first one genuinely works, using your laptop as a stand-in for board B.


Your EMQX console has a built-in **WebSocket / MQTT client** tool (usually under a diagnostics or tools section). Open it, connect with your `lampB` credentials, and:


**Test receiving** — publish to topic `lamplink/lampA/cmd` with message `blink`. Board A should blink 3× instantly.


**Test sending** — subscribe to `lamplink/lampB/cmd`, then press the button on board A. The message `blink` appears in the browser.


If both work, board A is perfect. Any later problem is definitely board B's.


Prefer a desktop tool? [MQTT Explorer](http://mqtt-explorer.com/) does the same thing and is worth having — seeing messages live is what separates "A isn't sending" from "B isn't receiving" in seconds.


---


## Part 6 — Flash board B


### 6.1 Edit secrets.h — swap the identity


Open `include/secrets.h` and **swap** the two IDs:


```c
#define MY_ID    "lampB"      // was lampA
#define PEER_ID  "lampA"      // was lampB
```


Also change the credentials to board B's account:


```c
#define MQTT_USER  "lampB"
#define MQTT_PASS  "board-Bs-password"
```


WiFi, host, port and certificate stay the same for now. Save.


> Only one `secrets.h` exists at a time, so you edit it between flashes. Keep two notes — `secretsA.txt` and `secretsB.txt` — in a folder **outside** the repo, so swapping is copy-paste rather than retyping. Outside the repo, so git never sees them.


### 6.2 Flash


1. Unplug board A, plug in board B
2. Bottom status bar → switch environment to **`env:lampB`**
3. Click Upload (→)
4. Open Serial Monitor


You should see `I am 'lampB', my peer is 'lampA'` and the same connection sequence.


**If it says `lampA`,** you flashed before saving `secrets.h`, or you flashed the wrong board. Save and re-upload.


### 6.3 The moment of truth


Power both boards — they can share one computer, or one can run off a USB charger.


**Press the button on A → B blinks 3×. Press on B → A blinks 3×.**


That's the entire concept proven. Everything else you build is decoration on top of this.


### 6.4 Test it across networks


Still on the same WiFi, so this hasn't yet proven the long-distance part.


Put **one board on your phone's mobile hotspot** and leave the other on home WiFi. Different networks, different ISPs, traffic going out to the internet and back.


To do this, set `WIFI_SSID` / `WIFI_PASSWORD` to your hotspot and re-flash that board.


If that works, **different cities work identically.** There is no additional step — the broker doesn't care whether the two boards are 2 metres or 2,000 km apart. Test this before shipping a board to your friend.


---


## Part 7 — Troubleshooting


### Read the serial output in order


The log tells you exactly where you stopped:


| Last line you see | Problem is in |
|---|---|
| Nothing at all | Port, cable, or baud rate (must be 115200) |
| Stuck on `[WiFi] Connecting...` | WiFi credentials or 5 GHz network |
| Stuck on `[Time] Syncing...` | Internet access — you're on WiFi but not online |
| `[MQTT] ... FAILED, state = N` | Broker. Look up N below |


### MQTT error codes


| Code | Meaning | Most likely cause |
|---|---|---|
| **-4** | Timeout | Hostname wrong — check you removed `mqtts://` and `:8883`. Or outbound 8883 is blocked (common on office/corporate WiFi) |
| **-2** | Connect failed | **TLS.** Either the clock didn't sync, or the certificate is mangled. Start here — this is the most common error by far |
| **2** | Bad client ID | Usually two boards using the same ID |
| **4** | Bad credentials | Wrong `MQTT_USER` or `MQTT_PASS` |
| **5** | Not authorised | Credentials are right, but the ACL rules (§2.5) block this topic |


### Specific symptoms


| Symptom | Cause | Fix |
|---|---|---|
| Both boards connect, then endlessly disconnect | Same client ID — both flashed with the same `MY_ID` | Give each its own |
| A's button works, B never blinks | IDs not swapped on B | Check B's serial says `I am 'lampB'` |
| Board publishes fine, never receives | `mqtt.loop()` not running | It must be called every pass of `loop()` |
| One press fires several blinks | Contact bounce | Raise `DEBOUNCE_MS` above 50 |
| Board won't boot with button wired | Button on a strapping pin | Move it off GPIO 0 / 2 / 12 / 15 |
| Serial monitor shows garbage characters | Wrong baud rate | Must be 115200 |
| Upload stuck at `Connecting.....` | No auto-reset | Hold BOOT while uploading |
| `secrets.h: No such file` | Never created it | Copy `secrets.example.h` → `secrets.h` (§3.2) |


### The `-2` deep dive


Since it's the one you're most likely to hit:


1. **Did the clock sync?** The `[Time] Clock set:` line must show a real current date. Showing 1970 means NTP failed — usually a firewall blocking UDP port 123.
2. **Is the certificate intact?** Re-open the original file, re-copy, re-paste. Don't retype it.
3. **Are both marker lines there?** BEGIN and END.
4. **Was it opened in Word at any point?** Re-download it.
5. **Is it the right certificate?** It must be from *your* broker provider, not a random one off the internet.


---


## Part 8 — What it costs


**₹0/month, ongoing.**


EMQX Serverless's free tier includes 1 million session-minutes and 1 GB of traffic per month, with no card required.


Run the numbers for this project: two boards connected 24/7 for a full month is about 86,400 session-minutes — roughly 8% of the allowance. Each message here is 5 bytes. Even a very chatty day is kilobytes against a 1 GB budget.


You could add six more lamps and still stay inside the free tier. The one caveat: free tiers carry no uptime guarantee. For a mood lamp, that's entirely fine.


One-time hardware for the full lamp later: LED strip, a 5 V supply per lamp, buttons, and a diffuser. For step 1 you need nothing you don't already own.


---


## Part 9 — Push to GitHub safely


Before your first push, verify your secrets aren't going with it:


```bash
cd lamp-link
git init
git status
```


Look at the file list. **`include/secrets.h` must NOT appear.** If it does, `.gitignore` isn't being applied — stop and fix that before continuing.


You *should* see `include/secrets.example.h`. That's correct — it's the template with fake values.


```bash
git add .
git commit -m "Lamp-Link step 1: MQTT button-to-LED over TLS"
git remote add origin https://github.com/YOUR_USERNAME/lamp-link.git
git push -u origin main
```


**If you ever accidentally commit `secrets.h`:** removing it in a later commit is not enough — it stays in the history forever. Treat those credentials as burned. Change your WiFi password and delete/recreate the broker users. Annoying but quick, and far better than leaving live credentials in a public repo.


---


## Part 10 — What's next


You now have a working, encrypted, authenticated link between two microcontrollers in different places. That's the hard part done.


1. ~~**Blink over the internet**~~ ← complete
2. **Add the LED strip** — FastLED driving your 10 vertical segments; build three looks: busy red, free green, miss-you red/pink/white cycle
3. **Add the button grammar** — single press cycles your own patterns, double press sends "I miss you" for 60 s, long press toggles busy/free. A single press *clears* a pending remote message instead of cycling, so your first tap acts as acknowledgement
4. **Add the WiFi captive portal** — WiFiManager, so credentials stop being hardcoded and the lamp can be set up from a phone
5. **Add pairing** — your colour-code handshake, running over the broker, with the peer list saved in flash


Two rules to carry forward:


- **Status is sticky, events are not.** Publish busy/free with `retained = true`, so a rebooting lamp immediately learns the current state. Publish "I miss you" with `retained = false`, or it replays every time the lamp powers on.
- **Pairing is an edge, not a group.** Each lamp keeps its own peer list. A–B and B–C paired means pressing B reaches A and C, but pressing A reaches only B. Never relay a *received* command onward — only physical button presses should send — or your pairing model quietly turns into a mesh.




