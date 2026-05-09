#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "radio_logic.h"
#include "logger.h"

// ===== Klucze Preferences =====
#define PREF_NAMESPACE "radio"
#define PREF_WIFI_SSID  "wifi_ssid"
#define PREF_WIFI_PASS  "wifi_pass"
#define PREF_VOLUME     "volume"
#define PREF_STATION_CNT "station_cnt"

// ===== Domyślne wartości =====
#define DEFAULT_WIFI_SSID "PLAY_Swiatlowod_36EE"
#define DEFAULT_WIFI_PASS "XT7SUqGt"
#define DEFAULT_VOLUME 6
#define MAX_STATIONS 16

// ===== Globalne zmienne konfiguracyjne =====
extern char cfg_wifi_ssid[64];
extern char cfg_wifi_pass[64];
extern int cfg_volume;
extern RadioStation cfg_stations[MAX_STATIONS];
extern int cfg_station_count;

// ===== Forward declarations =====
void loadConfig();
void saveConfig();
void saveVolume(int vol);
void saveWiFi(const char* ssid, const char* pass);
void saveStation(int idx, const char* name, const char* url, const char* label, StreamQuality quality, bool isTuba);
void saveStationCount(int count);

// ===== Implementacja =====

char cfg_wifi_ssid[64] = "";
char cfg_wifi_pass[64] = "";
int cfg_volume = DEFAULT_VOLUME;
RadioStation cfg_stations[MAX_STATIONS];
int cfg_station_count = 0;

// Domyślne stacje (fallback gdy brak w NVS)
static const RadioStation DEFAULT_STATIONS[] = {
    {"RMF FM",      {{"http://stream11.radiostream.pl/tuba1-1.mp3",    Q_HQ, "192 MP3"}}, 1, true},
    {"Radio ZET",   {{"http://stream11.radiostream.pl/tuba2-1.mp3",    Q_HQ, "224 MP3"}}, 1, true},
    {"Radio 357",   {{"http://stream.radio357.pl/m3u8",   Q_HQ, "224 AAC"}}, 1, true},
    {"Złote Przeboje", {{"http://stream11.radiostream.pl/tuba3-1.mp3", Q_HQ, "192 MP3"}}, 1, true},
    {"Antyradio",   {{"http://an01.cdn.eurozet.pl/ant-waw.mp3",    Q_HQ, "128 MP3"}}, 1, true},
    {"TOK FM",      {{"http://stream30.radiostream.pl/tuba10-1.mp3",   Q_MQ, "128 MP3"}}, 1, true},
    {"Eska Rock",   {{"http://stream11.radiostream.pl/tuba8-1.mp3",    Q_HQ, "192 MP3"}}, 1, true},
    {"TOK FM 2",    {{"https://stream30.radiostream.pl/tuba10-1.mp3",  Q_MQ, "128 MP3"}}, 1, true},
};
static const int DEFAULT_STATION_COUNT = sizeof(DEFAULT_STATIONS) / sizeof(DEFAULT_STATIONS[0]);

// ===== Funkcje pomocnicze =====
static void applyDefaultStations() {
    cfg_station_count = DEFAULT_STATION_COUNT;
    for (int i = 0; i < cfg_station_count && i < MAX_STATIONS; i++) {
        strncpy(cfg_stations[i].name, DEFAULT_STATIONS[i].name, 63);
        cfg_stations[i].name[63] = '\0';
        cfg_stations[i].streamCount = DEFAULT_STATIONS[i].streamCount;
        cfg_stations[i].isTuba = DEFAULT_STATIONS[i].isTuba;
        for (int j = 0; j < cfg_stations[i].streamCount; j++) {
            strncpy(cfg_stations[i].streams[j].url, DEFAULT_STATIONS[i].streams[j].url, 255);
            cfg_stations[i].streams[j].url[255] = '\0';
            cfg_stations[i].streams[j].quality = DEFAULT_STATIONS[i].streams[j].quality;
            strncpy(cfg_stations[i].streams[j].label, DEFAULT_STATIONS[i].streams[j].label, 31);
            cfg_stations[i].streams[j].label[31] = '\0';
        }
    }
}

