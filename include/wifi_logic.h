#ifndef WIFI_LOGIC_H
#define WIFI_LOGIC_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "logger.h"

void setupWiFi() {
    info("WIFI", "Reset stanu WiFi przed laczeniem...");
    
    // Pełny reset state machine WiFi
    WiFi.persistent(false);
    WiFi.setSleep(false);           // Wyłącz power save - może zakłócać po flashu
    WiFi.disconnect(true, true);    // force=true, erase_ap=true
    WiFi.mode(WIFI_AP_STA);         // Przez AP_STA dla pełniejszego resetu
    delay(200);
    esp_task_wdt_reset();
    WiFi.mode(WIFI_OFF);
    delay(500);                     // Dłuższy delay - daj radiu czas na wyciszenie
    esp_task_wdt_reset();
    
    WiFi.mode(WIFI_STA);

    // Próba 1: standardowe połączenie
    info("WIFI", "Laczenie z: " + String(WIFI_SSID));
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
        esp_task_wdt_reset();
    }
    
    // Jeśli nie udało się za pierwszym razem - próba 2 z pełnym resetem
    if (WiFi.status() != WL_CONNECTED) {
        info("WIFI", "Próba 1 nieudana, reset i ponowna próba...");
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        delay(300);
        esp_task_wdt_reset();
        WiFi.mode(WIFI_STA);
        delay(100);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        
        attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
            esp_task_wdt_reset();
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        info("WIFI", "Polaczono! IP: " + WiFi.localIP().toString());
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
    } else {
        error("WIFI", "Timeout polaczenia WiFi po 20s (2 proby)!");
        esp_task_wdt_reset();
    }
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

#endif