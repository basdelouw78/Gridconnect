#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

#include "WiFiConfig.h"
#include <qrcode.h>

// ---------- TFT & Touch ----------
TFT_eSPI tft = TFT_eSPI();
static const uint16_t kCalData[5] = {231, 3663, 253, 3471, 7};

// ---------- App State ----------
enum AppState { SPLASH, WIFI_CONFIG, MAIN_MENU, CONNECTING };
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
static Btn btnBack     { 20, 180, 440, 40, "Terug", TFT_DARKGREY };

// ---------- DB Monitor (Neon) ----------
enum class DbState { UNKNOWN, UP, DOWN };
static DbState g_dbState = DbState::UNKNOWN;
static unsigned long g_lastDbCheck = 0;

// NVS-config: "net" namespace → keys "neon_host" (string), "neon_port" (uint16)
static String   g_neonHost;
static uint16_t g_neonPort = 5432;

static const uint32_t DB_CHECK_INTERVAL_MS = 10000;
static const uint32_t DB_TCP_TIMEOUT_MS    = 600;   // ms, snappy

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

// ---- WiFi icon (rechtsboven) ----
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
  int bx = x;
  int bw = 5; int gap = 2;

  for (int i = 0; i < 4; i++) {
    int h = 5 + i * 5; // 5,10,15,20
    int by = y + 19 - h;
    uint16_t c = (i < lvl) ? fg : tft.color565(130,130,130);
    if (!connected) c = tft.color565(90,90,90);
    tft.fillRect(bx, by, bw, h, c);
    tft.drawRect(bx, by, bw, h, TFT_BLACK);
    bx += bw + gap;
  }
}

// ---- DB status pictogram ----
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

// ---- Header + icons ----
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

// ---- Neon DB health: snelle TLS connect ----
static void tickDbCheck() {
  if (g_neonHost.isEmpty()) {
    g_dbState = DbState::UNKNOWN;
    return;
  }
  unsigned long now = millis();
  if (now - g_lastDbCheck < DB_CHECK_INTERVAL_MS) return;
  g_lastDbCheck = now;

  if (!WiFi.isConnected()) {
    g_dbState = DbState::DOWN;
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // TODO: optioneel echte root CA pinnen voor strictere security

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  bool ok = client.connect(g_neonHost.c_str(), g_neonPort, DB_TCP_TIMEOUT_MS);
#else
  // Oudere cores hebben geen timeout-variant; dit kan iets langer blokkeren
  bool ok = client.connect(g_neonHost.c_str(), g_neonPort);
#endif

  if (ok) {
    g_dbState = DbState::UP;
    client.stop();
  } else {
    g_dbState = DbState::DOWN;
  }
}

// ---------- UI Schermen ----------
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

  drawButton(btnSettings);
  drawButton(btnMonitor);
  drawButton(btnData);
  drawButton(btnSystem);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("GridConnect Energy Management v1.0", 240, 280, 1);

  refreshStatusIcons();
}

static void drawWiFiConfig() {
  tft.fillScreen(TFT_BLACK);
  drawHeaderWithStatus("WiFi Setup");

  drawButton(btnReset);
  drawButton(btnInfo);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Access Point Active:", 10, 90, 2);
  tft.drawString("SSID: " + WiFiCfg.getApSsid(), 10, 110, 2);
  tft.drawString("PASS: " + WiFiCfg.getApPass(), 10, 130, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Scan QR or connect manually", 10, 150, 1);

  // QR render
  QRCode q;
  const uint8_t version = 3;
  uint8_t qdata[qrcode_getBufferSize(version)];
  String qrText = "WIFI:T:WPA;S:" + WiFiCfg.getApSsid() + ";P:" + WiFiCfg.getApPass() + ";;";
  qrcode_initText(&q, qdata, version, 0, qrText.c_str());
  int size = 150;
  int box = size / q.size;
  int x0 = 300, y0 = 90;
  for (int yy = 0; yy < q.size; ++yy)
    for (int xx = 0; xx < q.size; ++xx)
      tft.fillRect(x0 + xx*box, y0 + yy*box, box, box, qrcode_getModule(&q, xx, yy) ? TFT_BLACK : TFT_WHITE);

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
  drawButton(btnBack);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String status = WiFi.isConnected() ? "Verbonden met: " + WiFi.SSID() : "Geen Wi-Fi verbinding";
  tft.drawString(status, 20, 240, 2);

  refreshStatusIcons();
}

// Kleine confirm overlay
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
    WiFiCfg.loop();
    tickDbCheck();
    refreshStatusIcons();
    delay(20);
  }
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.setTouch((uint16_t*)kCalData);

  // DB (Neon) host/port uit NVS
  Preferences prefs;
  prefs.begin("net", true);
  g_neonHost = prefs.getString("postgresql://neondb_owner:npg_BQ6Kt9vlUirM@ep-wispy-darkness-a2omgupi-pooler.eu-central-1.aws.neon.tech/neondb?sslmode=require&channel_binding=require", ""); // bv: "your-project-name-region.neon.tech"
  g_neonPort = prefs.getUShort("neon_port", 5432);
  prefs.end();

  WiFiCfg.begin();

  drawGridconnectLogo();
  stateStartTime = millis();
  currentState = SPLASH;
}