// ===== Ładowanie konfiguracji z NVS =====
inline void loadConfig() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, true); // read-only

    // Sprawdź czy NVS zawiera jakieś dane (sprawdzając station_cnt)
    bool hasData = prefs.isKey(PREF_STATION_CNT);
    
    // WiFi - jeśli brak w NVS, użyj domyślnych
    if (prefs.isKey(PREF_WIFI_SSID)) {
        String ssid = prefs.getString(PREF_WIFI_SSID, DEFAULT_WIFI_SSID);
        strncpy(cfg_wifi_ssid, ssid.c_str(), 63);
        cfg_wifi_ssid[63] = '\0';
    } else {
        strncpy(cfg_wifi_ssid, DEFAULT_WIFI_SSID, 63);
        cfg_wifi_ssid[63] = '\0';
    }
    if (prefs.isKey(PREF_WIFI_PASS)) {
        String pass = prefs.getString(PREF_WIFI_PASS, DEFAULT_WIFI_PASS);
        strncpy(cfg_wifi_pass, pass.c_str(), 63);
        cfg_wifi_pass[63] = '\0';
    } else {
        strncpy(cfg_wifi_pass, DEFAULT_WIFI_PASS, 63);
        cfg_wifi_pass[63] = '\0';
    }

    // Volume
    if (prefs.isKey(PREF_VOLUME)) {
        cfg_volume = prefs.getInt(PREF_VOLUME, DEFAULT_VOLUME);
    } else {
        cfg_volume = DEFAULT_VOLUME;
    }
    if (cfg_volume < 0) cfg_volume = 0;
    if (cfg_volume > 21) cfg_volume = 21;

    // Station count
    if (hasData) {
        cfg_station_count = prefs.getInt(PREF_STATION_CNT, DEFAULT_STATION_COUNT);
    } else {
        cfg_station_count = 0;
    }
    
    if (cfg_station_count <= 0 || cfg_station_count > MAX_STATIONS) {
        // Brak stacji w NVS - użyj domyślnych (bez logowania błędów)
        prefs.end();
        applyDefaultStations();
        info("CFG", "Using default stations (" + String(DEFAULT_STATION_COUNT) + ")");
        return;
    }

    // Wczytaj stacje z NVS (tylko jeśli istnieją w NVS)
    for (int i = 0; i < cfg_station_count && i < MAX_STATIONS; i++) {
        String key_prefix = "s" + String(i) + "_";
        
        if (!prefs.isKey((key_prefix + "name").c_str())) {
            // Brak danych stacji - użyj domyślnych
            prefs.end();
            applyDefaultStations();
            info("CFG", "Station " + String(i) + " missing, using defaults");
            return;
        }
        
        String name = prefs.getString((key_prefix + "name").c_str(), "");
        strncpy(cfg_stations[i].name, name.c_str(), 63);
        cfg_stations[i].name[63] = '\0';
        cfg_stations[i].streamCount = prefs.getInt((key_prefix + "scnt").c_str(), 1);
        cfg_stations[i].isTuba = prefs.getBool((key_prefix + "tuba").c_str(), false);
        
        for (int j = 0; j < cfg_stations[i].streamCount && j < 3; j++) {
            String skey = key_prefix + "s" + String(j) + "_";
            if (prefs.isKey((skey + "url").c_str())) {
                String url = prefs.getString((skey + "url").c_str(), "");
                strncpy(cfg_stations[i].streams[j].url, url.c_str(), 255);
                cfg_stations[i].streams[j].url[255] = '\0';
            }
            cfg_stations[i].streams[j].quality = (StreamQuality)prefs.getInt((skey + "q").c_str(), Q_MQ);
            String label = prefs.getString((skey + "lbl").c_str(), "");
            strncpy(cfg_stations[i].streams[j].label, label.c_str(), 31);
            cfg_stations[i].streams[j].label[31] = '\0';
        }
    }

    prefs.end();
    info("CFG", "Config loaded: " + String(cfg_station_count) + " stations, vol=" + String(cfg_volume));
}

