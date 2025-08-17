#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

class WiFiConfig {
public:
  void begin();
  void loop();
  void startAP(const String& ssid_prefix = "Gridconnect-",
               const String& ap_pass = "12345678");

  bool hasSavedCredentials() const;
  void tryConnectSaved();

  void clearCredentials();
  void factoryReset();

  String getApSsid() const { return _apSSID; }
  String getApPass() const { return _apPASS; }
  IPAddress apIP() const { return WiFi.softAPIP(); }
  WebServer& server() { return _server; }
  void setupRoutes(); // MAAK PUBLIC VOOR GEBRUIK IN MAIN.CPP

private:
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