/* ============================================================================
   LAMP-LINK  ·  DIAGNOSTIC SKETCH
   ----------------------------------------------------------------------------
   This does NOT replace main.cpp permanently. It is a throwaway tool that runs
   the whole connection chain ONE STEP AT A TIME and tells you exactly which
   step fails and why.


   It tests, in order:
     TEST 1  Board info            - is the chip alive and what is it?
     TEST 2  WiFi scan             - what networks can this board ACTUALLY see?
     TEST 3  Find my network       - is my SSID there? what band/channel/signal?
     TEST 4  Connect to WiFi       - with a real timeout and a decoded reason
     TEST 5  Internet reachability - on WiFi is not the same as online
     TEST 6  NTP clock sync        - REQUIRED before any TLS can work
     TEST 7  DNS lookup of broker  - does the hostname resolve to an IP?
     TEST 8  Raw TCP to port 8883  - is the port open / not firewalled?
     TEST 9  TLS handshake         - is the CA certificate correct?
     TEST 10 MQTT connect          - are the username and password correct?
     TEST 11 Publish + subscribe   - full loopback proof through the broker


   Each test prints PASS or FAIL. On FAIL it prints the likely causes and
   STOPS, because every later test depends on the earlier ones.


   HOW TO USE
     1. Back up your real src/main.cpp (rename it main.cpp.bak)
     2. Put this file in as src/main.cpp
     3. Keep your existing include/secrets.h exactly as it is
     4. Upload, open Serial Monitor at 115200
     5. Read the first FAIL - that is your problem
   ============================================================================ */


#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>
#include "secrets.h"


// ---------------------------------------------------------------------------
// Pretty-printing helpers so the output is readable at a glance
// ---------------------------------------------------------------------------


void banner(int n, const char *title) {
  Serial.println();
  Serial.println("===========================================================");
  Serial.printf("  TEST %d  -  %s\n", n, title);
  Serial.println("===========================================================");
}


void pass(const char *msg) { Serial.printf("  [PASS] %s\n", msg); }
void info(const char *msg) { Serial.printf("         %s\n", msg); }


// Prints a FAIL block, then halts forever. Halting is deliberate: running
// later tests after a failure just produces confusing noise.
void failStop(const char *msg, const char *causes) {
  Serial.println();
  Serial.printf("  [FAIL] %s\n", msg);
  Serial.println();
  Serial.println("  LIKELY CAUSES / WHAT TO DO:");
  Serial.println(causes);
  Serial.println();
  Serial.println("  === STOPPED. Fix the above, then re-upload. ===");
  while (true) {
    delay(1000);   // Park here forever
  }
}


// ---------------------------------------------------------------------------
// Decode WiFi status codes into plain English
// ---------------------------------------------------------------------------
const char *wifiStatusText(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE (still trying)";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (network name not found on air)";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED (usually WRONG PASSWORD)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}


// Decode PubSubClient state codes
const char *mqttStateText(int s) {
  switch (s) {
    case -4: return "TIMEOUT - server did not respond in time";
    case -3: return "CONNECTION_LOST - network dropped mid-handshake";
    case -2: return "CONNECT_FAILED - almost always TLS (clock or CA cert)";
    case -1: return "DISCONNECTED - client called disconnect()";
    case  0: return "CONNECTED - success";
    case  1: return "BAD_PROTOCOL - server refused protocol version";
    case  2: return "BAD_CLIENT_ID - id rejected (often a duplicate)";
    case  3: return "UNAVAILABLE - broker up but not accepting connections";
    case  4: return "BAD_CREDENTIALS - wrong MQTT username or password";
    case  5: return "UNAUTHORIZED - credentials OK but ACL forbids this";
    default: return "UNKNOWN";
  }
}


// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------


WiFiClientSecure netClient;
PubSubClient     mqtt(netClient);


volatile bool gotLoopback = false;   // Set by the callback in TEST 11
String         topicSelf;            // We publish to ourselves to prove the round trip


void onMessage(char *topic, byte *payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("         >>> RECEIVED on '%s': '%s'\n", topic, msg.c_str());
  if (msg == "diagnostic-ping") gotLoopback = true;
}


