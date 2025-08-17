#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ElegantOTA.h>   // Voeg deze toe
#include <ESPmDNS.h>      // Voeg deze toe

#include "WiFiConfig.h"
#include "DeviceConfig.h"
#include <qrcode.h>

// ---------- TFT & Touch ----------
TFT_eSPI tft = TFT_eSPI();
static const uint16_t kCalData[5] = {231, 3663, 253, 3471, 7};

// ---------- App State ----------
enum AppState { SPLASH, WIFI_CONFIG, MAIN_MENU, CONNECTING, SETTINGS_MENU, Woning_INFO };
static AppState currentState = SPLASH;
static unsigned long stateStartTime = 0;

// ---------- Buttons ----------
struct Btn { int16_t x, y, w, h; const char* label; uint16_t color; };
static Btn btnReset{ 20, 40, 120, 30, "Reset WiFi", TFT_RED };
static Btn btnInfo {180, 40, 120, 30, "Status", TFT_BLUE };

static Btn btnSettings{ 20, 100, 200, 40, "Settings", TFT_DARKGREY };
static Btn btnMonitor{ 260, 100, 200, 40, "Monitor", TFT_DARKGREEN };
static Btn btnData{ 20, 180, 200, 40, "Data", TFT_NAVY };
static Btn btnSystem{ 260, 180, 200, 40, "System", TFT_MAROON };

static Btn btnResetWifi{ 20, 100, 200, 40, "Reset WiFi", TFT_ORANGE };
static Btn btnFactory  { 260,100, 200, 40, "Fabrieksinst.", TFT_RED };
static Btn btnWoning   { 20, 160, 200, 40, "Woning Setup", TFT_BLUE };
static Btn btnBack     { 260,230, 200, 40, "Terug", TFT_DARKGREY };

static Btn btnWoningReset { 20, 230, 200, 40, "Reset Woning", TFT_ORANGE };

// ---------- DB Monitor (Neon) ----------
enum class DbState { UNKNOWN, UP, DOWN };
static DbState g_dbState = DbState::UNKNOWN;
static unsigned long g_lastDbCheck = 0;
static String   g_neonHost;
static uint16_t g_neonPort = 5432;
static const uint32_t DB_CHECK_INTERVAL_MS = 10000;
static const uint32_t DB_TCP_TIMEOUT_MS    = 600;

// ---------- Server Management ----------
static bool g_serverStarted = false;
static bool g_routesRegistered = false;

void registerAllRoutes() {
  if (g_routesRegistered) return;
  
  Serial.println("Registering all server routes...");
  
  // Registreer WiFi configuratie routes
  WiFiCfg.setupRoutes();
  
  // Registreer Device configuratie routes
  DevCfg.attachRoutes(WiFiCfg.server());
  
  // Start ElegantOTA
  ElegantOTA.begin(&WiFiCfg.server(), "admin", "changeme");
  
  g_routesRegistered = true;
  Serial.println("All routes registered successfully");
}

void ensureServerRunning() {
  // Eerst altijd routes registreren
  registerAllRoutes();
  
  if (!g_serverStarted) {
    Serial.println("Starting web server...");
    
    // Start de server
    WiFiCfg.server().begin();
    
    // Setup mDNS - probeer meerdere keren
    for (int i = 0; i < 3; i++) {
      if (MDNS.begin("gridconnect")) {
        Serial.println("mDNS started: gridconnect.local");
        MDNS.addService("http", "tcp", 80);
        break;
      } else {
        Serial.printf("mDNS attempt %d failed\n", i + 1);
        delay(100);
      }
    }
    
    g_serverStarted = true;
    Serial.println("Web server started successfully");
    
    if (WiFi.isConnected()) {
      Serial.print("Server accessible at: http://");
      Serial.println(WiFi.localIP());
      Serial.println("And at: http://gridconnect.local");
      Serial.println("Routes available: /, /scan, /setwifi, /setup, /setsite, /update");
      
      // Test of de server reageert
      Serial.println("Testing server response...");
    }
  }
}

// ---------- Helpers ----------
static inline void drawButton(const Btn& b) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, b.color);
  tft.drawCentreString(b.label, b.x + b.w/2, b.y + (b.h - 16)/2, 2);
}
static inline bool inButton(const Btn& b, uint16_t x, uint16_t y) {
  return (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h);
}

