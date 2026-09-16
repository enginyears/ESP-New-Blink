/* ============================================================================
   LAMP-LINK  ·  STEP 1  ·  "press button here, LED blinks there"
   ----------------------------------------------------------------------------
   Two ESP32 boards run THIS SAME FILE. The only difference between them is
   MY_ID / PEER_ID inside include/secrets.h.


   What happens:
     press button on A  ->  A publishes "blink" to  lamplink/<B>/cmd
                        ->  broker forwards it to B (which is subscribed)
                        ->  B blinks its LED 3 times
     press button on B  ->  same thing in reverse.


   Nothing blocks. No delay() in the main loop. That matters, because later
   you will add LED animations that must keep running while WiFi reconnects.
   ============================================================================ */


// ---------------------------------------------------------------------------
// SECTION 1 — LIBRARIES
// ---------------------------------------------------------------------------


#include <Arduino.h>          // Core Arduino functions (pinMode, millis, Serial...)
#include <WiFi.h>             // ESP32 WiFi radio. Gives us the WiFi object.
#include <WiFiClientSecure.h> // A TCP socket WRAPPED IN TLS ENCRYPTION.
                              // This is the "s" in mqtts. Use this, never plain WiFiClient.
#include <PubSubClient.h>     // The MQTT protocol itself (publish / subscribe).
                              // It does NOT do networking - it rides on top of the
                              // client object you hand it. That is why we pass
                              // WiFiClientSecure into it below.
#include "secrets.h"          // YOUR private values. Never committed to git.
                              // Quotes (not <>) = "look in my project folder first".


// ---------------------------------------------------------------------------
// SECTION 2 — HARDWARE PINS
// ---------------------------------------------------------------------------


// GPIO 2 is the small blue LED soldered onto most ESP32 DevKit boards.
// If your board has no onboard LED, wire one to any free GPIO:
//   GPIO -> 330 ohm resistor -> LED long leg ... LED short leg -> GND
#define LED_PIN 2


// GPIO 4 is a safe, "boring" pin. Avoid GPIO 0, 2, 12 and 15 for buttons:
// the ESP32 reads those at boot to decide how to start up, and a pressed
// button can stop the board from booting at all.
#define BUTTON_PIN 4


// How we wire the button: one leg to GPIO 4, the other leg to GND.
// We turn on the ESP32's INTERNAL pull-up resistor, which gently holds the
// pin at 3.3V ("HIGH"). Pressing the button connects it to GND ("LOW").
// So:  released = HIGH = 1 ,  pressed = LOW = 0. It reads backwards. That is
// normal and it means you need ZERO extra components.
#define PRESSED LOW           // Naming it makes the logic below readable.


// ---------------------------------------------------------------------------
// SECTION 3 — TIMING CONSTANTS
// ---------------------------------------------------------------------------


const unsigned long DEBOUNCE_MS      = 50;    // Ignore contact bounce (see loop notes)
const unsigned long RECONNECT_MS     = 5000;  // Wait 5s between MQTT retry attempts
const unsigned long BLINK_ON_MS      = 150;   // LED on time during a blink
const unsigned long BLINK_OFF_MS     = 150;   // LED off time during a blink
const int           BLINK_COUNT      = 3;     // How many blinks per "ping"


// ---------------------------------------------------------------------------
// SECTION 4 — TOPIC NAMES (built once at boot)
// ---------------------------------------------------------------------------


// A "topic" is just a text label on a message. Think of it as the name on a
// pigeonhole at the post office. The broker never inspects the payload; it
// only routes by topic. You invent these names - no registration needed.
//
//   I SUBSCRIBE to my own inbox        -> lamplink/<MY_ID>/cmd
//   I PUBLISH to my peer's inbox       -> lamplink/<PEER_ID>/cmd
//
// Publishing to the PEER's topic (not my own) is exactly why my button does
// not affect my own lamp - which is the behaviour you asked for.


String topicIn;    // my inbox  (I listen here)
String topicOut;   // peer inbox (I shout here)


// ---------------------------------------------------------------------------
// SECTION 5 — GLOBAL OBJECTS
// ---------------------------------------------------------------------------


WiFiClientSecure netClient;          // The encrypted pipe to the internet.
PubSubClient     mqtt(netClient);    // MQTT speaks THROUGH that encrypted pipe.


// --- button state tracking ---
int           lastRawRead     = HIGH; // Previous raw electrical reading
int           stableState     = HIGH; // The reading we actually trust
unsigned long lastChangeMs    = 0;    // When the raw reading last flipped


// --- non-blocking blink state machine ---
int           blinksRemaining = 0;    // How many on/off cycles still owed
bool          ledIsOn         = false;
unsigned long ledChangedAtMs  = 0;


// --- reconnect throttling ---
unsigned long lastReconnectAttemptMs = 0;


// ---------------------------------------------------------------------------
// SECTION 6 — HELPER: start a blink sequence
// ---------------------------------------------------------------------------
// Notice this function returns IMMEDIATELY. It does not blink. It only writes
// down "you owe 3 blinks" and lets loop() pay that debt over time. This is the
// single most important habit to build: never use delay() for output effects.


