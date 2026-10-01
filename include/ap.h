#ifndef AP_H
#define AP_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include "config.h"
#include "config_manager.h"
#include "logger.h"

// ===== Konfiguracja AP =====
#define AP_SSID         "Radio_ESP32"
#define AP_PASSWORD     ""
#define AP_CHANNEL      1
#define AP_MAX_CLIENTS  4
#define AP_IP           IPAddress(192, 168, 4, 1)
#define AP_GATEWAY      IPAddress(192, 168, 4, 1)
#define AP_SUBNET       IPAddress(255, 255, 255, 0)

// ===== Plik konfiguracyjny =====
#define CONFIG_FILE     "/config.txt"

// ===== Czasy przytrzymania ENC2 (ms) =====
#define AP_HOLD_ENTER_MS  10000  // 10s wejscie w tryb AP
#define AP_HOLD_EXIT_MS   5000   // 5s wyjscie z trybu AP

// ===== Zmienne stanu AP =====
extern bool apActive;
extern IPAddress apIP;
extern WebServer* apServer;

// ===== Forward declarations =====
void startAP();
void stopAP();
void drawAPScreen(Arduino_Canvas* canvas);
bool loadConfigFromFile();
void saveConfigToFile();
void handleAPLoop();

// ===== Implementacja =====

bool apActive = false;
IPAddress apIP(192, 168, 4, 1);
WebServer* apServer = nullptr;