// ===== Zapis całej konfiguracji =====
inline void saveConfig() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false); // read-write

    // WiFi
    prefs.putString(PREF_WIFI_SSID, String(cfg_wifi_ssid));
    prefs.putString(PREF_WIFI_PASS, String(cfg_wifi_pass));

    // Volume
    prefs.putInt(PREF_VOLUME, cfg_volume);

    // Station count
    prefs.putInt(PREF_STATION_CNT, cfg_station_count);

    // Stations
    for (int i = 0; i < cfg_station_count && i < MAX_STATIONS; i++) {
        String key_prefix = "s" + String(i) + "_";
        
        prefs.putString((key_prefix + "name").c_str(), String(cfg_stations[i].name));
        prefs.putInt((key_prefix + "scnt").c_str(), cfg_stations[i].streamCount);
        prefs.putBool((key_prefix + "tuba").c_str(), cfg_stations[i].isTuba);
        
        for (int j = 0; j < cfg_stations[i].streamCount && j < 3; j++) {
            String skey = key_prefix + "s" + String(j) + "_";
            prefs.putString((skey + "url").c_str(), String(cfg_stations[i].streams[j].url));
            prefs.putInt((skey + "q").c_str(), (int)cfg_stations[i].streams[j].quality);
            prefs.putString((skey + "lbl").c_str(), String(cfg_stations[i].streams[j].label));
        }
    }

    prefs.end();
    info("CFG", "Config saved: " + String(cfg_station_count) + " stations");
}

// ===== Zapis tylko głośności (szybki) =====
inline void saveVolume(int vol) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putInt(PREF_VOLUME, vol);
    prefs.end();
}

// ===== Zapis WiFi =====
inline void saveWiFi(const char* ssid, const char* pass) {
    strncpy(cfg_wifi_ssid, ssid, 63);
    cfg_wifi_ssid[63] = '\0';
    strncpy(cfg_wifi_pass, pass, 63);
    cfg_wifi_pass[63] = '\0';
    
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString(PREF_WIFI_SSID, String(cfg_wifi_ssid));
    prefs.putString(PREF_WIFI_PASS, String(cfg_wifi_pass));
    prefs.end();
    info("CFG", "WiFi saved");
}

// ===== Zapis pojedynczej stacji =====
inline void saveStation(int idx, const char* name, const char* url, const char* label, StreamQuality quality, bool isTuba) {
    if (idx < 0 || idx >= MAX_STATIONS) return;
    
    strncpy(cfg_stations[idx].name, name, 63);
    cfg_stations[idx].name[63] = '\0';
    cfg_stations[idx].streamCount = 1;
    cfg_stations[idx].isTuba = isTuba;
    strncpy(cfg_stations[idx].streams[0].url, url, 255);
    cfg_stations[idx].streams[0].url[255] = '\0';
    cfg_stations[idx].streams[0].quality = quality;
    strncpy(cfg_stations[idx].streams[0].label, label, 31);
    cfg_stations[idx].streams[0].label[31] = '\0';
    
    if (idx >= cfg_station_count) {
        cfg_station_count = idx + 1;
    }
    
    saveConfig();
}

// ===== Zapis liczby stacji =====
inline void saveStationCount(int count) {
    if (count > 0 && count <= MAX_STATIONS) {
        cfg_station_count = count;
        Preferences prefs;
        prefs.begin(PREF_NAMESPACE, false);
        prefs.putInt(PREF_STATION_CNT, count);
        prefs.end();
    }
}

#endif