// WiFi icon
static uint8_t wifiLevelFromRSSI(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}
static void drawWifiIcon(int x, int y) {
  uint16_t fg = TFT_WHITE;
  uint16_t bg = TFT_DARKGREY;
  bool connected = WiFi.isConnected();
  uint8_t lvl = connected ? wifiLevelFromRSSI(WiFi.RSSI()) : 0;
  tft.fillRect(x, y, 30, 20, bg);
  int bx = x, bw = 5, gap = 2;
  for (int i = 0; i < 4; i++) {
    int h = 5 + i * 5, by = y + 19 - h;
    uint16_t c = (i < lvl) ? fg : tft.color565(130,130,130);
    if (!connected) c = tft.color565(90,90,90);
    tft.fillRect(bx, by, bw, h, c);
    tft.drawRect(bx, by, bw, h, TFT_BLACK);
    bx += bw + gap;
  }
}

// DB icon
static void drawDbIcon(int x, int y, DbState st) {
  uint16_t bgHeader = TFT_DARKGREY;
  uint16_t color = TFT_LIGHTGREY;
  if (st == DbState::UP)   color = TFT_DARKGREEN;
  if (st == DbState::DOWN) color = TFT_RED;
  tft.fillRect(x, y, 46, 20, bgHeader);
  tft.fillRoundRect(x, y, 40, 18, 4, color);
  tft.drawRoundRect(x, y, 40, 18, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, color);
  tft.drawCentreString("DB", x + 20, y + 2, 2);
}

