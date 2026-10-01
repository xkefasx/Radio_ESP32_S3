#ifndef TIMER_LOGIC_H
#define TIMER_LOGIC_H

#include <Arduino_GFX_Library.h>
#include <time.h>
#include "config.h"
#include "radio_logic.h"

// ===== Stałe dla timera =====
const int TIMER_HEADER_H = 40;
const int TIMER_MIN_VAL = 0;
const int TIMER_MAX_VAL = 120;  // 120 minut
const int TIMER_STEP = 1;      // Krok 1 minuta

// ===== Zmienne timera (deklaracje, definicje w main.cpp) =====
extern std::atomic<bool> timerActive;
extern std::atomic<int> timerRemainingSeconds;
extern TimerMode currentTimerMode;
extern int timerMinutes;
extern int alarmHour;
extern int alarmMinute;
extern unsigned long lastTouchAction; // Z main.cpp

// ===== Funkcje =====
void drawTimerUI(Arduino_Canvas *canvas, int br, int vol, bool isPlaying, bool isConn) {
    canvas->fillScreen(COL_BG);
    
    // Górny pasek
    canvas->fillRect(0, 0, 320, TIMER_HEADER_H, COL_BG_HEADER);
    
    // Nazwa trybu (SLEEP/ALARM)
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(1);
    canvas->setCursor(10, 12);
    if (currentTimerMode == TIMER_MODE_SLEEP) {
        canvas->print("SLEEP TIMER");
    } else {
        canvas->print("RADIO ALARM");
    }
    
    // Przełącznik trybu w prawym górnym rogu
    canvas->setTextColor(COL_ORANGE);
    canvas->setCursor(240, 12);
    canvas->print("MODE>");
    
    // ===== CENTRALNY WYŚWIETLANIE =====
    if (timerActive.load()) {
        // Odliczanie - wyświetl pozostały czas
        int remaining = timerRemainingSeconds.load();
        int minutes = remaining / 60;
        int seconds = remaining % 60;
        char timeStr[16];
        sprintf(timeStr, "%02d:%02d", minutes, seconds);
        canvas->setTextColor(COL_GREEN);
        canvas->setTextSize(4);
        int textW = strlen(timeStr) * 24;
        canvas->setCursor((320 - textW) / 2, 70);
        canvas->print(timeStr);
    } else if (currentTimerMode == TIMER_MODE_SLEEP) {
        // Tryb SLEEP - wyświetlanie minut
        char timeStr[16];
        sprintf(timeStr, "%d min", timerMinutes);
        canvas->setTextColor(COL_TEXT);
        canvas->setTextSize(4);
        int textW = strlen(timeStr) * 24;
        canvas->setCursor((320 - textW) / 2, 70);
        canvas->print(timeStr);
    } else {
        // Tryb ALARM - wyświetlanie godziny:minuty
        char timeStr[16];
        sprintf(timeStr, "%02d:%02d", alarmHour, alarmMinute);
        canvas->setTextColor(COL_TEXT);
        canvas->setTextSize(4);
        int textW = strlen(timeStr) * 24;
        canvas->setCursor((320 - textW) / 2, 70);
        canvas->print(timeStr);
    }
    
    // ===== Przyciski =====
    const int btnW = 80;
    const int btnH = 35;
    const int btnY = 130;
    
    // Przycisk -15 min
    canvas->fillRect(30, btnY, btnW, btnH, COL_BG_CARD);
    canvas->drawRect(30, btnY, btnW, btnH, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(2);
    canvas->setCursor(50, btnY + 12);
    canvas->print("-1");
    
    // Przycisk +15 min
    canvas->fillRect(210, btnY, btnW, btnH, COL_BG_CARD);
    canvas->drawRect(210, btnY, btnW, btnH, COL_DIVIDER);
    canvas->setCursor(230, btnY + 12);
    canvas->print("+1");
    
    // Przycisk START/STOP
    const int startBtnW = 100;
    const int startBtnH = 40;
    const int startBtnX = (320 - startBtnW) / 2;
    const int startBtnY = 180;
    
    if (timerActive.load()) {
        canvas->fillRect(startBtnX, startBtnY, startBtnW, startBtnH, COL_RED);
    } else {
        canvas->fillRect(startBtnX, startBtnY, startBtnW, startBtnH, COL_GREEN);
    }
    canvas->drawRect(startBtnX, startBtnY, startBtnW, startBtnH, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(2);
    canvas->setCursor(startBtnX + 20, startBtnY + 24);
    if (timerActive.load()) {
        canvas->print("STOP");
    } else {
        canvas->print("START");
    }
    
    // ===== Dolny pasek nawigacji =====
    canvas->fillRect(0, 210, 320, 30, COL_NAV_BG);
    canvas->drawFastHLine(0, 210, 320, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(1);
    canvas->setCursor(10, 218);
    canvas->print("< CLOCK");
}

// Obsługa dotyku na ekranie timera
void handleTimerTouch(int tx, int ty, unsigned long now) {
    const unsigned long TOUCH_DEBOUNCE_MS = 250;
    
    // Przycisk -15 min
    if (tx >= 30 && tx <= 110 && ty >= 130 && ty <= 165) {
        if ((now - lastTouchAction) > TOUCH_DEBOUNCE_MS) {
            if (currentTimerMode == TIMER_MODE_SLEEP) {
                timerMinutes -= TIMER_STEP;
                if (timerMinutes < TIMER_MIN_VAL) timerMinutes = TIMER_MIN_VAL;
            } else {
                // ALARM: zmiana minut w dół
                alarmMinute -= 5;
                if (alarmMinute < 0) {
                    alarmMinute = 55;
                    alarmHour--;
                    if (alarmHour < 0) alarmHour = 23;
                }
            }
            lastTouchAction = now;
        }
    }
    // Przycisk +15 min
    else if (tx >= 210 && tx <= 290 && ty >= 130 && ty <= 165) {
        if ((now - lastTouchAction) > TOUCH_DEBOUNCE_MS) {
            if (currentTimerMode == TIMER_MODE_SLEEP) {
                timerMinutes += TIMER_STEP;
                if (timerMinutes > TIMER_MAX_VAL) timerMinutes = TIMER_MAX_VAL;
            } else {
                // ALARM: zmiana minut w górę
                alarmMinute += 5;
                if (alarmMinute > 59) {
                    alarmMinute = 0;
                    alarmHour++;
                    if (alarmHour > 23) alarmHour = 0;
                }
            }
            lastTouchAction = now;
        }
    }
    // Przycisk START/STOP
    else if (tx >= 110 && tx <= 210 && ty >= 180 && ty <= 220) {
        if ((now - lastTouchAction) > TOUCH_DEBOUNCE_MS) {
            if (timerActive.load()) {
                timerActive = false;
                timerRemainingSeconds = 0;
            } else {
                if (currentTimerMode == TIMER_MODE_SLEEP) {
                    if (timerMinutes > 0) {
                        timerActive = true;
                        timerRemainingSeconds = timerMinutes * 60;
                    }
                } else {
                    // ALARM: sprawdź czy nie jest już czas na alarm
                    struct tm ti;
                    if (getLocalTime(&ti)) {
                        int currentMinutes = ti.tm_hour * 60 + ti.tm_min;
                        int alarmMinutes = alarmHour * 60 + alarmMinute;
                        if (alarmMinutes > currentMinutes) {
                            timerRemainingSeconds = (alarmMinutes - currentMinutes) * 60;
                        } else {
                            // Alarm jutro
                            timerRemainingSeconds = ((24 * 60 - currentMinutes) + alarmMinutes) * 60;
                        }
                        timerActive = true;
                    }
                }
            }
            lastTouchAction = now;
        }
    }
    // Przełącznik trybu (górny prawy róg)
    else if (tx >= 240 && tx <= 310 && ty >= 0 && ty <= 40) {
        if ((now - lastTouchAction) > TOUCH_DEBOUNCE_MS) {
            currentTimerMode = (currentTimerMode == TIMER_MODE_SLEEP) ? TIMER_MODE_ALARM : TIMER_MODE_SLEEP;
            // Reset timera przy zmianie trybu
            timerActive = false;
            timerRemainingSeconds = 0;
            lastTouchAction = now;
        }
    }
}

// Obsługa enkodera #2 dla zmiany wartości
void handleTimerEncoder(int delta) {
    if (currentTimerMode == TIMER_MODE_SLEEP) {
        // SLEEP: zmiana minut
        timerMinutes += delta * TIMER_STEP;
        if (timerMinutes < TIMER_MIN_VAL) timerMinutes = TIMER_MIN_VAL;
        if (timerMinutes > TIMER_MAX_VAL) timerMinutes = TIMER_MAX_VAL;
    } else {
        // ALARM: zmiana godzin/minut
        if (delta > 0) {
            alarmMinute += 5;
            if (alarmMinute > 59) {
                alarmMinute = 0;
                alarmHour++;
                if (alarmHour > 23) alarmHour = 0;
            }
        } else {
            alarmMinute -= 5;
            if (alarmMinute < 0) {
                alarmMinute = 55;
                alarmHour--;
                if (alarmHour < 0) alarmHour = 23;
            }
        }
    }
}

#endif