// ===========================================================================
//  TEST 1 - BOARD INFO
// ===========================================================================
void test1_board() {
  banner(1, "BOARD INFO");


  Serial.printf("         Chip model    : %s\n", ESP.getChipModel());
  Serial.printf("         Chip revision : %d\n", ESP.getChipRevision());
  Serial.printf("         CPU cores     : %d\n", ESP.getChipCores());
  Serial.printf("         CPU freq      : %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("         Flash size    : %u KB\n", ESP.getFlashChipSize() / 1024);
  Serial.printf("         Free heap     : %u bytes\n", ESP.getFreeHeap());


  // The MAC address uniquely identifies this board. Useful if your router
  // uses MAC filtering, or if you need to find the board in the router's
  // connected-devices list.
  Serial.printf("         WiFi MAC      : %s\n", WiFi.macAddress().c_str());


  Serial.println();
  Serial.println("         --- Values loaded from secrets.h ---");
  Serial.printf("         MY_ID         : '%s'\n", MY_ID);
  Serial.printf("         PEER_ID       : '%s'\n", PEER_ID);


  // Printing inside [ ] makes stray leading/trailing SPACES visible.
  // A trailing space in an SSID or hostname is invisible but fatal.
  Serial.printf("         WIFI_SSID     : [%s]  (len %d)\n", WIFI_SSID, (int)strlen(WIFI_SSID));
  Serial.printf("         WIFI_PASSWORD : %d characters\n", (int)strlen(WIFI_PASSWORD));
  Serial.printf("         MQTT_HOST     : [%s]\n", MQTT_HOST);
  Serial.printf("         MQTT_PORT     : %d\n", MQTT_PORT);
  Serial.printf("         MQTT_USER     : [%s]\n", MQTT_USER);
  Serial.printf("         MQTT_PASS     : %d characters\n", (int)strlen(MQTT_PASS));


  // --- sanity checks on the config itself, before we use it ---


  if (strlen(WIFI_SSID) == 0)
    failStop("WIFI_SSID is empty.",
             "   - You did not fill in include/secrets.h");


  if (WIFI_SSID[strlen(WIFI_SSID) - 1] == ' ' || WIFI_SSID[0] == ' ')
    failStop("WIFI_SSID has a leading or trailing SPACE.",
             "   - Look at the [brackets] above. Retype it by hand.");


  if (strstr(MQTT_HOST, "://") != NULL)
    failStop("MQTT_HOST contains '://'",
             "   - Remove the mqtts:// prefix. Hostname ONLY.\n"
             "   - Correct: abc123.ala.ap-southeast-1.emqxsl.com");


  if (strchr(MQTT_HOST, ':') != NULL)
    failStop("MQTT_HOST contains a ':' (port).",
             "   - Remove the :8883 suffix. The port is set separately.");


  if (strstr(MQTT_CA_CERT, "BEGIN CERTIFICATE") == NULL)
    failStop("MQTT_CA_CERT does not contain a certificate.",
             "   - You left the PASTE_THE_ENTIRE_CONTENTS placeholder in.\n"
             "   - Download the CA file from your EMQX Overview page and\n"
             "     paste the whole thing, BEGIN and END lines included.");


  if (strstr(MQTT_CA_CERT, "PASTE") != NULL)
    failStop("MQTT_CA_CERT still contains the word PASTE.",
             "   - The placeholder was never replaced with a real certificate.");


  Serial.printf("         CA cert length: %d bytes  (typical: 1000-2000)\n",
                (int)strlen(MQTT_CA_CERT));


  pass("Board alive and secrets.h looks structurally valid.");
}


// ===========================================================================
//  TEST 2 - SCAN FOR NETWORKS
// ===========================================================================
// This is the single most useful test. It shows what the radio can ACTUALLY
// see. If your network is not in this list, nothing else matters.
void test2_scan() {
  banner(2, "WIFI SCAN - what can this board actually see?");


  WiFi.mode(WIFI_STA);      // Station mode = join someone else's network
  WiFi.disconnect();        // Ensure we start clean
  delay(200);


  info("Scanning (takes ~5 seconds)...");
  int n = WiFi.scanNetworks();


  if (n <= 0)
    failStop("Scan found ZERO networks.",
             "   - The board sees no WiFi at all. Either the antenna is\n"
             "     damaged, the board is in a shielded location, or the\n"
             "     board is underpowered (try a different USB cable/port).");


  Serial.printf("\n         Found %d networks:\n\n", n);
  Serial.println("          #  SSID                           Signal   Ch  Band     Security");
  Serial.println("         --- ------------------------------ -------- --- -------- --------");


  for (int i = 0; i < n; i++) {
    int ch    = WiFi.channel(i);
    int rssi  = WiFi.RSSI(i);


    // Channels 1-14 are the 2.4 GHz band. Anything higher is 5 GHz.
    // NOTE: a plain ESP32 cannot see 5 GHz at all, so in practice every
    // row here will be 2.4 GHz - which is itself the useful finding.
    const char *band = (ch <= 14) ? "2.4 GHz" : "5 GHz";


    // RSSI is negative. Closer to 0 = stronger.
    const char *quality;
    if      (rssi > -55) quality = "EXCELLENT";
    else if (rssi > -67) quality = "GOOD";
    else if (rssi > -75) quality = "WEAK";
    else                 quality = "TOO WEAK";


    Serial.printf("         %2d  %-30s %4d dBm %3d  %-8s %s  [%s]\n",
      i + 1,
      WiFi.SSID(i).c_str(),
      rssi,
      ch,
      band,
      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN    " : "secured ",
      quality);
  }


  Serial.println();
  info("NOTE: A plain ESP32 has a 2.4 GHz-only radio.");
  info("      If your network is missing from this list, it is almost");
  info("      certainly 5 GHz - or simply out of range.");


  pass("Scan completed.");
}


// ===========================================================================
//  TEST 3 - IS MY NETWORK IN THAT LIST?
// ===========================================================================
void test3_findNetwork() {
  banner(3, "FIND MY NETWORK");


  int n = WiFi.scanComplete();       // Reuse results from TEST 2
  if (n <= 0) n = WiFi.scanNetworks();


  int  foundIndex = -1;
  int  caseInsensitiveMatch = -1;


  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == String(WIFI_SSID)) {
      foundIndex = i;
      break;
    }
    // Also check for a case-only difference - a very common typo that
    // produces a confusing "network not found".
    if (WiFi.SSID(i).equalsIgnoreCase(String(WIFI_SSID))) {
      caseInsensitiveMatch = i;
    }
  }


  if (foundIndex < 0) {
    if (caseInsensitiveMatch >= 0) {
      Serial.printf("\n         You wrote : [%s]\n", WIFI_SSID);
      Serial.printf("         On air is : [%s]\n", WiFi.SSID(caseInsensitiveMatch).c_str());
      failStop("SSID found, but the CAPITALISATION does not match.",
               "   - SSIDs are case-sensitive. Copy the exact spelling shown\n"
               "     above into WIFI_SSID in secrets.h.");
    }
    Serial.printf("\n         Looking for: [%s]\n", WIFI_SSID);
    failStop("Your SSID was NOT found in the scan.",
             "   - Most likely it is a 5 GHz network. ESP32 is 2.4 GHz only.\n"
             "     Look for a 2.4 GHz network in the TEST 2 list above and\n"
             "     use that name instead.\n"
             "   - If your router uses ONE name for both bands, log into the\n"
             "     router and split them, or enable the 2.4 GHz band.\n"
             "   - Or: the board is out of range. Move it near the router.\n"
             "   - Or: a typo. Compare your name against the list above.");
  }


  int rssi = WiFi.RSSI(foundIndex);
  int ch   = WiFi.channel(foundIndex);


  Serial.printf("         Found '%s'\n", WIFI_SSID);
  Serial.printf("         Signal  : %d dBm\n", rssi);
  Serial.printf("         Channel : %d\n", ch);
  Serial.printf("         BSSID   : %s\n", WiFi.BSSIDstr(foundIndex).c_str());


  if (rssi < -75) {
    info("");
    info("WARNING: signal is very weak. Connection may fail or drop.");
    info("         Move the board closer to the router and retest.");
  }
  if (ch > 11) {
    info("");
    info("WARNING: channel is above 11. Some ESP32 regional settings only");
    info("         allow 1-11, which makes the network unjoinable.");
    info("         Try setting your router to a fixed channel of 1, 6 or 11.");
  }


  pass("Your network is visible and in range.");
}


