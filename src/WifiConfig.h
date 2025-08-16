#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

// Eenvoudige WiFi-config module met:
// - AP+Captive portal ("/" + "/scan" + "/setwifi")
// - Opslaan/lees van SSID/Pass in NVS (Preferences)
// - Helpers voor reset en factory reset
// - ElegantOTA init (v2) in begin()

class WiFiConfig {
public:
  // Moet aangeroepen worden in setup(); zorgt voor:
  // - Preferences openen
  // - Indien credentials bestaan -> verbinden als STA
  // - WebServer en ElegantOTA klaarzetten (maar alleen actief binnen AP-mode)
  void begin();

  // Draai dit in loop() om DNS/web te blijven afhandelen wanneer AP actief is
  void loop();

  // Start AP + captive portal. Wacht niet blokkerend.
  void startAP(const String& ssid_prefix = "Gridconnect-",
               const String& ap_pass = "12345678");

  // Zijn er opgeslagen credentials?
  bool hasSavedCredentials() const;

  // Probeer met opgeslagen credentials te verbinden (niet-blokkerend)
  void tryConnectSaved();

  // Huidige AP SSID/PASS
  String getApSsid() const { return _apSSID; }
  String getApPass() const { return _apPASS; }
  IPAddress apIP() const { return WiFi.softAPIP(); }

  // Wis alleen Wi-Fi credentials
  void clearCredentials();

  // Fabrieksreset voor dit device (wis onze prefs + disconnect + restart)
  void factoryReset();

  // Interne WebServer delen (alleen als je ‘m elders nodig hebt)
  WebServer& server() { return _server; }

private:
  void setupRoutes();       // registreer captive routes
  void buildApSsid();       // genereer SSID met MAC-suffix
  void loadCredentials();   // uit NVS
  void saveCredentials(const String& ssid, const String& pass);

  // route handlers
  void handleRoot();
  void handleScan();
  void handleSetWiFi();
  void handleNotFound();

private:
  WebServer _server{80};
  DNSServer _dns;
  Preferences _prefs;

  String _apSSID;
  String _apPASS = "12345678";

  String _ssidSaved;
  String _passSaved;

  bool _apActive = false;
};

extern WiFiConfig WiFiCfg;