// ===== HTML strony konfiguracyjnej (w PROGMEM) =====
const char apIndexHTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio ESP32 - Konfiguracja</title>
<style>
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px;max-width:800px;margin:auto}
  h1{color:#e94560;text-align:center}
  h2{color:#0f3460;border-bottom:2px solid #e94560;padding-bottom:5px}
  .card{background:#16213e;border-radius:10px;padding:20px;margin:20px 0}
  label{display:block;margin:10px 0 5px;color:#aaa}
  input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid #333;border-radius:5px;background:#1a1a2e;color:#eee;box-sizing:border-box}
  input[type=number]{width:80px;padding:10px;border:1px solid #333;border-radius:5px;background:#1a1a2e;color:#eee}
  .station{border:1px solid #333;border-radius:5px;padding:15px;margin:10px 0;background:#0f3460}
  .station label{color:#e94560}
  button{padding:12px 24px;border:none;border-radius:5px;cursor:pointer;font-size:16px}
  .btn-save{background:#e94560;color:#fff;width:100%;margin:20px 0}
  .btn-add{background:#0f3460;color:#eee;margin:5px 0}
  .btn-del{background:#533;color:#e45;margin-left:10px;padding:5px 10px}
  .success{color:#4caf50;text-align:center;padding:10px}
  .note{color:#888;font-size:12px;text-align:center;margin-top:10px}
</style>
</head>
<body>
<h1>Radio ESP32</h1>
<div id="status"></div>

<div class="card">
<h2>WiFi</h2>
<label>SSID:</label><input type="text" id="wifi_ssid" placeholder="Nazwa sieci WiFi">
<label>Hasło:</label><input type="password" id="wifi_pass" placeholder="Hasło WiFi">
</div>

<div class="card">
<h2>Stacje radiowe</h2>
<div id="stations"></div>
<button class="btn-add" onclick="addStation()">+ Dodaj stację</button>
</div>

<button class="btn-save" onclick="saveConfig()">Zapisz i uruchom ponownie</button>
<div class="note">Po zapisie ESP32 zrestartuje się z nową konfiguracją.</div>

<script>
let stationCount = 0;

function loadConfig() {
  fetch('/api/config').then(r=>r.json()).then(cfg => {
    document.getElementById('wifi_ssid').value = cfg.wifi_ssid || '';
    document.getElementById('wifi_pass').value = cfg.wifi_pass || '';
    if(cfg.stations) {
      cfg.stations.forEach(s => addStation(s.name, s.url, s.label));
    }
  });
}

function addStation(name, url, label) {
  const div = document.getElementById('stations');
  const idx = stationCount++;
  const s = document.createElement('div');
  s.className = 'station';
  s.id = 'station_' + idx;
  s.innerHTML = `
    <label>Nazwa stacji ${idx+1}:</label>
    <input type="text" id="s_name_${idx}" value="${name||''}" placeholder="Nazwa">
    <label>URL strumienia:</label>
    <input type="text" id="s_url_${idx}" value="${url||''}" placeholder="http://...">
    <label>Etykieta (np. 192 MP3):</label>
    <input type="text" id="s_label_${idx}" value="${label||''}" placeholder="128 MP3">
    <button class="btn-del" onclick="removeStation(${idx})">Usuń</button>
  `;
  div.appendChild(s);
}

function removeStation(idx) {
  const el = document.getElementById('station_' + idx);
  if(el) el.remove();
}

function saveConfig() {
  const stations = [];
  for(let i = 0; i < stationCount; i++) {
    const el = document.getElementById('station_' + i);
    if(!el) continue;
    const name = document.getElementById('s_name_' + i).value.trim();
    const url = document.getElementById('s_url_' + i).value.trim();
    const label = document.getElementById('s_label_' + i).value.trim();
    if(name && url) stations.push({name, url, label});
  }
  
  const data = {
    wifi_ssid: document.getElementById('wifi_ssid').value.trim(),
    wifi_pass: document.getElementById('wifi_pass').value.trim(),
    stations: stations
  };
  
  document.getElementById('status').innerHTML = '<div class="success">Zapisywanie...</div>';
  
  fetch('/api/config', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify(data)
  }).then(r => r.text()).then(msg => {
    document.getElementById('status').innerHTML = '<div class="success">' + msg + '</div>';
    setTimeout(() => { fetch('/api/restart', {method:'POST'}); }, 2000);
  });
}

loadConfig();
</script>
</body>
</html>
)rawliteral";

// ===== Rysowanie ekranu AP =====
inline void drawAPScreen(Arduino_Canvas* canvas) {
    canvas->fillScreen(COL_BG);
    canvas->setTextColor(COL_TEXT);
    
    // Nagłówek
    canvas->setTextSize(2);
    canvas->setCursor(40, 30);
    canvas->print("TRYB AP");
    
    // IP serwera
    canvas->setTextSize(1);
    canvas->setCursor(20, 70);
    canvas->print("Serwer konfiguracyjny:");
    
    canvas->setTextSize(3);
    canvas->setCursor(20, 100);
    String ipStr = apIP.toString();
    canvas->print(ipStr.c_str());
    
    // Instrukcja
    canvas->setTextSize(1);
    canvas->setTextColor(COL_TEXT_SEC);
    canvas->setCursor(20, 150);
    canvas->print("Polacz sie z WiFi: ");
    canvas->setTextColor(COL_YELLOW);
    canvas->print(AP_SSID);
    
    canvas->setTextColor(COL_TEXT_SEC);
    canvas->setCursor(20, 170);
    canvas->print("Otworz przegladarke i wpisz IP");
    
    // Przytrzymaj ENC2 aby wyjść
    canvas->setTextColor(COL_RED);
    canvas->setCursor(20, 200);
    canvas->print("ENC2 5s = wyjscie");
}

// ===== Inicjalizacja LittleFS (raz) =====
static bool littlefsMounted = false;
inline bool initLittleFS() {
    if (littlefsMounted) {
        info("AP", "LittleFS already mounted");
        return true;
    }
    info("AP", "Mounting LittleFS...");
    if (!LittleFS.begin(true)) {  // true = auto-format
        error("AP", "LittleFS mount FAILED even with auto-format");
        return false;
    }
    littlefsMounted = true;
    info("AP", "LittleFS mounted OK");
    return true;
}

// ===== Funkcje pomocnicze do czytania/zapisu pliku =====
inline String readConfigFile() {
    if (!initLittleFS()) {
        error("AP", "readConfigFile: LittleFS not available");
        return "";
    }
    
    if (!LittleFS.exists(CONFIG_FILE)) {
        info("AP", "readConfigFile: " + String(CONFIG_FILE) + " does NOT exist");
        return "";
    }
    
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) {
        error("AP", "readConfigFile: open for READ failed");
        return "";
    }
    
    size_t fsize = f.size();
    String content = f.readString();
    f.close();
    
    info("AP", "readConfigFile: read " + String(content.length()) + "/" + String(fsize) + " bytes from " + String(CONFIG_FILE));
    return content;
}

inline bool writeConfigFile(const String& content) {
    if (!initLittleFS()) {
        error("AP", "writeConfigFile: LittleFS not available");
        return false;
    }
    
    // Najpierw usuń stary plik (jeśli istnieje)
    if (LittleFS.exists(CONFIG_FILE)) {
        LittleFS.remove(CONFIG_FILE);
        info("AP", "writeConfigFile: removed old " + String(CONFIG_FILE));
    }
    
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) {
        error("AP", "writeConfigFile: open for WRITE failed");
        return false;
    }
    
    size_t written = f.print(content);
    f.flush();  // Wymuś zapis
    f.close();
    
    // Weryfikacja
    bool exists = LittleFS.exists(CONFIG_FILE);
    size_t fsize = 0;
    if (exists) {
        File vf = LittleFS.open(CONFIG_FILE, "r");
        if (vf) {
            fsize = vf.size();
            vf.close();
        }
    }
    
    info("AP", "writeConfigFile: wrote " + String(written) + "/" + String(content.length()) + " bytes, exists=" + String(exists) + ", size=" + String(fsize));
    return written > 0 && exists;
}

// ===== Ładowanie konfiguracji z pliku =====
inline bool loadConfigFromFile() {
    String content = readConfigFile();
    if (content.length() == 0) {
        info("AP", "No config file found, using defaults");
        return false;
    }
    
    // Parsuj linie key=value
    int lineStart = 0;
    while (lineStart < (int)content.length()) {
        int lineEnd = content.indexOf('\n', lineStart);
        if (lineEnd == -1) lineEnd = content.length();
        String line = content.substring(lineStart, lineEnd);
        line.trim();
        
        int eq = line.indexOf('=');
        if (eq > 0) {
            String key = line.substring(0, eq);
            String val = line.substring(eq + 1);
            key.trim();
            val.trim();
            
            if (key == "WIFI_SSID" && val.length() > 0) {
                strncpy(cfg_wifi_ssid, val.c_str(), 63);
                cfg_wifi_ssid[63] = '\0';
            } else if (key == "WIFI_PASS") {
                strncpy(cfg_wifi_pass, val.c_str(), 63);
                cfg_wifi_pass[63] = '\0';
            } else if (key.startsWith("STATION_") && key.endsWith("_NAME")) {
                int idx = key.substring(8, key.indexOf("_NAME")).toInt();
                if (idx >= 0 && idx < MAX_STATIONS) {
                    strncpy(cfg_stations[idx].name, val.c_str(), 63);
                    cfg_stations[idx].name[63] = '\0';
                    cfg_stations[idx].streamCount = 1;
                    cfg_stations[idx].isTuba = false;
                    if (idx >= cfg_station_count) cfg_station_count = idx + 1;
                }
            } else if (key.startsWith("STATION_") && key.endsWith("_URL")) {
                int idx = key.substring(8, key.indexOf("_URL")).toInt();
                if (idx >= 0 && idx < MAX_STATIONS) {
                    strncpy(cfg_stations[idx].streams[0].url, val.c_str(), 255);
                    cfg_stations[idx].streams[0].url[255] = '\0';
                }
            } else if (key.startsWith("STATION_") && key.endsWith("_LABEL")) {
                int idx = key.substring(8, key.indexOf("_LABEL")).toInt();
                if (idx >= 0 && idx < MAX_STATIONS) {
                    strncpy(cfg_stations[idx].streams[0].label, val.c_str(), 31);
                    cfg_stations[idx].streams[0].label[31] = '\0';
                }
            }
        }
        lineStart = lineEnd + 1;
    }
    
    info("AP", "Config loaded from file: " + String(cfg_station_count) + " stations");
    return true;
}

// ===== Zapis konfiguracji do pliku =====
inline void saveConfigToFile() {
    String content;
    
    content += "WIFI_SSID=" + String(cfg_wifi_ssid) + "\n";
    content += "WIFI_PASS=" + String(cfg_wifi_pass) + "\n";
    
    for (int i = 0; i < cfg_station_count && i < MAX_STATIONS; i++) {
        if (strlen(cfg_stations[i].name) == 0) continue;
        content += "STATION_" + String(i) + "_NAME=" + String(cfg_stations[i].name) + "\n";
        content += "STATION_" + String(i) + "_URL=" + String(cfg_stations[i].streams[0].url) + "\n";
        content += "STATION_" + String(i) + "_LABEL=" + String(cfg_stations[i].streams[0].label) + "\n";
    }
    
    writeConfigFile(content);
}

// ===== Handler HTTP: GET /api/config =====
static void handleGetConfig() {
    String json = "{";
    json += "\"wifi_ssid\":\"" + String(cfg_wifi_ssid) + "\",";
    json += "\"wifi_pass\":\"" + String(cfg_wifi_pass) + "\",";
    json += "\"stations\":[";
    bool first = true;
    for (int i = 0; i < cfg_station_count && i < MAX_STATIONS; i++) {
        if (strlen(cfg_stations[i].name) == 0) continue;
        if (!first) json += ",";
        first = false;
        json += "{";
        json += "\"name\":\"" + String(cfg_stations[i].name) + "\",";
        json += "\"url\":\"" + String(cfg_stations[i].streams[0].url) + "\",";
        json += "\"label\":\"" + String(cfg_stations[i].streams[0].label) + "\"";
        json += "}";
    }
    json += "]}";
    apServer->send(200, "application/json", json);
}

// ===== Handler HTTP: POST /api/config =====
static void handlePostConfig() {
    if (!apServer->hasArg("plain")) {
        apServer->send(400, "text/plain", "No data");
        return;
    }
    
    String body = apServer->arg("plain");
    info("AP", "Received config: " + String(body.length()) + " bytes");
    
    // Użyj ArduinoJson do parsowania JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        error("AP", "JSON parse error: " + String(err.c_str()));
        apServer->send(400, "text/plain", "JSON parse error");
        return;
    }
    
    // WiFi
    const char* ssid = doc["wifi_ssid"] | "";
    const char* pass = doc["wifi_pass"] | "";
    strncpy(cfg_wifi_ssid, ssid, 63);
    cfg_wifi_ssid[63] = '\0';
    strncpy(cfg_wifi_pass, pass, 63);
    cfg_wifi_pass[63] = '\0';
    
    // Stations
    JsonArray stations = doc["stations"].as<JsonArray>();
    int idx = 0;
    for (JsonObject s : stations) {
        if (idx >= MAX_STATIONS) break;
        
        const char* name = s["name"] | "";
        const char* url = s["url"] | "";
        const char* label = s["label"] | "";
        
        strncpy(cfg_stations[idx].name, name, 63);
        cfg_stations[idx].name[63] = '\0';
        strncpy(cfg_stations[idx].streams[0].url, url, 255);
        cfg_stations[idx].streams[0].url[255] = '\0';
        strncpy(cfg_stations[idx].streams[0].label, label, 31);
        cfg_stations[idx].streams[0].label[31] = '\0';
        cfg_stations[idx].streamCount = 1;
        cfg_stations[idx].streams[0].quality = Q_HQ;
        cfg_stations[idx].isTuba = false;
        
        idx++;
    }
    cfg_station_count = idx;
    
    // Zapisz tylko do pliku
    saveConfigToFile();
    saveVolume(cfg_volume);
    
    apServer->send(200, "text/plain", "OK - Restarting...");
    info("AP", "Config saved to file (" + String(cfg_station_count) + " stations), restarting...");
    
    delay(1000);
    ESP.restart();
}

// ===== Handler HTTP: POST /api/restart =====
static void handleRestart() {
    apServer->send(200, "text/plain", "Restarting...");
    delay(500);
    ESP.restart();
}

// ===== Debug: wyświetl zawartość pliku =====
static void handleConfigRaw() {
    String content = readConfigFile();
    if (content.length() == 0) {
        apServer->send(200, "text/plain", "Brak pliku /config.txt\n");
    } else {
        apServer->send(200, "text/plain", content);
    }
}

// ===== Strona główna =====
static void handleRoot() {
    apServer->send(200, "text/html", apIndexHTML);
}

// ===== Start AP =====
inline void startAP() {
    if (apActive) return;
    
    info("AP", "Starting AP mode...");
    
    // LittleFS już zainicjalizowany w setup() - nie formatuj
    // Konfiguracja WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CLIENTS);
    apIP = WiFi.softAPIP();
    
    info("AP", "AP IP: " + apIP.toString());
    
    // Start serwera WWW
    apServer = new WebServer(80);
    apServer->on("/", handleRoot);
    apServer->on("/api/config", HTTP_GET, handleGetConfig);
    apServer->on("/api/config", HTTP_POST, handlePostConfig);
    apServer->on("/api/config/raw", HTTP_GET, handleConfigRaw);
    apServer->on("/api/restart", HTTP_POST, handleRestart);
    apServer->begin();
    
    apActive = true;
    info("AP", "AP mode active. IP: " + apIP.toString());
}

// ===== Stop AP =====
inline void stopAP() {
    if (!apActive) return;
    
    info("AP", "Stopping AP mode...");
    
    // Zatrzymaj serwer
    if (apServer) {
        apServer->stop();
        delete apServer;
        apServer = nullptr;
    }
    
    // Wyłącz AP
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    
    apActive = false;
    info("AP", "AP mode stopped. Resources freed.");
}

// ===== Pętla AP (wywoływana w loop) =====
inline void handleAPLoop() {
    if (apActive && apServer) {
        apServer->handleClient();
    }
}

#endif