#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>

class DeviceConfig {
public:
  void begin();
  void attachRoutes(WebServer& server);  // registreert /setup en /setsite

  // identifiers
  String deviceId()   const { return _deviceId; }

  // woning-velden
  String postcode()   const { return _postcode; }
  String huisnummer() const { return _huisnummer; }
  String trafocode()  const { return _trafocode; }

  // reset woning-informatie
  void clearSite();

private:
  void load();
  void save(const String& pc, const String& hn, const String& tc);

  // routes
  void handleSetup();
  void handleSetSite();

private:
  Preferences _prefs;   // namespace "site"

  String _deviceId;
  String _postcode;
  String _huisnummer;
  String _trafocode;

  WebServer* _srv = nullptr;
};

extern DeviceConfig DevCfg;
