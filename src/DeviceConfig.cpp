#include "DeviceConfig.h"
#include <WiFi.h>
#include <nvs_flash.h>
#include <nvs.h>

DeviceConfig DevCfg;

static String htmlEscape(const String& s) {
  String out; out.reserve(s.length()+8);
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}
// Lees string uit NVS zonder Preferences-logerrors (geeft "" bij ontbreken)
static String nvsGetStringOrEmpty(const char* ns, const char* key) {
  String out;
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READONLY, &h) == ESP_OK) {
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, key, nullptr, &len);
    if (err == ESP_OK && len > 0) {
      char* buf = (char*)malloc(len);
      if (buf) {
        if (nvs_get_str(h, key, buf, &len) == ESP_OK) out = String(buf);
        free(buf);
      }
    }
    nvs_close(h);
  }
  return out; // "" als key niet bestaat
}
void DeviceConfig::begin() {
  _prefs.begin("site", false);
  // deviceId genereren (eenmalig)
  _deviceId = _prefs.getString("device_id", "");
  if (_deviceId.length() == 0) {
    uint64_t mac = ESP.getEfuseMac();
    char buf[17];
    snprintf(buf, sizeof(buf), "%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)(mac & 0xFFFFFFFF));
    _deviceId = String("GC-") + buf; // bijv. GC-1A2B3C4D
    _prefs.putString("device_id", _deviceId);
  }
  load();
}

void DeviceConfig::attachRoutes(WebServer& server) {
  _srv = &server;
  _srv->on("/setup",   HTTP_GET, std::bind(&DeviceConfig::handleSetup,  this));
  _srv->on("/setsite", HTTP_GET, std::bind(&DeviceConfig::handleSetSite,this));
}

void DeviceConfig::load() {
  _postcode   = _prefs.getString("postcode", "");
  _huisnummer = _prefs.getString("huisnummer", "");
  _trafocode  = _prefs.getString("trafocode", "");
}

void DeviceConfig::save(const String& pc, const String& hn, const String& tc) {
  _prefs.putString("postcode",   pc);
  _prefs.putString("huisnummer", hn);
  _prefs.putString("trafocode",  tc);
  load();
}

void DeviceConfig::clearSite() {
  _prefs.remove("postcode");
  _prefs.remove("huisnummer");
  _prefs.remove("trafocode");
  load();
}
void DeviceConfig::handleSetup() {
  if (!_srv) return;
  
  String html = R"HTML(<!DOCTYPE html>
<html lang="nl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>GridConnect - Woning Configuratie</title>
    
    <!-- PWA Meta Tags -->
    <meta name="theme-color" content="#667eea">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <meta name="apple-mobile-web-app-title" content="GridConnect">

    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            overflow-x: hidden;
            -webkit-font-smoothing: antialiased;
            -webkit-tap-highlight-color: transparent;
            user-select: none;
        }

        .app-container {
            max-width: 100%;
            min-height: 100vh;
            background: rgba(255, 255, 255, 0.95);
            backdrop-filter: blur(10px);
            display: flex;
            flex-direction: column;
        }

        .header {
            background: linear-gradient(135deg, #2ecc71, #27ae60);
            padding: 20px;
            text-align: center;
            color: white;
        }

        .header h1 {
            font-size: 2rem;
            margin-bottom: 5px;
            font-weight: 600;
        }

        .header .subtitle {
            opacity: 0.9;
            font-size: 1rem;
        }

        .status-bar {
            background: #f8f9fa;
            padding: 15px 20px;
            border-bottom: 1px solid #e9ecef;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        .device-id {
            font-family: 'SF Mono', 'Monaco', 'Cascadia Code', monospace;
            background: #e3f2fd;
            padding: 6px 12px;
            border-radius: 8px;
            font-size: 0.85rem;
            color: #1976d2;
            font-weight: 500;
        }

        .content {
            flex: 1;
            padding: 20px;
            overflow-y: auto;
        }

        .current-config {
            background: white;
            border-radius: 16px;
            padding: 20px;
            margin-bottom: 20px;
            border-left: 4px solid #3498db;
            box-shadow: 0 4px 20px rgba(0,0,0,0.08);
        }

        .current-config h3 {
            color: #2c3e50;
            margin-bottom: 16px;
            font-size: 1.1rem;
            font-weight: 600;
        }

        .config-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
            padding: 8px 0;
        }

        .config-label {
            font-weight: 600;
            color: #34495e;
            font-size: 0.95rem;
        }

        .config-value {
            color: #7f8c8d;
            font-family: 'SF Mono', monospace;
            font-size: 0.9rem;
            text-align: right;
        }

        .empty-value {
            color: #e74c3c;
            font-style: italic;
            font-family: inherit;
        }

        .form-container {
            background: white;
            border-radius: 16px;
            padding: 25px 20px;
            margin-bottom: 20px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.08);
        }

        .form-group {
            margin-bottom: 24px;
        }

        .form-group label {
            display: block;
            margin-bottom: 8px;
            font-weight: 600;
            color: #2c3e50;
            font-size: 1rem;
        }

        .form-group input {
            width: 100%;
            padding: 16px;
            border: 2px solid #e9ecef;
            border-radius: 12px;
            font-size: 1.1rem;
            transition: all 0.3s ease;
            background: #fafafa;
            -webkit-appearance: none;
        }

        .form-group input:focus {
            outline: none;
            border-color: #3498db;
            background: white;
            box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.1);
        }

        .help-text {
            font-size: 0.85rem;
            color: #7f8c8d;
            margin-top: 6px;
        }

        .btn-container {
            display: flex;
            flex-direction: column;
            gap: 12px;
            margin-bottom: 20px;
        }

        .btn {
            width: 100%;
            padding: 18px;
            border: none;
            border-radius: 12px;
            font-size: 1.1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            position: relative;
            overflow: hidden;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
            -webkit-appearance: none;
            -webkit-tap-highlight-color: transparent;
        }

        .btn:active {
            transform: scale(0.98);
        }

        .btn-primary {
            background: linear-gradient(135deg, #3498db, #2980b9);
            color: white;
            box-shadow: 0 4px 15px rgba(52, 152, 219, 0.3);
        }

        .btn-secondary {
            background: linear-gradient(135deg, #95a5a6, #7f8c8d);
            color: white;
        }

        .btn-danger {
            background: linear-gradient(135deg, #e74c3c, #c0392b);
            color: white;
        }

        .btn:disabled {
            background: #bdc3c7 !important;
            cursor: not-allowed;
            transform: none;
            box-shadow: none;
        }

        .loading-overlay {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0,0,0,0.7);
            display: none;
            align-items: center;
            justify-content: center;
            z-index: 1000;
        }

        .loading-overlay.show {
            display: flex;
        }

        .loading-content {
            background: white;
            padding: 30px;
            border-radius: 16px;
            text-align: center;
            margin: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
        }

        .spinner {
            width: 50px;
            height: 50px;
            border: 4px solid #f3f3f3;
            border-top: 4px solid #3498db;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin: 0 auto 20px;
        }

        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }

        .success-message {
            background: linear-gradient(135deg, #2ecc71, #27ae60);
            color: white;
            padding: 16px 20px;
            border-radius: 12px;
            margin-bottom: 20px;
            text-align: center;
            font-weight: 600;
            animation: slideIn 0.3s ease-out;
            display: none;
        }

        @keyframes slideIn {
            from { transform: translateY(-20px); opacity: 0; }
            to { transform: translateY(0); opacity: 1; }
        }

        .footer {
            text-align: center;
            padding: 20px;
            color: #7f8c8d;
            font-size: 0.85rem;
            background: #f8f9fa;
            border-top: 1px solid #e9ecef;
        }

        /* Mobile optimizations */
        @media (max-width: 480px) {
            .header h1 {
                font-size: 1.8rem;
            }
            
            .content {
                padding: 15px;
            }
            
            .form-container,
            .current-config {
                padding: 20px 15px;
            }
        }
    </style>
</head>
<body>
    <div class="app-container">
        <div class="header">
            <h1>GridConnect</h1>
            <div class="subtitle">Woning Configuratie</div>
        </div>

        <div class="status-bar">
            <div class="device-id">)HTML" + htmlEscape(_deviceId) + R"HTML(</div>
        </div>

        <div class="content">
            <!-- Success Message -->
            <div class="success-message" id="successMessage"></div>

            <!-- Current Configuration -->
            <div class="current-config">
                <h3>Huidige Configuratie</h3>
                <div class="config-item">
                    <span class="config-label">Postcode:</span>
                    <span class="config-value">)HTML" + 
                    (_postcode.length() ? htmlEscape(_postcode) : "Niet ingesteld") + R"HTML(</span>
                </div>
                <div class="config-item">
                    <span class="config-label">Huisnummer:</span>
                    <span class="config-value">)HTML" + 
                    (_huisnummer.length() ? htmlEscape(_huisnummer) : "Niet ingesteld") + R"HTML(</span>
                </div>
                <div class="config-item">
                    <span class="config-label">Trafocode:</span>
                    <span class="config-value">)HTML" + 
                    (_trafocode.length() ? htmlEscape(_trafocode) : "Niet ingesteld") + R"HTML(</span>
                </div>
            </div>

            <!-- Configuration Form -->
            <div class="form-container">
                <form id="setupForm">
                    <div class="form-group">
                        <label for="postcode">Postcode</label>
                        <input type="text" id="postcode" name="postcode" placeholder="bijv. 1234AB" required value=")HTML" + htmlEscape(_postcode) + R"HTML(">
                        <div class="help-text">Nederlandse postcode (4 cijfers + 2 letters)</div>
                    </div>

                    <div class="form-group">
                        <label for="huisnummer">Huisnummer</label>
                        <input type="text" id="huisnummer" name="huisnummer" placeholder="bijv. 12" required value=")HTML" + htmlEscape(_huisnummer) + R"HTML(">
                        <div class="help-text">Huisnummer inclusief eventuele toevoeging</div>
                    </div>

                    <div class="form-group">
                        <label for="trafocode">Trafocode</label>
                        <input type="text" id="trafocode" name="trafocode" placeholder="bijv. TRAFO-01" required value=")HTML" + htmlEscape(_trafocode) + R"HTML(">
                        <div class="help-text">Code van de transformator in uw buurt</div>
                    </div>
                </form>
            </div>

            <!-- Buttons -->
            <div class="btn-container">
                <button class="btn btn-primary" id="saveBtn" onclick="saveConfiguration()">
                    Configuratie Opslaan
                </button>
                <button class="btn btn-secondary" onclick="location.reload()">
                    Vernieuw Gegevens
                </button>
                <button class="btn btn-danger" onclick="resetConfiguration()">
                    Reset Configuratie
                </button>
            </div>
        </div>

        <div class="footer">
            GridConnect Energy Management System v1.0<br>
            <small>Configureer uw woning voor optimaal energiebeheer</small>
        </div>
    </div>

    <!-- Loading Overlay -->
    <div class="loading-overlay" id="loadingOverlay">
        <div class="loading-content">
            <div class="spinner"></div>
            <p id="loadingText">Laden...</p>
        </div>
    </div>

    <script>
        function resetConfiguration() {
            if (confirm('Weet u zeker dat u alle woninggegevens wilt wissen?')) {
                showLoading('Configuratie wordt gereset...');
                
                const params = new URLSearchParams({
                    postcode: '',
                    huisnummer: '',
                    trafocode: ''
                });

                fetch('/setsite?' + params.toString())
                    .then(function(response) {
                        if (response.ok) {
                            showMessage('Configuratie is gereset!', 'warning');
                            setTimeout(function() { location.reload(); }, 1500);
                        } else {
                            throw new Error('Server error');
                        }
                    })
                    .catch(function(error) {
                        showMessage('Fout bij resetten. Probeer opnieuw.', 'error');
                    })
                    .finally(function() {
                        hideLoading();
                    });
            }
        }

        function saveConfiguration() {
            const form = document.getElementById('setupForm');
            const formData = new FormData(form);
            
            const postcode = formData.get('postcode').trim();
            const huisnummer = formData.get('huisnummer').trim();
            const trafocode = formData.get('trafocode').trim();

            if (!postcode || !huisnummer || !trafocode) {
                showMessage('Vul alle velden in', 'warning');
                return;
            }

            const postcodeRegex = /^[1-9][0-9]{3}\s?[a-zA-Z]{2}$/;
            if (!postcodeRegex.test(postcode)) {
                showMessage('Ongeldige postcode format. Gebruik bijvoorbeeld: 1234AB', 'warning');
                return;
            }

            showLoading('Configuratie wordt opgeslagen...');

            const params = new URLSearchParams({
                postcode: postcode.replace(/\s/g, '').toUpperCase(),
                huisnummer: huisnummer,
                trafocode: trafocode.toUpperCase()
            });

            fetch('/setsite?' + params.toString())
                .then(function(response) {
                    if (response.ok) {
                        showMessage('Configuratie succesvol opgeslagen!', 'success');
                        
                        if ('vibrate' in navigator) {
                            navigator.vibrate([100, 50, 100]);
                        }
                        
                        setTimeout(function() { location.reload(); }, 1500);
                    } else {
                        throw new Error('Server error');
                    }
                })
                .catch(function(error) {
                    console.error('Save error:', error);
                    showMessage('Fout bij opslaan. Probeer opnieuw.', 'error');
                })
                .finally(function() {
                    hideLoading();
                });
        }

        function showLoading(text) {
            document.getElementById('loadingText').textContent = text || 'Laden...';
            document.getElementById('loadingOverlay').classList.add('show');
            document.getElementById('saveBtn').disabled = true;
        }

        function hideLoading() {
            document.getElementById('loadingOverlay').classList.remove('show');
            document.getElementById('saveBtn').disabled = false;
        }

        function showMessage(text, type) {
            const messageEl = document.getElementById('successMessage');
            messageEl.textContent = text;
            messageEl.style.display = 'block';
            
            switch(type) {
                case 'success':
                    messageEl.style.background = 'linear-gradient(135deg, #2ecc71, #27ae60)';
                    break;
                case 'warning':
                    messageEl.style.background = 'linear-gradient(135deg, #f39c12, #e67e22)';
                    break;
                case 'error':
                    messageEl.style.background = 'linear-gradient(135deg, #e74c3c, #c0392b)';
                    break;
                default:
                    messageEl.style.background = 'linear-gradient(135deg, #3498db, #2980b9)';
                    break;
            }
            
            setTimeout(function() {
                messageEl.style.display = 'none';
            }, 3000);
        }

        // Form Enhancement
        document.getElementById('postcode').addEventListener('input', function(e) {
            var value = e.target.value.replace(/\s/g, '').toUpperCase();
            if (value.length > 4) {
                value = value.slice(0, 4) + ' ' + value.slice(4, 6);
            }
            e.target.value = value;
        });

        document.getElementById('trafocode').addEventListener('input', function(e) {
            e.target.value = e.target.value.toUpperCase();
        });

        console.log('GridConnect Setup loaded successfully!');
    </script>
</body>
</html>)HTML";

  _srv->send(200, "text/html", html);
}
void DeviceConfig::handleSetSite() {
  if (!_srv) return;
  String pc = _srv->hasArg("postcode")   ? _srv->arg("postcode")   : "";
  String hn = _srv->hasArg("huisnummer") ? _srv->arg("huisnummer") : "";
  String tc = _srv->hasArg("trafocode")  ? _srv->arg("trafocode")  : "";
  save(pc, hn, tc);

  _srv->send(200, "text/html",
    "<meta http-equiv='refresh' content='1;url=/setup'/>"
    "<div style='font-family:Arial;padding:40px;text-align:center'>"
    "<h2>✅ Opgeslagen</h2><p>Terug naar <a href='/setup'>Woning setup</a>...</p></div>");
}
