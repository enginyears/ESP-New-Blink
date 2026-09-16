/* ============================================================================
   secrets.example.h   —   TEMPLATE. This file IS committed to git.
   ----------------------------------------------------------------------------
   HOW TO USE:
     1. Copy this file and rename the copy to  secrets.h  (same folder)
     2. Fill in your real values in secrets.h
     3. NEVER commit secrets.h  —  .gitignore already blocks it


   Anyone cloning your repo copies this template and fills in their own.
   ============================================================================ */


#pragma once   // "only include this file once per compile" - prevents duplicate
               // definition errors. Modern replacement for #ifndef guards.


// ---------------------------------------------------------------------------
// 1. WHO AM I
// ---------------------------------------------------------------------------
// THE ONLY LINES THAT DIFFER BETWEEN YOUR TWO BOARDS.
//
//   Board A:   MY_ID "lampA"   PEER_ID "lampB"
//   Board B:   MY_ID "lampB"   PEER_ID "lampA"     <-- swapped
//
// Get these backwards and each lamp will blink itself instead of its peer.


#define MY_ID    "lampA"
#define PEER_ID  "lampB"


// ---------------------------------------------------------------------------
// 2. WIFI
// ---------------------------------------------------------------------------
// ESP32 radios are 2.4 GHz only. If your router broadcasts 2.4 and 5 GHz under
// one name, the ESP32 usually still finds the 2.4 band - but if it refuses to
// connect, that is the first thing to check.


#define WIFI_SSID      "YourWifiName"   // Your router's SSID (name)
#define WIFI_PASSWORD  "YourWifiPassword"   // Your router's password


// ---------------------------------------------------------------------------
// 3. MQTT BROKER
// ---------------------------------------------------------------------------
// From your EMQX Serverless deployment Overview page.
// Host looks like:  abc12345.ala.ap-southeast-1.emqxsl.com   (NO "mqtts://")


#define MQTT_HOST  "y61fb3f1.ala.asia-southeast1.emqxsl.com"
#define MQTT_PORT  8883          // 8883 = MQTT over TLS. Never use 1883.


// Create these under Access Control -> Client Authentication.
// Give EACH lamp its OWN username. If one board is ever compromised or lost,
// you revoke that one account without touching the other.
#define MQTT_USER  "lampA"
#define MQTT_PASS  "thisisalongpasswordforlampA"


// ---------------------------------------------------------------------------
// 4. CA CERTIFICATE
// ---------------------------------------------------------------------------
// Download this from your deployment's Overview page (EMQX Serverless uses
// one-way TLS and publishes its CA file). Open the .crt in a text editor and
// paste the WHOLE thing below, including both BEGIN and END lines.
//
// R"EOF( ... )EOF" is a C++ raw string literal: everything between the markers
// is taken literally, so you do not need \n escapes on every line.
//
// Formatting rules that will bite you:
//   - keep -----BEGIN CERTIFICATE----- and -----END CERTIFICATE-----
//   - do not add spaces or re-wrap the base64 lines
//   - a mangled cert shows up as MQTT state = -2, not as a clear error


static const char *MQTT_CA_CERT = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";