#ifndef VARIABLES_H
#define VARIABLES_H

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>

// Include radio_logic.h for StreamQuality, RadioStream, RadioStation definitions
#include "radio_logic.h"

// ===== Default WiFi credentials =====
const char* DEFAULT_WIFI_SSID = "PLAY_Swiatlowod_36EE";
const char* DEFAULT_WIFI_PASS = "XT7SUqGt";

// ===== Default stations =====
const RadioStation DEFAULT_STATIONS[] = {
    {"RMF FM",      {{"http://stream11.radiostream.pl/tuba1-1.mp3",    Q_HQ, "192 MP3"}}, 1, true},
    {"Radio ZET",   {{"http://stream11.radiostream.pl/tuba2-1.mp3",    Q_HQ, "224 MP3"}}, 1, true},
    {"Radio 357",   {{"http://stream.radio357.pl/m3u8",   Q_HQ, "224 AAC"}}, 1, true},
    {"Złote Przeboje", {{"http://stream11.radiostream.pl/tuba3-1.mp3", Q_HQ, "192 MP3"}}, 1, true},
    {"Antyradio",   {{"http://an01.cdn.eurozet.pl/ant-waw.mp3",    Q_HQ, "128 MP3"}}, 1, true},
    {"TOK FM",      {{"http://stream30.radiostream.pl/tuba10-1.mp3",   Q_MQ, "128 MP3"}}, 1, true},
    {"Eska Rock",   {{"http://stream11.radiostream.pl/tuba8-1.mp3",    Q_HQ, "192 MP3"}}, 1, true},
    {"TOK FM 2",      {{"https://stream30.radiostream.pl/tuba10-1.mp3",   Q_MQ, "128 MP3"}}, 1, true},
};

const int DEFAULT_TOTAL_STATIONS = sizeof(DEFAULT_STATIONS) / sizeof(DEFAULT_STATIONS[0]);
const int DEFAULT_VOLUME = 6;

// ===== Global variables (loaded from file or defaults) =====
char wifi_ssid[64];
char wifi_pass[64];
RadioStation* stations = nullptr;
int total_stations = 0;
int volume = DEFAULT_VOLUME;

// ===== Helper function to trim whitespace =====
inline void trim(char* str) {
    if (!str) return;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    char* start = str;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
}

// ===== Helper function to parse key=value line =====
inline bool parseLine(const char* line, char* key, char* value) {
    const char* eq = strchr(line, '=');
    if (!eq) return false;
    int keyLen = eq - line;
    if (keyLen >= 64) return false;
    strncpy(key, line, keyLen);
    key[keyLen] = '\0';
    trim(key);
    strcpy(value, eq + 1);
    trim(value);
    return true;
}

// ===== Helper function to parse quality string =====
inline StreamQuality parseQuality(const char* str) {
    if (strcmp(str, "LQ") == 0) return Q_LQ;
    if (strcmp(str, "MQ") == 0) return Q_MQ;
    if (strcmp(str, "HQ") == 0) return Q_HQ;
    if (strcmp(str, "SSL") == 0) return Q_SSL;
    return Q_MQ; // default
}

// ===== Apply default values =====
inline void applyDefaultValues() {
    strncpy(wifi_ssid, DEFAULT_WIFI_SSID, 63);
    wifi_ssid[63] = '\0';
    strncpy(wifi_pass, DEFAULT_WIFI_PASS, 63);
    wifi_pass[63] = '\0';
    volume = DEFAULT_VOLUME;
    
    // Allocate and copy default stations
    if (stations) {
        delete[] stations;
    }
    total_stations = DEFAULT_TOTAL_STATIONS;
    stations = new RadioStation[total_stations];
    for (int i = 0; i < total_stations; i++) {
        stations[i] = DEFAULT_STATIONS[i];
    }
}