void startBlink() {
  blinksRemaining = BLINK_COUNT;
  ledIsOn         = true;
  digitalWrite(LED_PIN, HIGH);      // Turn on right now so it feels instant
  ledChangedAtMs  = millis();       // Remember when, so loop() knows when to flip
}


// ---------------------------------------------------------------------------
// SECTION 7 — HELPER: service the blink state machine
// ---------------------------------------------------------------------------
// Called on EVERY pass of loop(). Almost every call does nothing at all - it
// just checks a clock and returns. That is what "non-blocking" means.


void serviceBlink() {
  if (blinksRemaining <= 0 && !ledIsOn) return;   // Nothing owed, nothing lit


  unsigned long now = millis();
  // How long the LED should stay in its CURRENT state before flipping:
  unsigned long window = ledIsOn ? BLINK_ON_MS : BLINK_OFF_MS;


  if (now - ledChangedAtMs < window) return;      // Not time to flip yet


  if (ledIsOn) {
    digitalWrite(LED_PIN, LOW);                   // On -> Off
    ledIsOn = false;
    blinksRemaining--;                            // One full blink completed
  } else if (blinksRemaining > 0) {
    digitalWrite(LED_PIN, HIGH);                  // Off -> On (next blink)
    ledIsOn = true;
  }
  ledChangedAtMs = now;
}


// ---------------------------------------------------------------------------
// SECTION 8 — CALLBACK: a message arrived from the broker
// ---------------------------------------------------------------------------
// PubSubClient calls this function FOR US whenever a message lands on a topic
// we subscribed to. We never call it ourselves.
//
//   topic   - which pigeonhole it came from
//   payload - the raw bytes. NOT a C string: there is no \0 terminator!
//   length  - how many of those bytes are real
//
// Forgetting that payload is not null-terminated is the #1 beginner MQTT bug:
// you print it and get your message plus random garbage.


void onMessage(char *topic, byte *payload, unsigned int length) {
  String msg;
  msg.reserve(length);                       // Pre-size to avoid re-allocations
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];                 // Copy byte-by-byte, safely
  }


  Serial.printf("[MQTT] IN  %s : %s\n", topic, msg.c_str());


  // Right now we understand exactly one command. In later steps this becomes
  // a JSON parse with "type": "ping" / "status" / "pair".
  if (msg == "blink") {
    startBlink();
  }
}


// ---------------------------------------------------------------------------
// SECTION 9 — CONNECT TO WIFI
// ---------------------------------------------------------------------------
// This one IS allowed to block, because it runs once in setup() before the
// lamp has any job to do.


void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);


  WiFi.mode(WIFI_STA);                 // STA = "station" = join someone else's
                                       // network. (The other mode, AP, is the
                                       // hotspot you will use later for setup.)
  WiFi.setSleep(false);                // Disable WiFi power-save. Costs a few mA
                                       // but removes random 100-500ms lag on
                                       // incoming messages. Worth it here.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);


  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }


  Serial.printf("\n[WiFi] Connected. IP = %s\n", WiFi.localIP().toString().c_str());
}


// ---------------------------------------------------------------------------
// SECTION 10 — SET THE CLOCK  (this step is NOT optional)
// ---------------------------------------------------------------------------
// TLS certificates contain "valid from" and "valid until" dates. To verify the
// broker's certificate the ESP32 must know today's date. On power-up it thinks
// it is 1 Jan 1970, so EVERY certificate looks "not yet valid" and the
// connection fails with the infamous error  -2.
//
// So: ask an internet time server (NTP) what time it is, before touching MQTT.


void syncClock() {
  Serial.print("[Time] Syncing with NTP");


  // Args: GMT offset (sec), daylight offset (sec), then up to 3 NTP servers.
  // 19800 = 5.5 hours = IST. The offset only affects how time PRINTS -
  // TLS itself uses UTC internally, so any offset still validates fine.
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");


  time_t now = time(nullptr);
  while (now < 1700000000) {           // Any value this large means "clock is real"
    delay(300);
    Serial.print(".");
    now = time(nullptr);
  }


  Serial.printf("\n[Time] Clock set: %s", ctime(&now));
}


// ---------------------------------------------------------------------------
// SECTION 11 — CONNECT TO THE MQTT BROKER
// ---------------------------------------------------------------------------
// Returns true on success. Deliberately does NOT loop forever - loop() decides
// when to retry, so a broker outage never freezes the lamp.


bool connectMQTT() {
  // Every client on a broker needs a UNIQUE id. If both lamps used the same
  // one, the broker would kick the first off each time the second connected,
  // and you would watch them fight in an endless disconnect loop.
  String clientId = String("lamp-") + MY_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);


  Serial.printf("[MQTT] Connecting as %s ... ", clientId.c_str());


  // connect(id, user, pass) -> username/password are checked by the broker.
  // This is your authentication layer, on TOP of TLS encryption.
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println("connected");


    // Subscribe to MY inbox. QoS 1 = "broker keeps retrying until I confirm
    // I got it". QoS 0 (the default) can silently drop a message on a flaky
    // link, which for a button press is the difference between working and
    // "sometimes working".
    mqtt.subscribe(topicIn.c_str(), 1);
    Serial.printf("[MQTT] Subscribed to %s\n", topicIn.c_str());


    startBlink();                     // Visible "I am online" confirmation
    return true;
  }


  // state() returns a negative number explaining the failure. Decoded below.
  Serial.printf("FAILED, state = %d\n", mqtt.state());
  return false;
}