void loop() {
  static unsigned long lastWiFiCheck = 0;
  static unsigned long lastIconRefresh = 0;
  uint16_t tx, ty;

  WiFiCfg.loop();     // captive portal / web
  tickDbCheck();      // periodiek DB check

  switch (currentState) {
    case SPLASH:
      if (millis() - stateStartTime > 3000) {
        if (WiFi.isConnected()) {
          currentState = MAIN_MENU;
          drawMainMenu();
        } else {
          WiFiCfg.startAP();
          currentState = WIFI_CONFIG;
          drawWiFiConfig();
        }
        stateStartTime = millis();
      }
      break;

    case WIFI_CONFIG:
      if (millis() - lastWiFiCheck > 2000) {
        if (WiFi.isConnected()) {
          currentState = MAIN_MENU;
          drawMainMenu();
          stateStartTime = millis();
        }
        lastWiFiCheck = millis();
      }
      if (tft.getTouch(&tx, &ty)) {
        if (inButton(btnReset, tx, ty)) {
          WiFiCfg.startAP();
          drawWiFiConfig();
          delay(250);
        } else if (inButton(btnInfo, tx, ty)) {
          tft.fillRect(0, 250, 480, 70, TFT_BLACK);
          tft.setTextColor(TFT_GREEN, TFT_BLACK);
          if (WiFi.isConnected()) {
            tft.drawString("IP: " + WiFi.localIP().toString(), 10, 260, 2);
          } else {
            tft.drawString("AP: " + WiFiCfg.apIP().toString(), 10, 260, 2);
          }
          delay(200);
        }
      }
      break;

    case CONNECTING:
      if (millis() - stateStartTime > 15000) {
        if (!WiFi.isConnected()) {
          WiFiCfg.startAP();
          currentState = WIFI_CONFIG;
          drawWiFiConfig();
        } else {
          currentState = MAIN_MENU;
          drawMainMenu();
        }
        stateStartTime = millis();
      } else if (WiFi.isConnected()) {
        currentState = MAIN_MENU;
        drawMainMenu();
        stateStartTime = millis();
      }
      break;

    case MAIN_MENU:
      if (tft.getTouch(&tx, &ty)) {
        if (inButton(btnSettings, tx, ty)) {
          drawSettingsMenu();
          while (true) {
            WiFiCfg.loop();
            tickDbCheck();
            if (millis() - lastIconRefresh > 800) {
              refreshStatusIcons();
              lastIconRefresh = millis();
            }
            if (tft.getTouch(&tx, &ty)) {
              if (inButton(btnBack, tx, ty)) {
                drawMainMenu();
                break;
              } else if (inButton(btnResetWifi, tx, ty)) {
                if (uiConfirm("Reset Wi-Fi", "Opslaan wissen en AP starten?")) {
                  tft.fillScreen(TFT_BLACK);
                  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
                  tft.drawCentreString("Wi-Fi reset...", 240, 140, 4);
                  WiFiCfg.clearCredentials();
                  WiFi.disconnect(true, true);
                  delay(150);
                  WiFiCfg.startAP();
                  currentState = WIFI_CONFIG;
                  drawWiFiConfig();
                  break;
                } else {
                  drawSettingsMenu();
                }
              } else if (inButton(btnFactory, tx, ty)) {
                if (uiConfirm("Fabrieksinstellingen", "Alles wissen en herstarten?")) {
                  tft.fillScreen(TFT_BLACK);
                  tft.setTextColor(TFT_RED, TFT_BLACK);
                  tft.drawCentreString("Fabrieksinstellingen...", 240, 140, 4);
                  delay(250);
                  WiFiCfg.factoryReset();  // herstart
                } else {
                  drawSettingsMenu();
                }
              }
              delay(150);
            }
            delay(20);
          }
        } else if (inButton(btnMonitor, tx, ty)) {
          tft.fillScreen(TFT_BLACK);
          drawHeaderWithStatus("System Monitor");
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawCentreString("Coming Soon...", 240, 140, 2);
          delay(1500);
          drawMainMenu();
        } else if (inButton(btnData, tx, ty)) {
          tft.fillScreen(TFT_BLACK);
          drawHeaderWithStatus("Data Logging");
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawCentreString("Coming Soon...", 240, 140, 2);
          delay(1500);
          drawMainMenu();
        } else if (inButton(btnSystem, tx, ty)) {
          tft.fillScreen(TFT_BLACK);
          drawHeaderWithStatus("System Information");

          tft.setTextColor(TFT_CYAN, TFT_BLACK);
          tft.drawString("Network Status", 20, 60, 2);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawString("WiFi IP: " + WiFi.localIP().toString(), 20, 85, 2);
          tft.drawString("Signal: " + String(WiFi.RSSI()) + " dBm", 20, 105, 2);
          tft.drawString("MAC: " + WiFi.macAddress(), 20, 125, 2);

          tft.setTextColor(TFT_CYAN, TFT_BLACK);
          tft.drawString("Hardware Info", 20, 160, 2);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawString("Free Memory: " + String(ESP.getFreeHeap()) + " bytes", 20, 185, 2);
          tft.drawString("Chip Model: " + String(ESP.getChipModel()), 20, 205, 2);

          tft.setTextColor(TFT_CYAN, TFT_BLACK);
          tft.drawString("Device Info", 250, 60, 2);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawString("CPU Freq: " + String(ESP.getCpuFreqMHz()) + " MHz", 250, 85, 2);
          tft.drawString("Uptime: " + String(millis() / 1000) + " sec", 250, 105, 2);
          tft.drawString("Flash Size: " + String(ESP.getFlashChipSize()) + " bytes", 250, 125, 2);

          tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
          tft.drawCentreString("Touch anywhere to return", 240, 280, 2);

          while (!tft.getTouch(&tx, &ty)) {
            WiFiCfg.loop();
            tickDbCheck();
            if (millis() - lastIconRefresh > 800) {
              refreshStatusIcons();
              lastIconRefresh = millis();
            }
            delay(30);
          }
          drawMainMenu();
        }
        delay(150);
      } else {
        if (millis() - lastIconRefresh > 800) {
          refreshStatusIcons();
          lastIconRefresh = millis();
        }
      }
      break;
  }
}