// ===== Create default variables file =====
inline bool createDefaultVariablesFile() {
    File file = LittleFS.open("/variables.txt", "w");
    if (!file) {
        Serial.println("[VARS] Failed to create default variables file");
        return false;
    }
    
    file.printf("WIFI_SSID=%s\n", DEFAULT_WIFI_SSID);
    file.printf("WIFI_PASS=%s\n", DEFAULT_WIFI_PASS);
    file.printf("VOLUME=%d\n", DEFAULT_VOLUME);
    file.printf("STATION_COUNT=%d\n", DEFAULT_TOTAL_STATIONS);
    
    for (int i = 0; i < DEFAULT_TOTAL_STATIONS; i++) {
        file.printf("STATION_%d_NAME=%s\n", i, DEFAULT_STATIONS[i].name);
        file.printf("STATION_%d_STREAM_COUNT=%d\n", i, DEFAULT_STATIONS[i].streamCount);
        file.printf("STATION_%d_IS_TUBA=%s\n", i, DEFAULT_STATIONS[i].isTuba ? "true" : "false");
        for (int j = 0; j < DEFAULT_STATIONS[i].streamCount; j++) {
            file.printf("STATION_%d_STREAM_%d_URL=%s\n", i, j, DEFAULT_STATIONS[i].streams[j].url);
            file.printf("STATION_%d_STREAM_%d_QUALITY=%s\n", i, j, 
                DEFAULT_STATIONS[i].streams[j].quality == Q_LQ ? "LQ" :
                DEFAULT_STATIONS[i].streams[j].quality == Q_MQ ? "MQ" :
                DEFAULT_STATIONS[i].streams[j].quality == Q_HQ ? "HQ" : "SSL");
            file.printf("STATION_%d_STREAM_%d_LABEL=%s\n", i, j, DEFAULT_STATIONS[i].streams[j].label);
        }
    }
    
    file.close();
    Serial.println("[VARS] Default variables file created");
    return true;
}

// ===== Load variables from file =====
inline bool loadVariables() {
    if (!LittleFS.exists("/variables.txt")) {
        Serial.println("[VARS] Variables file not found, creating default");
        applyDefaultValues();
        return createDefaultVariablesFile();
    }
    
    File file = LittleFS.open("/variables.txt", "r");
    if (!file) {
        Serial.println("[VARS] Failed to open variables file, using defaults");
        applyDefaultValues();
        return false;
    }
    
    // Temporary storage for loaded data
    char temp_ssid[64] = {0};
    char temp_pass[64] = {0};
    int temp_volume = DEFAULT_VOLUME;
    int temp_station_count = DEFAULT_TOTAL_STATIONS;
    RadioStation* temp_stations = nullptr;
    
    char line[512];
    bool fileValid = true;
    
    while (file.available() && fileValid) {
        int len = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        trim(line);
        
        // Skip empty lines and comments
        if (strlen(line) == 0 || line[0] == '#') continue;
        
        char key[64] = {0};
        char value[256] = {0};
        if (!parseLine(line, key, value)) continue;
        
        if (strcmp(key, "WIFI_SSID") == 0) {
            strncpy(temp_ssid, value, 63);
            temp_ssid[63] = '\0';
        } else if (strcmp(key, "WIFI_PASS") == 0) {
            strncpy(temp_pass, value, 63);
            temp_pass[63] = '\0';
        } else if (strcmp(key, "VOLUME") == 0) {
            temp_volume = atoi(value);
            if (temp_volume < 0) temp_volume = 0;
            if (temp_volume > 21) temp_volume = 21;
        } else if (strcmp(key, "STATION_COUNT") == 0) {
            temp_station_count = atoi(value);
            if (temp_station_count <= 0 || temp_station_count > 32) {
                Serial.println("[VARS] Invalid station count, using defaults");
                fileValid = false;
            }
        }
    }
    
    // Allocate stations array
    if (fileValid && temp_station_count > 0) {
        temp_stations = new RadioStation[temp_station_count];
        memset(temp_stations, 0, sizeof(RadioStation) * temp_station_count);
    }
    
    // Parse station data (second pass)
    if (fileValid && temp_stations) {
        file.seek(0);
        int current_station_idx = -1;
        int current_stream_idx = 0;
        
        while (file.available() && fileValid) {
            int len = file.readBytesUntil('\n', line, sizeof(line) - 1);
            line[len] = '\0';
            trim(line);
            
            if (strlen(line) == 0 || line[0] == '#') continue;
            
            char key[64] = {0};
            char value[256] = {0};
            if (!parseLine(line, key, value)) continue;
            
            if (strncmp(key, "STATION_", 8) == 0) {
                int idx = atoi(key + 8);
                char* subkey = strchr(key + 8, '_');
                if (subkey) {
                    subkey++; // skip underscore
                    
                    if (strcmp(subkey, "NAME") == 0) {
                        if (idx >= 0 && idx < temp_station_count) {
                            current_station_idx = idx;
                            strncpy(temp_stations[idx].name, value, 63);
                            temp_stations[idx].name[63] = '\0';
                        }
                    } else if (strcmp(subkey, "STREAM_COUNT") == 0) {
                        if (idx >= 0 && idx < temp_station_count) {
                            temp_stations[idx].streamCount = atoi(value);
                            if (temp_stations[idx].streamCount < 0) temp_stations[idx].streamCount = 0;
                            if (temp_stations[idx].streamCount > 3) temp_stations[idx].streamCount = 3;
                        }
                    } else if (strcmp(subkey, "IS_TUBA") == 0) {
                        if (idx >= 0 && idx < temp_station_count) {
                            temp_stations[idx].isTuba = (strcmp(value, "true") == 0);
                        }
                    } else if (strncmp(subkey, "STREAM_", 7) == 0) {
                        int stream_idx = atoi(subkey + 7);
                        char* stream_key = strchr(subkey + 7, '_');
                        if (stream_key && idx >= 0 && idx < temp_station_count && stream_idx >= 0 && stream_idx < 3) {
                            stream_key++; // skip underscore
                            if (strcmp(stream_key, "URL") == 0) {
                                strncpy(temp_stations[idx].streams[stream_idx].url, value, 255);
                                temp_stations[idx].streams[stream_idx].url[255] = '\0';
                            } else if (strcmp(stream_key, "QUALITY") == 0) {
                                temp_stations[idx].streams[stream_idx].quality = parseQuality(value);
                            } else if (strcmp(stream_key, "LABEL") == 0) {
                                strncpy(temp_stations[idx].streams[stream_idx].label, value, 31);
                                temp_stations[idx].streams[stream_idx].label[31] = '\0';
                            }
                        }
                    }
                }
            }
        }
        
        // Validate stations
        for (int i = 0; i < temp_station_count; i++) {
            if (strlen(temp_stations[i].name) == 0 || temp_stations[i].streamCount <= 0) {
                Serial.printf("[VARS] Invalid station data at index %d, using defaults\n", i);
                fileValid = false;
                break;
            }
        }
    }
    
    file.close();
    
    if (!fileValid) {
        Serial.println("[VARS] Invalid file format, using defaults");
        if (temp_stations) delete[] temp_stations;
        applyDefaultValues();
        return false;
    }
    
    // Apply loaded values
    strncpy(wifi_ssid, temp_ssid, 63);
    wifi_ssid[63] = '\0';
    strncpy(wifi_pass, temp_pass, 63);
    wifi_pass[63] = '\0';
    volume = temp_volume;
    
    if (stations) delete[] stations;
    stations = temp_stations;
    total_stations = temp_station_count;
    
    Serial.println("[VARS] Variables loaded successfully");
    Serial.printf("[VARS] WiFi: %s, Volume: %d, Stations: %d\n", wifi_ssid, volume, total_stations);
    return true;
}