// ---------------------------------------------------------------------------
// SECTION 12 — SETUP  (runs once at power-on)
// ---------------------------------------------------------------------------


void setup() {
  Serial.begin(115200);
  delay(300);                          // Give the USB serial port a moment
  Serial.println("\n\n=== LAMP-LINK step 1 ===");
  Serial.printf("I am '%s', my peer is '%s'\n", MY_ID, PEER_ID);


  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);


  // INPUT_PULLUP switches on the internal resistor described in Section 2.
  pinMode(BUTTON_PIN, INPUT_PULLUP);


  // Build the two topic strings once, so we are not doing string maths
  // thousands of times per second inside loop().
  topicIn  = String("lamplink/") + MY_ID   + "/cmd";
  topicOut = String("lamplink/") + PEER_ID + "/cmd";


  connectWiFi();
  syncClock();                         // MUST come before any TLS attempt


  // Hand the broker's CA certificate to the TLS layer. This is what lets the
  // ESP32 prove it is really talking to YOUR broker and not an impostor.
  // Without this line (or with setInsecure()) anyone able to intercept your
  // traffic could pretend to be the broker.
  netClient.setCACert(MQTT_CA_CERT);


  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);         // Register Section 8 as our handler
  mqtt.setKeepAlive(30);               // Ping broker every 30s so it knows we
                                       // are alive, and so we notice a dead
                                       // link within ~45s instead of minutes.
  mqtt.setBufferSize(512);             // Default is only 256 bytes. Raise it now
                                       // so bigger JSON messages later do not
                                       // get silently dropped.
}


// ---------------------------------------------------------------------------
// SECTION 13 — LOOP  (runs thousands of times per second, forever)
// ---------------------------------------------------------------------------


void loop() {


  // --- 13a. Keep the MQTT connection healthy ------------------------------
  if (!mqtt.connected()) {
    unsigned long now = millis();
    // Only retry every RECONNECT_MS. Hammering the broker on every loop pass
    // gets your client rate-limited or temporarily banned.
    if (now - lastReconnectAttemptMs >= RECONNECT_MS) {
      lastReconnectAttemptMs = now;
      connectMQTT();
    }
  } else {
    // mqtt.loop() is the engine. It reads incoming bytes off the socket and
    // fires onMessage(). If you forget to call it, you can publish fine but
    // will NEVER receive anything. Extremely common beginner bug.
    mqtt.loop();
  }


  // --- 13b. Read the button, with debouncing ------------------------------
  // A mechanical button does not close cleanly. For a few milliseconds the
  // contacts chatter: HIGH-LOW-HIGH-LOW-HIGH... Without debouncing, one
  // physical press sends five MQTT messages.
  //
  // The rule: ignore the pin until it has held the SAME value for 50ms.


  int raw = digitalRead(BUTTON_PIN);


  if (raw != lastRawRead) {
    lastChangeMs = millis();     // The reading moved - restart the stopwatch
    lastRawRead  = raw;
  }


  if ((millis() - lastChangeMs) > DEBOUNCE_MS && raw != stableState) {
    stableState = raw;           // 50ms of calm: this reading is trustworthy


    if (stableState == PRESSED) {   // Act on the press, not the release
      Serial.printf("[BTN] Pressed -> publishing to %s\n", topicOut.c_str());


      if (mqtt.connected()) {
        // publish(topic, payload, retained)
        //   retained = false  ->  a lamp that boots later does NOT replay this.
        // For a "ping" event that is exactly right. Your busy/free STATUS in
        // step 3 will use retained = true instead, for the opposite reason.
        mqtt.publish(topicOut.c_str(), "blink", false);
      } else {
        Serial.println("[BTN] Ignored - broker not connected");
      }
    }
  }


  // --- 13c. Advance the LED animation -------------------------------------
  serviceBlink();
}


/* ============================================================================
   MQTT state() ERROR CODES  -  what PubSubClient is telling you
   ----------------------------------------------------------------------------
    -4  timeout                 broker did not answer. Wrong hostname, or your
                                network/firewall is blocking outbound 8883.
    -2  connect failed          almost always TLS. Either the clock is wrong
                                (see Section 10) or the CA cert is wrong/mangled.
                                This is the error you are most likely to hit.
    -1  disconnected            client called disconnect()
     0  connected               success
     1  bad protocol version
     2  bad client id           usually a duplicate client id
     3  server unavailable
     4  BAD CREDENTIALS         wrong MQTT username or password
     5  NOT AUTHORISED          credentials are right, but the broker's ACL
                                forbids this client from that topic
   ============================================================================ */