#include "WiFiConfig.h"
#include <ElegantOTA.h>   // v2
#include <ESPmDNS.h>

// ---------- HTML UI ----------
static String makeRootHtml() {
  String html =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>GridConnect WiFi Setup</title>"
    "<style>body{font-family:Arial;margin:40px;background:#f0f0f0}"
    ".container{max-width:420px;margin:0 auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
    "h1{color:#2c3e50;text-align:center} input,select,button{width:100%;padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:5px}"
    ".btn{background:#3498db;color:white;border:none;cursor:pointer} .btn:hover{background:#2980b9}"
    ".row{display:flex;gap:10px} .row>*{flex:1}"
    ".muted{color:#888;font-size:12px;text-align:center;margin-top:6px}"
    "a{color:#3498db;text-decoration:none}"
    "</style></head><body>"
    "<div class='container'>"
    "<h1>🔌 GridConnect</h1><h2>WiFi Configuration</h2>"
    "<div class='row'>"
    "<button id='scan' class='btn' onclick='doScan()'>🔍 Scan Wi-Fi</button>"
    "<select id='nets'><option value='' disabled selected>Select network...</option></select>"
    "</div>"
    "<form action='/setwifi' method='get' onsubmit='applyPick()'>"
    "Network Name (SSID):<br><input id='ssid' name='ssid' required placeholder='Choose above or type manually'><br>"
    "Password:<br><input id='pass' name='pass' type='password' placeholder='Leave empty for open networks'><br>"
    "<input type='submit' value='Connect to Network' class='btn'>"
    "</form>"
    "<p class='muted'>Tip: kies via de dropdown; SSID wordt automatisch ingevuld.</p>"
    "<p class='muted'><a href='/setup'>🏠 Woning setup</a> • <a href='/update'>⚙️ Firmware Update</a></p>"
    "</div>"
    "<script>"
    "const netsEl=document.getElementById('nets');"
    "const ssidEl=document.getElementById('ssid');"
    "async function doScan(){"
      "netsEl.innerHTML='<option>Scanning...</option>';"
      "try{"
        "const r=await fetch('/scan');"
        "const arr=await r.json();"
        "if(!Array.isArray(arr)||arr.length===0){netsEl.innerHTML='<option>No networks found</option>';return}"
        "arr.sort((a,b)=>b.rssi-a.rssi);"
        "netsEl.innerHTML='<option value=\"\" disabled selected>Select network...</option>' + "
          "arr.map(n=>`<option value=\"${n.ssid.replace(/\"/g,'&quot;')}\">${n.ssid} (${n.enc}, ${n.rssi} dBm)</option>`).join('');"
      "}catch(e){netsEl.innerHTML='<option>Scan failed</option>'}"
    "}"
    "netsEl.addEventListener('change',()=>{ if(netsEl.value) ssidEl.value=netsEl.value; });"
    "function applyPick(){ if(netsEl.value && !ssidEl.value) ssidEl.value=netsEl.value; }"
    "</script>"
    "</body></html>";
  return html;
}

static const char* encToText(
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  wifi_auth_mode_t enc
#else
  uint8_t enc
#endif
) {
#if defined(WIFI_AUTH_OPEN)
  if (enc == WIFI_AUTH_OPEN) return "OPEN";
#endif
#if defined(WIFI_AUTH_WEP)
  if (enc == WIFI_AUTH_WEP) return "WEP";
#endif
#if defined(WIFI_AUTH_WPA_PSK)
  if (enc == WIFI_AUTH_WPA_PSK) return "WPA";
#endif
#if defined(WIFI_AUTH_WPA2_PSK)
  if (enc == WIFI_AUTH_WPA2_PSK) return "WPA2";
#endif
#if defined(WIFI_AUTH_WPA_WPA2_PSK)
  if (enc == WIFI_AUTH_WPA_WPA2_PSK) return "WPA/WPA2";
#endif
#if defined(WIFI_AUTH_WPA3_PSK)
  if (enc == WIFI_AUTH_WPA3_PSK) return "WPA3";
#endif
  return "SECURED";
}

WiFiConfig WiFiCfg;

void WiFiConfig::begin() {
  _prefs.begin("net", false);
  loadCredentials();

  if (hasSavedCredentials()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssidSaved.c_str(), _passSaved.c_str());
  } else {
    WiFi.mode(WIFI_MODE_NULL);
  }
}