// ===== Save variables to file =====
inline bool saveVariables() {
    File file = LittleFS.open("/variables.txt", "w");
    if (!file) {
        Serial.println("[VARS] Failed to open file for writing");
        return false;
    }
    
    file.printf("WIFI_SSID=%s\n", wifi_ssid);
    file.printf("WIFI_PASS=%s\n", wifi_pass);
    file.printf("VOLUME=%d\n", volume);
    file.printf("STATION_COUNT=%d\n", total_stations);
    
    for (int i = 0; i < total_stations; i++) {
        file.printf("STATION_%d_NAME=%s\n", i, stations[i].name);
        file.printf("STATION_%d_STREAM_COUNT=%d\n", i, stations[i].streamCount);
        file.printf("STATION_%d_IS_TUBA=%s\n", i, stations[i].isTuba ? "true" : "false");
        for (int j = 0; j < stations[i].streamCount; j++) {
            file.printf("STATION_%d_STREAM_%d_URL=%s\n", i, j, stations[i].streams[j].url);
            file.printf("STATION_%d_STREAM_%d_QUALITY=%s\n", i, j, 
                stations[i].streams[j].quality == Q_LQ ? "LQ" :
                stations[i].streams[j].quality == Q_MQ ? "MQ" :
                stations[i].streams[j].quality == Q_HQ ? "HQ" : "SSL");
            file.printf("STATION_%d_STREAM_%d_LABEL=%s\n", i, j, stations[i].streams[j].label);
        }
    }
    
    file.close();
    Serial.println("[VARS] Variables saved successfully");
    return true;
}

// ===== Initialize LittleFS and load variables =====
inline bool initVariables() {
    if (!LittleFS.begin(true)) {
        Serial.println("[VARS] LittleFS mount failed, trying to format...");
        if (!LittleFS.begin(true)) {
            Serial.println("[VARS] LittleFS format failed, using defaults");
            applyDefaultValues();
            return false;
        }
    }
    
    Serial.println("[VARS] LittleFS mounted successfully");
    return loadVariables();
}

#endif
