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
  void begin();                   // init prefs + evt STA connect
  void loop();                    // afhandelen DNS/Web indien AP actief
  void startAP(const String& ssid_prefix = "Gridconnect-",
               const String& ap_pass = "12345678");

  bool hasSavedCredentials() const;
  void tryConnectSaved();

  // Credentials / reset
  void clearCredentials();
  void factoryReset();

  // Info voor UI
  String getApSsid() const { return _apSSID; }
  String getApPass() const { return _apPASS; }
  IPAddress apIP() const { return WiFi.softAPIP(); }
  WebServer& server() { return _server; }

private:
  void setupRoutes();
  void buildApSsid();
  void loadCredentials();
  void saveCredentials(const String& ssid, const String& pass);

  void handleRoot();
  void handleScan();
  void handleSetWiFi();
  void handleNotFound();

private:
  WebServer   _server{80};
  DNSServer   _dns;
  Preferences _prefs;

  String _apSSID;
  String _apPASS = "12345678";

  String _ssidSaved;
  String _passSaved;

  bool _apActive = false;
};

extern WiFiConfig WiFiCfg;
