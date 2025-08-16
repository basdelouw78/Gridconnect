#include <Arduino.h>
#include <TFT_eSPI.h>
#include "WiFiConfig.h"
#include <qrcode.h>
#include <WiFi.h>

// ---------- TFT & Touch ----------
TFT_eSPI tft = TFT_eSPI();
static const uint16_t kCalData[5] = {231, 3663, 253, 3471, 7};

enum AppState { SPLASH, WIFI_CONFIG, MAIN_MENU, CONNECTING };
static AppState currentState = SPLASH;
static unsigned long stateStartTime = 0;

struct Btn { int16_t x, y, w, h; const char* label; uint16_t color; };
static Btn btnReset{ 20, 40, 120, 30, "Reset WiFi", TFT_RED };
static Btn btnInfo {180, 40, 120, 30, "Status", TFT_BLUE };

// Main menu
static Btn btnSettings{ 20, 100, 200, 40, "Settings", TFT_DARKGREY };
static Btn btnMonitor{ 260, 100, 200, 40, "Monitor", TFT_DARKGREEN };
static Btn btnData{ 20, 180, 200, 40, "Data", TFT_NAVY };
static Btn btnSystem{ 260, 180, 200, 40, "System", TFT_MAROON };

// Settings menu (nieuw)
static Btn btnResetWifi{ 20, 100, 200, 40, "Reset WiFi", TFT_ORANGE };
static Btn btnFactory  { 260,100, 200, 40, "Fabrieksinst.", TFT_RED };
static Btn btnBack     { 20, 180, 440, 40, "Terug", TFT_DARKGREY };

static inline void drawButton(const Btn& b) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, b.color);
  tft.drawCentreString(b.label, b.x + b.w/2, b.y + (b.h - 16)/2, 2);
}
static inline bool inButton(const Btn& b, uint16_t x, uint16_t y) {
  return (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h);
}

// ---------- WiFi / Captive helpers ----------
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
  tft.fillRect(0, 0, 480, 35, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString("GridConnect Control Panel", 240, 10, 2);

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
}

static void drawWiFiConfig() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("WiFi Setup", 10, 10, 2);

  drawButton(btnReset);
  drawButton(btnInfo);

  // QR + AP-info rechts
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
}

static void drawSettingsMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 480, 35, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString("Settings", 240, 10, 2);

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
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.setTouch((uint16_t*)kCalData);

  // WiFi-config init (laadt credentials en probeert STA-verbinding)
  WiFiCfg.begin();

  drawGridconnectLogo();
  stateStartTime = millis();
  currentState = SPLASH;
}

void loop() {
  static unsigned long lastWiFiCheck = 0;
  uint16_t tx, ty;

  // WiFi-config module laten draaien (alleen relevant als AP actief)
  WiFiCfg.loop();

  switch (currentState) {
    case SPLASH:
      if (millis() - stateStartTime > 3000) {
        if (WiFi.isConnected()) {
          currentState = MAIN_MENU;
          drawMainMenu();
        } else {
          // Indien geen verbinding, start AP + captive
          WiFiCfg.startAP();
          currentState = WIFI_CONFIG;
          drawWiFiConfig();
        }
        stateStartTime = millis();
      }
      break;

    case WIFI_CONFIG:
      // Elke 2s check: verbonden?
      if (millis() - lastWiFiCheck > 2000) {
        if (WiFi.isConnected()) {
          currentState = MAIN_MENU;
          drawMainMenu();
          stateStartTime = millis();
        }
        lastWiFiCheck = millis();
      }
      // Touch
      if (tft.getTouch(&tx, &ty)) {
        if (inButton(btnReset, tx, ty)) {
          WiFiCfg.startAP();    // herstart AP/captive (nieuwe SSID suffix blijft)
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
      // In deze refactor laat de module de verbinding afhandelen; main kijkt alleen status
      if (millis() - stateStartTime > 15000) {
        if (!WiFi.isConnected()) {
          // terug naar WiFi-config
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
          // kleine inner-loop voor settings
          while (true) {
            WiFiCfg.loop(); // captive actief? laat doorlopen
            if (tft.getTouch(&tx, &ty)) {
              if (inButton(btnBack, tx, ty)) {
                drawMainMenu();
                break;
              } else if (inButton(btnResetWifi, tx, ty)) {
                // Wis Wi-Fi en ga naar captive portal
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
              } else if (inButton(btnFactory, tx, ty)) {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.drawCentreString("Fabrieksinstellingen...", 240, 140, 4);
                delay(250);
                WiFiCfg.factoryReset();  // herstart het device
                // komt normaliter niet meer hier
              }
              delay(200);
            }
            delay(20);
          }
        } else if (inButton(btnMonitor, tx, ty)) {
          tft.fillScreen(TFT_BLACK);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawCentreString("System Monitor", 160, 100, 4);
          tft.drawCentreString("Coming Soon...", 160, 140, 2);
          delay(1500);
          drawMainMenu();
        } else if (inButton(btnData, tx, ty)) {
          tft.fillScreen(TFT_BLACK);
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.drawCentreString("Data Logging", 160, 100, 4);
          tft.drawCentreString("Coming Soon...", 160, 140, 2);
          delay(1500);
          drawMainMenu();
        } else if (inButton(btnSystem, tx, ty)) {
          tft.fillScreen(TFT_BLACK);
          tft.fillRect(0, 0, 480, 35, TFT_DARKGREY);
          tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
          tft.drawCentreString("System Information", 240, 10, 2);

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
            delay(30);
          }
          drawMainMenu();
        }
        delay(150);
      }
      break;
  }
}