void WiFiConfig::loop() {
  static unsigned long lastServerDebug = 0;
  
  if (_apActive) {
    _dns.processNextRequest();
  }
  
  // Server requests afhandelen - DIT IS CRUCIAAL
  _server.handleClient();
  
  // Debug elke 15 seconden
  if (millis() - lastServerDebug > 15000) {
    Serial.println("WiFiConfig::loop() - Server handling clients");
    Serial.print("Server active on port 80, AP active: ");
    Serial.println(_apActive);
    lastServerDebug = millis();
  }
}

void WiFiConfig::startAP(const String& prefix, const String& ap_pass) {
  _apPASS = ap_pass;
  buildApSsid();
  if (prefix.length()) {
    _apSSID = prefix + _apSSID.substring(_apSSID.length() - 5);
  }

  WiFi.disconnect(true, true);
  delay(50);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(_apSSID.c_str(), _apPASS.c_str());

  _dns.start(53, "*", WiFi.softAPIP());
  _apActive = true;
  
  Serial.println("AP started: " + _apSSID);
  Serial.println("AP IP: " + WiFi.softAPIP().toString());
}

bool WiFiConfig::hasSavedCredentials() const {
  return _ssidSaved.length() > 0;
}

void WiFiConfig::tryConnectSaved() {
  if (!hasSavedCredentials()) return;
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(_ssidSaved.c_str(), _passSaved.c_str());
  }
}

void WiFiConfig::clearCredentials() {
  _prefs.remove("ssid");
  _prefs.remove("pass");
  _ssidSaved = "";
  _passSaved = "";
}

void WiFiConfig::factoryReset() {
  _prefs.clear();
  _ssidSaved = "";
  _passSaved = "";
  WiFi.disconnect(true, true);
  delay(100);
  ESP.restart();
}

void WiFiConfig::buildApSsid() {
  String mac = WiFi.macAddress();
  String last5 = mac.substring(mac.length() - 5);
  _apSSID = "Gridconnect-" + last5;
}

void WiFiConfig::loadCredentials() {
  _ssidSaved = _prefs.getString("ssid", "");
  _passSaved = _prefs.getString("pass", "");
}

void WiFiConfig::saveCredentials(const String& ssid, const String& pass) {
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", pass);
  _ssidSaved = ssid;
  _passSaved = pass;
}

void WiFiConfig::setupRoutes() {
  _server.on("/",        HTTP_GET, std::bind(&WiFiConfig::handleRoot,    this));
  _server.on("/scan",    HTTP_GET, std::bind(&WiFiConfig::handleScan,    this));
  _server.on("/setwifi", HTTP_GET, std::bind(&WiFiConfig::handleSetWiFi, this));
  _server.onNotFound(std::bind(&WiFiConfig::handleNotFound, this));
}

void WiFiConfig::handleRoot() {
  Serial.println("HTTP Request received: /");
  _server.send(200, "text/html", makeRootHtml());
  Serial.println("HTTP Response sent for /");
}

void WiFiConfig::handleScan() {
  Serial.println("HTTP Request received: /scan");
  int n = WiFi.scanNetworks(false, true);
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    wifi_auth_mode_t enc = WiFi.encryptionType(i);
#else
    uint8_t enc = WiFi.encryptionType(i);
#endif
    json += "{";
    json += "\"ssid\":\"" + String(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"channel\":" + String(WiFi.channel(i)) + ",";
    json += "\"enc\":\"" + String(encToText(enc)) + "\"";
    json += "}";
  }
  json += "]";
  _server.send(200, "application/json", json);
  WiFi.scanDelete();
  Serial.println("HTTP Response sent for /scan");
}

void WiFiConfig::handleSetWiFi() {
  Serial.println("HTTP Request received: /setwifi");
  if (!_server.hasArg("ssid")) {
    _server.send(400, "text/plain", "Missing SSID");
    return;
  }
  const String ssid = _server.arg("ssid");
  const String pass = _server.hasArg("pass") ? _server.arg("pass") : "";

  saveCredentials(ssid, pass);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  _server.send(200, "text/html",
    "<div style='text-align:center;padding:50px;font-family:Arial'>"
    "<h1>🔄 Connecting...</h1><p>Attempting to connect to <b>" + ssid + "</b></p>"
    "<p>Check the device screen for connection status.</p>"
    "<p><a href='/setup'>Ga naar woning setup</a></p>"
    "</div>");
  Serial.println("HTTP Response sent for /setwifi");
}

void WiFiConfig::handleNotFound() {
  Serial.println("HTTP 404 - redirecting to /");
  _server.sendHeader("Location", "/");
  _server.send(302, "text/plain", "");
}