// ===========================================================================
//  TEST 4 - CONNECT TO WIFI
// ===========================================================================
void test4_wifiConnect() {
  banner(4, "CONNECT TO WIFI");


  info("Attempting connection (20 second timeout)...");


  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                  // No power-save: avoids random lag
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);


  unsigned long start = millis();
  wl_status_t   st    = WL_IDLE_STATUS;


  // Unlike the endless "......" loop, this one GIVES UP and reports why.
  while (millis() - start < 20000) {
    st = WiFi.status();
    if (st == WL_CONNECTED) break;
    Serial.print(".");
    delay(500);
  }
  Serial.println();


  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("         Final status code: %d (%s)\n", st, wifiStatusText(st));
    failStop("Could not join the network within 20 seconds.",
             "   If status was CONNECT_FAILED:\n"
             "     - Wrong password. RETYPE it by hand in secrets.h; do not\n"
             "       paste, as a trailing space or smart-quote is invisible.\n"
             "     - Make sure it is the WiFi password, NOT the router admin\n"
             "       password. On many ISP routers both are on the sticker.\n"
             "   If status was NO_SSID_AVAIL:\n"
             "     - The network vanished between scan and connect. Retry.\n"
             "   Other things to try:\n"
             "     - Router set to WPA3-only: switch it to WPA2/WPA3 mixed.\n"
             "     - MAC filtering enabled: add the MAC from TEST 1.\n"
             "     - Device limit reached: remove an old device.\n"
             "     - TEST: use your PHONE HOTSPOT set to 2.4 GHz with a\n"
             "       simple password. If that works, the board is fine and\n"
             "       it is purely a router setting.");
  }


  Serial.printf("         IP address : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("         Gateway    : %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("         Subnet     : %s\n", WiFi.subnetMask().toString().c_str());
  Serial.printf("         DNS server : %s\n", WiFi.dnsIP().toString().c_str());
  Serial.printf("         Signal     : %d dBm\n", WiFi.RSSI());
  Serial.printf("         Took       : %lu ms\n", millis() - start);


  pass("Joined the WiFi network.");
}


