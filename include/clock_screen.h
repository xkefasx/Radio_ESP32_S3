#ifndef CLOCK_SCREEN_H
#define CLOCK_SCREEN_H

#include <Arduino_GFX_Library.h>
#include <time.h>
#include "config.h"
#include "radio_logic.h"

// ===== EKRAN ZEGARA (MODE_CLOCK) =====
void drawClockUI(Arduino_Canvas *canvas, int br, int vol, bool isPlaying, bool isConn) {
    canvas->fillScreen(COL_BG);
    
    // Duża godzina w centrum (size 6)
    struct tm ti;
    if (getLocalTime(&ti)) {
        char timeStr[9];
        strftime(timeStr, 9, "%H:%M", &ti);
        canvas->setTextColor(COL_TEXT);
        canvas->setTextSize(6);
        int textW = strlen(timeStr) * 36; // ~36px per char w size 6
        canvas->setCursor((320 - textW) / 2, 70);
        canvas->print(timeStr);
    }
    
    // Mała temperatura pod zegarem
    extern bool weatherLoaded;
    extern float temps[73];
    if (weatherLoaded) {
        float currentTemp = temps[72];
        canvas->setTextColor(COL_TEXT_SEC);
        canvas->setTextSize(2);
        char tempStr[10];
        sprintf(tempStr, "%.1f°C", currentTemp);
        int tempW = strlen(tempStr) * 12;
        canvas->setCursor((320 - tempW) / 2, 130);
        canvas->print(tempStr);
    }
    
    // Informacja o grającym radiu na dole
    canvas->fillRect(0, 200, 320, 40, COL_BG_HEADER);
    canvas->setTextSize(1);
    if (isConn) {
        canvas->setTextColor(COL_YELLOW);
        canvas->setCursor(10, 218);
        canvas->print("Laczenie...");
    } else if (isPlaying) {
        canvas->setTextColor(COL_GREEN);
        canvas->setCursor(10, 218);
        canvas->print("> ");
        extern int activeIdx;
        if (activeIdx >= 0) {
            canvas->print(stations[activeIdx].name);
        } else {
            canvas->print("Radio");
        }
    } else {
        canvas->setTextColor(COL_TEXT_SEC);
        canvas->setCursor(10, 218);
        canvas->print("|| Radio zatrzymane");
    }
    
    // Jasność i głośność w prawym dolnym rogu
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(240, 218);
    canvas->printf("B:%d%% V:%d", (br * 100) / 255, vol);
}

#endif