// Header
static void drawHeaderWithStatus(const char* title) {
  tft.fillRect(0, 0, 480, 35, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString(title, 240, 10, 2);
  drawDbIcon(480 - 46 - 32, 8, g_dbState);
  drawWifiIcon(480 - 30, 8);
}
static void refreshStatusIcons() {
  drawDbIcon(480 - 46 - 32, 8, g_dbState);
  drawWifiIcon(480 - 30, 8);
}

// Neon health (TLS connect)
static void tickDbCheck() {
  if (g_neonHost.isEmpty()) { g_dbState = DbState::UNKNOWN; return; }
  unsigned long now = millis();
  if (now - g_lastDbCheck < DB_CHECK_INTERVAL_MS) return;
  g_lastDbCheck = now;

  if (!WiFi.isConnected()) { g_dbState = DbState::DOWN; return; }

  WiFiClientSecure client;
  client.setInsecure();
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  bool ok = client.connect(g_neonHost.c_str(), g_neonPort, DB_TCP_TIMEOUT_MS);
#else
  bool ok = client.connect(g_neonHost.c_str(), g_neonPort);
#endif
  g_dbState = ok ? DbState::UP : DbState::DOWN;
  if (ok) client.stop();
}

// Schermen
static void drawGridconnectLogo() {
  tft.fillScreen(TFT_BLACK);
  for (int y = 0; y < 320; y++) {
    uint16_t color = tft.color565(0, y/4, y/3);
    tft.drawFastHLine(0, y, 480, color);
  }
  tft.fillRoundRect(120, 80, 240, 120, 15, TFT_WHITE);
  tft.fillRoundRect(125, 85, 230, 110, 12, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawCentreString("GRID", 240, 115, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("CONNECT", 240, 145, 4);
  for (int i = 0; i < 8; i++) tft.fillCircle(120 + i * 30, 175, 3, TFT_CYAN);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("Energy Management System", 240, 240, 2);
  tft.drawCentreString("v1.0", 240, 260, 1);
}

static void drawMainMenu() {
  tft.fillScreen(TFT_BLACK);
  drawHeaderWithStatus("GridConnect Control Panel");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Main Menu", 20, 55, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Select an option below", 20, 80, 2);
  drawButton(btnSettings); drawButton(btnMonitor);
  drawButton(btnData);     drawButton(btnSystem);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("GridConnect Energy Management v1.0", 240, 280, 1);
  refreshStatusIcons();
}

static void drawWiFiConfig() {
  tft.fillScreen(TFT_BLACK);
  drawHeaderWithStatus("WiFi Setup");
  drawButton(btnReset); drawButton(btnInfo);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Access Point Active:", 10, 90, 2);
  tft.drawString("SSID: " + WiFiCfg.getApSsid(), 10, 110, 2);
  tft.drawString("PASS: " + WiFiCfg.getApPass(), 10, 130, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Scan QR or connect manually", 10, 150, 1);

  // QR met URL /setup
  QRCode q; const uint8_t version = 3; uint8_t qdata[qrcode_getBufferSize(version)];
  String url = "http://" + WiFiCfg.apIP().toString() + "/setup";
  qrcode_initText(&q, qdata, version, 0, url.c_str());
  int size = 150, box = size / q.size, x0 = 300, y0 = 90;
  for (int yy = 0; yy < q.size; ++yy)
    for (int xx = 0; xx < q.size; ++xx)
      tft.fillRect(x0 + xx*box, y0 + yy*box, box, box, qrcode_getModule(&q, xx, yy) ? TFT_BLACK : TFT_WHITE);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Open woning setup: " + url, 10, 170, 1);
  refreshStatusIcons();
}

static void drawSettingsMenu() {
  tft.fillScreen(TFT_BLACK);
  drawHeaderWithStatus("Settings");

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Beheer", 20, 55, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Netwerk en apparaat", 20, 80, 2);

  drawButton(btnResetWifi);
  drawButton(btnFactory);
  drawButton(btnWoning);
  drawButton(btnBack);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String status = WiFi.isConnected() ? "Verbonden met: " + WiFi.SSID() : "Geen Wi-Fi verbinding";
  tft.drawString(status, 20, 240, 2);
  refreshStatusIcons();
}

static void drawWoningInfo() {
  tft.fillScreen(TFT_BLACK);
  drawHeaderWithStatus("Woning Setup");

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Device ID:", 20, 55, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(DevCfg.deviceId(), 140, 55, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Postcode:", 20, 85, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(DevCfg.postcode().length() ? DevCfg.postcode() : "(niet ingesteld)", 140, 85, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Huisnummer:", 20, 115, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(DevCfg.huisnummer().length() ? DevCfg.huisnummer() : "(niet ingesteld)", 140, 115, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Trafocode:", 20, 145, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(DevCfg.trafocode().length() ? DevCfg.trafocode() : "(niet ingesteld)", 140, 145, 2);

  // QR: snelle link naar /setup (AP of LAN)
  String url = WiFi.isConnected()
                ? "http://" + WiFi.localIP().toString() + "/setup"
                : "http://" + WiFiCfg.apIP().toString() + "/setup";
  QRCode q; const uint8_t ver=3; uint8_t qdata[qrcode_getBufferSize(ver)];
  qrcode_initText(&q, qdata, ver, 0, url.c_str());
  int size=120, box=size/q.size, x0=340, y0=70;
  for (int yy = 0; yy < q.size; ++yy)
    for (int xx = 0; xx < q.size; ++xx)
      tft.fillRect(x0+xx*box, y0+yy*box, box, box, qrcode_getModule(&q, xx, yy) ? TFT_BLACK : TFT_WHITE);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Open: " + url, 300, 200, 1);

  drawButton(btnWoningReset);
  drawButton(btnBack);
  refreshStatusIcons();
}

// Confirm overlay
static bool uiConfirm(const char* title, const char* line1, const char* ok="Ja", const char* cancel="Nee") {
  tft.fillScreen(TFT_BLACK);
  tft.fillRoundRect(40, 60, 400, 200, 12, TFT_DARKGREY);
  tft.drawRoundRect(40, 60, 400, 200, 12, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString(title, 240, 80, 2);
  tft.setTextColor(TFT_YELLOW, TFT_DARKGREY);
  tft.drawCentreString(line1, 240, 120, 2);

  Btn bYes{70, 190, 140, 40, ok,   TFT_DARKGREEN};
  Btn bNo {270,190, 140, 40, cancel, TFT_MAROON};
  drawButton(bYes); drawButton(bNo);

  uint16_t x,y;
  while (true) {
    if (tft.getTouch(&x,&y)) {
      if (inButton(bYes,x,y)) return true;
      if (inButton(bNo,x,y))  return false;
      delay(150);
    }
    WiFiCfg.loop(); tickDbCheck(); refreshStatusIcons();
    delay(20);
  }
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  Serial.println("=== GridConnect Starting ===");
  
  tft.init(); 
  tft.setRotation(1); 
  tft.setTouch((uint16_t*)kCalData);

  // Neon host/port uit NVS
  Preferences prefs; 
  prefs.begin("net", true);
  g_neonHost = prefs.getString("neon_host", "");
  g_neonPort = prefs.getUShort("neon_port", 5432);
  prefs.end();

  // Initialize WiFi and Device config
  WiFiCfg.begin();
  DevCfg.begin();
  
  Serial.println("WiFi and Device config initialized");
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status());
  
  // Als we al verbonden zijn, start direct de server
  if (WiFi.isConnected()) {
    Serial.println("WiFi already connected, starting server immediately");
    ensureServerRunning();
  }
  
  // Splash screen
  drawGridconnectLogo();
  stateStartTime = millis();
  currentState = SPLASH;
  
  Serial.println("Setup completed");
}

void loop() {
  static unsigned long lastWiFiCheck = 0;
  static unsigned long lastIconRefresh = 0;
  static unsigned long lastDebug = 0;
  uint16_t tx, ty;

  // BELANGRIJKSTE: WiFiCfg.loop() moet altijd worden aangeroepen
  WiFiCfg.loop();
  tickDbCheck();
  
  // Zorg dat de server altijd draait als we WiFi hebben
  if (WiFi.isConnected() && !g_serverStarted) {
    Serial.println("WiFi connected, starting server...");
    ensureServerRunning();
  }
  
  // Debug info elke 5 seconden (verhoogd van 10)
  if (millis() - lastDebug > 5000) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.isConnected());
    if (WiFi.isConnected()) {
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("Server started: ");
      Serial.println(g_serverStarted);
      Serial.print("Routes registered: ");
      Serial.println(g_routesRegistered);
      Serial.print("Free heap: ");
      Serial.println(ESP.getFreeHeap());
      
      // Test of we HTTP requests ontvangen
      static int lastClientCount = 0;
      // We kunnen geen directe client count krijgen, maar we monitoren wel heap gebruik
    }
    lastDebug = millis();
  }

  switch (currentState) {
    case SPLASH:
      if (millis() - stateStartTime > 3000) {
        if (WiFi.isConnected()) { 
          ensureServerRunning();
          currentState = MAIN_MENU; 
          drawMainMenu(); 
        }
        else { 
          WiFiCfg.startAP();
          ensureServerRunning(); // Server ook starten in AP mode
          currentState = WIFI_CONFIG; 
          drawWiFiConfig(); 
        }
        stateStartTime = millis();
      }
      break;

    case WIFI_CONFIG:
      if (millis() - lastWiFiCheck > 2000) {
        if (WiFi.isConnected()) { 
          ensureServerRunning();
          currentState = MAIN_MENU; 
          drawMainMenu(); 
          stateStartTime = millis(); 
        }
        lastWiFiCheck = millis();
      }
      if (tft.getTouch(&tx, &ty)) {
        if (inButton(btnReset, tx, ty)) { 
          WiFiCfg.startAP(); 
          ensureServerRunning();
          drawWiFiConfig(); 
          delay(250); 
        }
        else if (inButton(btnInfo, tx, ty)) {
          tft.fillRect(0, 250, 480, 70, TFT_BLACK);
          tft.setTextColor(TFT_GREEN, TFT_BLACK);
          if (WiFi.isConnected()) tft.drawString("IP: " + WiFi.localIP().toString(), 10, 260, 2);
          else tft.drawString("AP: " + WiFiCfg.apIP().toString(), 10, 260, 2);
          delay(200);
        }
      }
      break;

    case CONNECTING:
      if (millis() - stateStartTime > 15000) {
        if (!WiFi.isConnected()) { 
          WiFiCfg.startAP(); 
          ensureServerRunning();
          currentState = WIFI_CONFIG; 
          drawWiFiConfig(); 
        }
        else { 
          ensureServerRunning(); 
          currentState = MAIN_MENU; 
          drawMainMenu(); 
        }
        stateStartTime = millis();
      } else if (WiFi.isConnected()) {
        ensureServerRunning();
        currentState = MAIN_MENU; drawMainMenu(); stateStartTime = millis();
      }
      break;

    case MAIN_MENU:
      if (tft.getTouch(&tx, &ty)) {
        if (inButton(btnSettings, tx, ty)) { currentState = SETTINGS_MENU; drawSettingsMenu(); }
        else if (inButton(btnMonitor, tx, ty)) {
          tft.fillScreen(TFT_BLACK); drawHeaderWithStatus("System Monitor");
          tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawCentreString("Coming Soon...", 240, 140, 2);
          delay(1500); drawMainMenu();
        } else if (inButton(btnData, tx, ty)) {
          tft.fillScreen(TFT_BLACK); drawHeaderWithStatus("Data Logging");
          tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawCentreString("Coming Soon...", 240, 140, 2);
          delay(1500); drawMainMenu();
        } else if (inButton(btnSystem, tx, ty)) {
          tft.fillScreen(TFT_BLACK); drawHeaderWithStatus("System Information");
          tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.drawString("Network Status", 20, 60, 2);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawString("WiFi IP: " + WiFi.localIP().toString(), 20, 85, 2);
          tft.drawString("Signal: " + String(WiFi.RSSI()) + " dBm", 20, 105, 2);
          tft.drawString("MAC: " + WiFi.macAddress(), 20, 125, 2);
          tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.drawString("Hardware Info", 20, 160, 2);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawString("Free Memory: " + String(ESP.getFreeHeap()) + " bytes", 20, 185, 2);
          tft.drawString("Chip Model: " + String(ESP.getChipModel()), 20, 205, 2);
          tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.drawString("Device Info", 250, 60, 2);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawString("CPU Freq: " + String(ESP.getCpuFreqMHz()) + " MHz", 250, 85, 2);
          tft.drawString("Uptime: " + String(millis() / 1000) + " sec", 250, 105, 2);
          tft.drawString("Flash Size: " + String(ESP.getFlashChipSize()) + " bytes", 250, 125, 2);
          tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
          tft.drawCentreString("Touch anywhere to return", 240, 280, 2);
          while (!tft.getTouch(&tx, &ty)) { WiFiCfg.loop(); tickDbCheck(); if (millis()-lastIconRefresh>800){refreshStatusIcons();lastIconRefresh=millis();} delay(30); }
          drawMainMenu();
        }
        delay(150);
      } else {
        if (millis() - lastIconRefresh > 800) { refreshStatusIcons(); lastIconRefresh = millis(); }
      }
      break;

    case SETTINGS_MENU:
      while (true) {
        WiFiCfg.loop(); tickDbCheck(); if (millis()-lastIconRefresh>800){refreshStatusIcons();lastIconRefresh=millis();}
        if (tft.getTouch(&tx, &ty)) {
          if (inButton(btnBack, tx, ty)) { drawMainMenu(); currentState = MAIN_MENU; break; }
          else if (inButton(btnResetWifi, tx, ty)) {
            if (uiConfirm("Reset Wi-Fi","Opslaan wissen en AP starten?")) {
              tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_YELLOW, TFT_BLACK);
              tft.drawCentreString("Wi-Fi reset...", 240, 140, 4);
              WiFiCfg.clearCredentials(); WiFi.disconnect(true, true); delay(150);
              g_serverStarted = false; // Reset server state
              g_routesRegistered = false; // Reset routes state
              WiFiCfg.startAP(); 
              ensureServerRunning();
              currentState = WIFI_CONFIG; 
              drawWiFiConfig(); 
              break;
            } else drawSettingsMenu();
          } else if (inButton(btnFactory, tx, ty)) {
            if (uiConfirm("Fabrieksinstellingen","Alles wissen en herstarten?")) {
              tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_RED, TFT_BLACK);
              tft.drawCentreString("Fabrieksinstellingen...", 240, 140, 4); delay(250);
              WiFiCfg.factoryReset();
            } else drawSettingsMenu();
          } else if (inButton(btnWoning, tx, ty)) {
            currentState = Woning_INFO; drawWoningInfo(); break;
          }
          delay(150);
        }
        delay(20);
      }
      break;

    case Woning_INFO:
      while (true) {
        WiFiCfg.loop(); tickDbCheck(); if (millis()-lastIconRefresh>800){refreshStatusIcons(); lastIconRefresh=millis();}
        if (tft.getTouch(&tx, &ty)) {
          if (inButton(btnBack, tx, ty)) { drawSettingsMenu(); currentState = SETTINGS_MENU; break; }
          else if (inButton(btnWoningReset, tx, ty)) {
            if (uiConfirm("Reset Woning","Gegevens wissen?")) {
              DevCfg.clearSite(); drawWoningInfo();
            } else { drawWoningInfo(); }
          }
          delay(150);
        }
        delay(20);
      }
      break;
  }
}