// ===========================================================================
//  TEST 5 - IS THERE ACTUAL INTERNET?
// ===========================================================================
// Being on WiFi only means you reached the ROUTER. It does not mean the
// router can reach the internet. This distinction trips people up.
void test5_internet() {
  banner(5, "INTERNET REACHABILITY");


  WiFiClient probe;
  info("Opening a plain TCP connection to 1.1.1.1:80 ...");


  if (!probe.connect("1.1.1.1", 80, 8000)) {
    failStop("Cannot reach the open internet.",
             "   - You are connected to the router, but the router has no\n"
             "     working internet connection. Check on your phone.\n"
             "   - Or a captive portal is intercepting traffic (common on\n"
             "     office, hotel and guest networks). The ESP32 cannot log\n"
             "     into those. Use a different network or your hotspot.");
  }
  probe.stop();
  pass("The board can reach the public internet.");
}


// ===========================================================================
//  TEST 6 - NTP CLOCK SYNC
// ===========================================================================
// THE most important non-obvious step. TLS certificates carry validity dates.
// A board that thinks it is 1970 rejects every certificate on earth.
void test6_clock() {
  banner(6, "NTP CLOCK SYNC  (required for TLS)");


  info("Asking internet time servers for the date...");
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");


  unsigned long start = millis();
  time_t now = time(nullptr);


  // 1700000000 = Nov 2023. Any value above that means we got a real answer.
  while (now < 1700000000 && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();


  if (now < 1700000000) {
    Serial.printf("         Clock still reads: %s", ctime(&now));
    failStop("NTP sync FAILED - the clock is wrong.",
             "   - TLS CANNOT WORK without a correct clock. This would show\n"
             "     up later as the infamous MQTT state = -2.\n"
             "   - NTP uses UDP port 123. Some routers and most corporate\n"
             "     firewalls block it.\n"
             "   - Try your phone hotspot, which will not block NTP.");
  }


  Serial.printf("         Current UTC+5:30 : %s", ctime(&now));
  Serial.printf("         Unix timestamp   : %lu\n", (unsigned long)now);
  Serial.printf("         Took             : %lu ms\n", millis() - start);


  pass("Clock is set. TLS can now validate certificates.");
}


// ===========================================================================
//  TEST 7 - DNS LOOKUP
// ===========================================================================
// Turn the broker's NAME into an IP ADDRESS. Separating this from the actual
// connection tells you whether a failure is "wrong name" or "port blocked".
void test7_dns() {
  banner(7, "DNS LOOKUP OF BROKER HOSTNAME");


  IPAddress ip;
  Serial.printf("         Resolving [%s] ...\n", MQTT_HOST);


  if (!WiFi.hostByName(MQTT_HOST, ip)) {
    failStop("DNS lookup FAILED - that hostname does not resolve.",
             "   - Typo in MQTT_HOST. Copy it again from your EMQX\n"
             "     deployment Overview page.\n"
             "   - Make sure there is NO mqtts:// prefix and NO :8883 suffix.\n"
             "   - Correct form: abc123.ala.ap-southeast-1.emqxsl.com\n"
             "   - Or your deployment was deleted / never finished creating.");
  }


  Serial.printf("         Resolved to: %s\n", ip.toString().c_str());
  pass("Broker hostname resolves correctly.");
}


// ===========================================================================
//  TEST 8 - RAW TCP TO PORT 8883
// ===========================================================================
// Before worrying about certificates, check the PORT is even reachable.
// A plain TCP connect proves the path is open without involving TLS at all.
void test8_tcp() {
  banner(8, "RAW TCP CONNECTION TO PORT 8883");


  WiFiClient raw;      // Deliberately NOT secure - testing the path only
  Serial.printf("         Connecting to %s:%d (no encryption yet)...\n",
                MQTT_HOST, MQTT_PORT);


  unsigned long start = millis();
  if (!raw.connect(MQTT_HOST, MQTT_PORT, 10000)) {
    failStop("Cannot open a TCP connection to the broker port.",
             "   - Outbound port 8883 is blocked. This is COMMON on office,\n"
             "     college and corporate networks.\n"
             "     TEST: try your phone's mobile hotspot. If it works there,\n"
             "     the network is the problem, not your code.\n"
             "   - Or the broker is not running. Check the deployment status\n"
             "     in the EMQX console.");
  }


  Serial.printf("         Connected in %lu ms\n", millis() - start);
  raw.stop();
  pass("Port 8883 is open and reachable.");
}


// ===========================================================================
//  TEST 9 - TLS HANDSHAKE
// ===========================================================================
// NOW we test the certificate. Because TEST 8 already proved the port is
// open, a failure here can ONLY be the certificate or the clock.
void test9_tls() {
  banner(9, "TLS HANDSHAKE  (validates the CA certificate)");


  netClient.setCACert(MQTT_CA_CERT);
  netClient.setTimeout(15);


  info("Performing encrypted handshake...");
  info("This is where a bad CA certificate reveals itself.");


  unsigned long start = millis();
  if (!netClient.connect(MQTT_HOST, MQTT_PORT)) {
    failStop("TLS handshake FAILED.",
             "   Port 8883 is open (TEST 8 passed) and the clock is correct\n"
             "   (TEST 6 passed), so this is the CERTIFICATE. Check:\n"
             "     - Did you paste the CA cert from YOUR broker provider?\n"
             "     - Are BOTH -----BEGIN----- and -----END----- lines present?\n"
             "     - Did you accidentally add indentation to the cert lines?\n"
             "     - Did you join or re-wrap any of the base64 lines?\n"
             "     - Was the file ever opened in WORD? Word inserts curly\n"
             "       quotes and re-wraps lines, silently corrupting it.\n"
             "       Re-download and open it in Notepad or VS Code instead.\n"
             "     - If the file held MULTIPLE certificate blocks, paste ALL.");
  }


  Serial.printf("         Handshake completed in %lu ms\n", millis() - start);
  netClient.stop();
  pass("TLS works. The broker's identity was verified against your CA cert.");
}


// ===========================================================================
//  TEST 10 - MQTT CONNECT
// ===========================================================================
// Everything below MQTT is now proven, so a failure here is credentials.
void test10_mqtt() {
  banner(10, "MQTT CONNECT  (checks username and password)");


  netClient.setCACert(MQTT_CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(512);


  // Unique client id. Two clients sharing an id kick each other off forever.
  String clientId = String("diag-") + MY_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.printf("         Client ID : %s\n", clientId.c_str());
  Serial.printf("         Username  : %s\n", MQTT_USER);
  info("Connecting...");


  if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    int st = mqtt.state();
    Serial.printf("\n         state = %d  (%s)\n", st, mqttStateText(st));
    failStop("MQTT connection refused by the broker.",
             "   state = 4 (BAD CREDENTIALS):\n"
             "     - MQTT_USER or MQTT_PASS is wrong. Re-create the user in\n"
             "       EMQX under Access Control > Client Authentication and\n"
             "       set a fresh password.\n"
             "     - Remember: this is the MQTT user, NOT your EMQX website\n"
             "       login. They are different accounts.\n"
             "   state = 5 (UNAUTHORIZED):\n"
             "     - Credentials are right but an ACL rule blocks you.\n"
             "       Temporarily remove the ACL rules and retest.\n"
             "   state = 2 (BAD CLIENT ID):\n"
             "     - Another client is using the same id.\n"
             "   state = 3 (UNAVAILABLE):\n"
             "     - Broker is up but refusing. Check deployment status.");
  }


  pass("Authenticated with the broker. You are fully connected.");
}


// ===========================================================================
//  TEST 11 - PUBLISH AND SUBSCRIBE LOOPBACK
// ===========================================================================
// Publish to our OWN topic and wait to receive it back. This proves the full
// round trip through the broker, independent of the second board.
void test11_loopback() {
  banner(11, "PUBLISH / SUBSCRIBE LOOPBACK");


  topicSelf = String("lamplink/") + MY_ID + "/cmd";


  Serial.printf("         Subscribing to : %s\n", topicSelf.c_str());
  if (!mqtt.subscribe(topicSelf.c_str(), 1)) {
    failStop("Subscribe was REJECTED.",
             "   - An ACL rule forbids this client from subscribing to that\n"
             "     topic. Check your authorization rules in EMQX.");
  }


  // Give the broker a moment to register the subscription
  for (int i = 0; i < 10; i++) { mqtt.loop(); delay(50); }


  Serial.printf("         Publishing 'diagnostic-ping' to the SAME topic...\n");
  if (!mqtt.publish(topicSelf.c_str(), "diagnostic-ping", false)) {
    failStop("Publish FAILED.",
             "   - An ACL rule forbids publishing to that topic, or the\n"
             "     message exceeded the buffer size.");
  }


  info("Waiting up to 5 seconds for it to come back...");
  unsigned long start = millis();
  while (!gotLoopback && millis() - start < 5000) {
    mqtt.loop();
    delay(10);
  }


  if (!gotLoopback) {
    failStop("Published, but the message never came back.",
             "   - Publish succeeded and subscribe succeeded, so this is\n"
             "     almost certainly an ACL rule allowing publish but\n"
             "     denying subscribe on that topic.\n"
             "   - Temporarily disable ALL ACL rules and retest.");
  }


  Serial.printf("         Round trip took %lu ms\n", millis() - start);
  pass("Full round trip through the broker works.");
}


// ===========================================================================
//  SETUP - run every test in order
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(1500);           // Let the USB serial port settle so we see everything


  Serial.println();
  Serial.println();
  Serial.println("###########################################################");
  Serial.println("#                                                         #");
  Serial.println("#        LAMP-LINK CONNECTION DIAGNOSTIC                  #");
  Serial.println("#                                                         #");
  Serial.println("#  Runs every step of the chain and stops at the first    #");
  Serial.println("#  failure, with an explanation of what to fix.           #");
  Serial.println("#                                                         #");
  Serial.println("###########################################################");


  test1_board();
  test2_scan();
  test3_findNetwork();
  test4_wifiConnect();
  test5_internet();
  test6_clock();
  test7_dns();
  test8_tcp();
  test9_tls();
  test10_mqtt();
  test11_loopback();


  Serial.println();
  Serial.println("###########################################################");
  Serial.println("#                                                         #");
  Serial.println("#   ALL TESTS PASSED                                      #");
  Serial.println("#                                                         #");
  Serial.println("#   WiFi, internet, clock, DNS, TCP, TLS, MQTT auth and   #");
  Serial.println("#   message round-trip are all working on this board.     #");
  Serial.println("#                                                         #");
  Serial.println("#   Restore your real main.cpp and flash it.              #");
  Serial.println("#                                                         #");
  Serial.println("###########################################################");
  Serial.println();
  Serial.printf("This board is listening on : lamplink/%s/cmd\n", MY_ID);
  Serial.printf("It will publish to         : lamplink/%s/cmd\n", PEER_ID);
  Serial.println();
  Serial.println("Now staying connected and printing any incoming message.");
  Serial.println("Send 'blink' to this board's topic from the EMQX web client");
  Serial.println("and you should see it appear below.");
  Serial.println();
}


// ===========================================================================
//  LOOP - stay connected so you can test from the EMQX web client
// ===========================================================================
void loop() {
  if (!mqtt.connected()) {
    Serial.printf("[!] MQTT dropped (state %d: %s). Reconnecting...\n",
                  mqtt.state(), mqttStateText(mqtt.state()));
    String clientId = String("diag-") + MY_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      mqtt.subscribe(topicSelf.c_str(), 1);
      Serial.println("[+] Reconnected.");
    } else {
      delay(5000);
    }
  }
  mqtt.loop();


  // Heartbeat every 30s so you know the board is still alive and listening
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 30000) {
    lastBeat = millis();
    Serial.printf("[.] Alive. WiFi %d dBm, MQTT %s\n",
                  WiFi.RSSI(), mqtt.connected() ? "connected" : "DOWN");
  }
}



