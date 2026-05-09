#ifndef WEATHER_LOGIC_H
#define WEATHER_LOGIC_H

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <time.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "logger.h"
#include "radio_logic.h"

float temps[73];
float minT = 0, maxT = 40;

// Catmull-Rom spline interpolation for smooth curves
float catmullRom(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5 * ((2 * p1) + (-p0 + p2) * t +
                  (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                  (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

// Blend two RGB565 colors
uint16_t blendColors(uint16_t color1, uint16_t color2, float ratio) {
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;

    uint8_t r = (uint8_t)(r1 * ratio + r2 * (1.0f - ratio));
    uint8_t g = (uint8_t)(g1 * ratio + g2 * (1.0f - ratio));
    uint8_t b = (uint8_t)(b1 * ratio + b2 * (1.0f - ratio));

    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;

    return (r << 11) | (g << 5) | b;
}

// Simple anti-aliased line drawing (fills gaps with same color)
void drawSmoothLine(Arduino_Canvas *canvas, int x0, int y0, int x1, int y1, uint16_t color) {
    canvas->drawLine(x0, y0, x1, y1, color);

    // Calculate slope
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);

    // Draw additional pixels to smooth edges
    if (dx > dy) {
        // More horizontal - fill vertical gaps
        int step = (x1 > x0) ? 1 : -1;
        for (int x = x0; x != x1; x += step) {
            float t = (float)(x - x0) / (x1 - x0);
            int y = y0 + (y1 - y0) * t;
            canvas->drawPixel(x, y + 1, color);
            canvas->drawPixel(x, y - 1, color);
        }
    } else {
        // More vertical - fill horizontal gaps
        int step = (y1 > y0) ? 1 : -1;
        for (int y = y0; y != y1; y += step) {
            float t = (float)(y - y0) / (y1 - y0);
            int x = x0 + (x1 - x0) * t;
            canvas->drawPixel(x + 1, y, color);
            canvas->drawPixel(x - 1, y, color);
        }
    }
}

bool updateWeather() {
    info("POGODA", "Rozpoczecie aktualizacji...");
    if (WiFi.status() != WL_CONNECTED) {
        error("POGODA", "Brak polaczenia WiFi");
        return false;
    }

    HTTPClient http;
    http.begin("http://api.open-meteo.com/v1/forecast?latitude=54.35&longitude=18.65&hourly=temperature_2m&past_days=3");
    http.setTimeout(10000); // ms - zwieksz timeout do 10s

    esp_task_wdt_reset(); // przed potencjalnie blokującym GET
    int code = http.GET();
    esp_task_wdt_reset(); // po GET
    info("POGODA", "HTTP code: " + String(code));

    if (code == HTTP_CODE_OK) {
        JsonDocument doc;
        DeserializationError jsonErr = deserializeJson(doc, http.getString());
        if (jsonErr) {
            error("POGODA", "Blad JSON: " + String(jsonErr.c_str()));
            http.end();
            return false;
        }
        esp_task_wdt_reset(); // po parsowaniu JSON

        JsonArray hourly = doc["hourly"]["temperature_2m"];
        struct tm ti;
        if (!getLocalTime(&ti)) {
            http.end();
            return false;
        }
        int baseIdx = 72 + ti.tm_hour;

        minT = 100; maxT = -100;
        for (int i = 0; i <= 72; i++) {
            temps[72 - i] = hourly[max(0, baseIdx - i)];
            if (temps[72 - i] < minT) minT = temps[72 - i];
            if (temps[72 - i] > maxT) maxT = temps[72 - i];
        }
        minT -= 2; maxT += 2;
        info("POGODA", "Zaktualizowano dane");
        http.end();
        return true;
    }

    error("POGODA", "Blad pobierania: " + String(code));
    http.end();
    return false;
}

void drawWeatherUI(Arduino_Canvas *canvas, int br, int vol) {
    canvas->fillScreen(COL_BG);

    // Nagłówek statusowy taki sam jak w radiu
    canvas->fillRect(0, 0, 320, HEADER_H, COL_BG_HEADER);
    
    // ✅ Ikona Play/Pause tak samo jak w radiu
    bool isPlaying = audioPlaying.load();
    bool isConn = isConnecting;

    if (isConn) {
        canvas->setTextColor(COL_YELLOW);
        canvas->setTextSize(2);
        canvas->setCursor(PLAY_ICON_X, PLAY_ICON_Y);
        canvas->print("...");
    } else if (isPlaying) {
        // Ikona PLAY (trójkąt)
        canvas->fillTriangle(PLAY_ICON_X, PLAY_ICON_Y, PLAY_ICON_X, PLAY_ICON_Y+12, PLAY_ICON_X+10, PLAY_ICON_Y+6, COL_GREEN);
    } else {
        // Ikona PAUSE (dwa prostokąty)
        canvas->fillRect(PLAY_ICON_X, PLAY_ICON_Y, 5, 18, COL_RED);
        canvas->fillRect(PLAY_ICON_X+8, PLAY_ICON_Y, 5, 18, COL_RED);
    }

    // ✅ Przyciski głośności +/-
    canvas->fillRect(VOL_MINUS_X, VOL_TOUCH_Y, VOL_BTN_SIZE, VOL_BTN_SIZE, COL_BG_CARD);
    canvas->drawRect(VOL_MINUS_X, VOL_TOUCH_Y, VOL_BTN_SIZE, VOL_BTN_SIZE, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(2);
    canvas->setCursor(VOL_MINUS_X + 2, VOL_TOUCH_Y);
    canvas->print("-");
    
    canvas->fillRect(60, VOL_TOUCH_Y, VOL_BTN_SIZE, VOL_BTN_SIZE, COL_BG_CARD);
    canvas->drawRect(60, VOL_TOUCH_Y, VOL_BTN_SIZE, VOL_BTN_SIZE, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(2);
    canvas->setCursor(60 + 2, VOL_TOUCH_Y);
    canvas->print("+");

    // ✅ Godzina
    struct tm ti;
    if (getLocalTime(&ti)) {
        canvas->setTextColor(COL_TEXT);
        canvas->setTextSize(2);
        char ts[6]; 
        strftime(ts, 6, "%H:%M", &ti);
        canvas->setCursor(130, PLAY_ICON_Y);
        canvas->print(ts);
    }

    // ✅ Kontrola jasności tak samo jak w radiu
    const int brIconX = 200, brIconY = BR_ICON_Y, brIconW = BR_ICON_SIZE, brIconH = BR_ICON_SIZE;
    const int brIconGap = BR_ICON_GAP;
    const int brMinusIconX = brIconX;
    const int brPlusIconX = brIconX + brIconW + brIconGap;
    const int brTextX = BR_TEXT_X;

    // Ikona czarne słońce (zmniejsz jasność)
    canvas->fillCircle(brMinusIconX + brIconW/2, brIconY + brIconH/2, 6, COL_BG);
    canvas->drawCircle(brMinusIconX + brIconW/2, brIconY + brIconH/2, 6, COL_BG);
    // Promienie czarne słońce
    for (int i = 0; i < 8; i++) {
        float angle = i * PI / 4;
        int x1 = brMinusIconX + brIconW/2 + cos(angle) * 8;
        int y1 = brIconY + brIconH/2 + sin(angle) * 8;
        int x2 = brMinusIconX + brIconW/2 + cos(angle) * 11;
        int y2 = brIconY + brIconH/2 + sin(angle) * 11;
        canvas->drawLine(x1, y1, x2, y2, COL_BG);
    }

    // Ikona białe słońce (zwiększ jasność)
    canvas->fillCircle(brPlusIconX + brIconW/2, brIconY + brIconH/2, 6, COL_TEXT);
    canvas->drawCircle(brPlusIconX + brIconW/2, brIconY + brIconH/2, 6, COL_TEXT);
    // Promienie białe słońce
    for (int i = 0; i < 8; i++) {
        float angle = i * PI / 4;
        int x1 = brPlusIconX + brIconW/2 + cos(angle) * 8;
        int y1 = brIconY + brIconH/2 + sin(angle) * 8;
        int x2 = brPlusIconX + brIconW/2 + cos(angle) * 11;
        int y2 = brIconY + brIconH/2 + sin(angle) * 11;
        canvas->drawLine(x1, y1, x2, y2, COL_TEXT);
    }

    // Wartość liczbowa jasności
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(1);
    canvas->setCursor(brTextX, 10);
    canvas->printf("%d%%", (br * 100) / 255);

    // Obszar wykresu z marginesami na osie
    // Oś Y (temperatura) po lewej: 40px margines
    // Oś X (czas) na dole: 18px margines
    const int chartX = 35, chartY = 40, chartW = 268, chartH = 140;

    // Tło wykresu (inwertowane: 0x0841 -> 0xF7BE)
    canvas->fillRect(chartX, chartY, chartW, chartH, 0xF7BE);

    // Linie siatki poziome (temperatura) - 5 poziomów
    float range = maxT - minT;
    int tempSteps = 5;
    canvas->setTextSize(1);
    canvas->setTextColor(COL_TEXT_SEC);
    for (int i = 0; i <= tempSteps; i++) {
        float tempVal = minT + (range * i / tempSteps);
        int yPos = chartY + chartH - (i * chartH / tempSteps);
        // Linia siatki
        canvas->drawFastHLine(chartX, yPos, chartW, COL_GRID);
        // Etykieta temperatury po lewej
        canvas->setCursor(2, yPos - 4);
        canvas->printf("%.0f", tempVal);
        // Ręcznie narysowany znaczek stopni (3x3px, pusty środek)
        int degX = canvas->getCursorX();
        int degY = yPos - 4;
        canvas->drawRect(degX, degY, 3, 3, COL_TEXT_SEC);
        canvas->drawPixel(degX + 1, degY + 1, 0xF7BE);
    }

    // Linie siatki pionowe (czas) - co 12h = 6 linii na 72h
    canvas->setTextColor(COL_TEXT_SEC);
    for (int i = 0; i <= 6; i++) {
        int xPos = chartX + (i * chartW / 6);
        canvas->drawFastVLine(xPos, chartY, chartH, COL_GRID);
        // Etykieta czasu na dole
        int hoursAgo = 72 - (i * 12);
        int labelHour;
        if (getLocalTime(&ti)) {
            labelHour = (ti.tm_hour - hoursAgo % 24 + 24) % 24;
            // Poprawka dnia: jeśli hoursAgo > tm.hour, przesuń o pełne dni
            int dayOffset = hoursAgo / 24;
            // Proste: pokaż godzinę względną
            int absHour = ti.tm_hour - (hoursAgo % 24);
            if (absHour < 0) absHour += 24;
            labelHour = absHour;
        } else {
            labelHour = (72 - hoursAgo) % 24;
        }
        canvas->setCursor(xPos - 8, chartY + chartH + 3);
        canvas->printf("%02d:00", labelHour);
        
        // ✅ Wyświetl datę dla NAJWCZESNIEJSZEJ godziny danego dnia
        static int lastShownDay = -1;
        if (i == 0) lastShownDay = -1; // Reset na początku pętli

        // Oblicz jaki to dzień
        time_t nowTime = time(NULL);
        time_t labelTime = nowTime - hoursAgo * 3600;
        struct tm labelTi;
        localtime_r(&labelTime, &labelTi);
        
        // Jeżeli to pierwszy znacznik tego dnia który widzimy na wykresie
        if (lastShownDay != labelTi.tm_mday) {
            lastShownDay = labelTi.tm_mday;
            canvas->setTextColor(COL_TEXT_DIM);
            canvas->setTextSize(1);
            canvas->setCursor(xPos - 7, chartY + chartH + 13);
            canvas->printf("%02d", labelTi.tm_mday);
        }
    }

    // Rysowanie krzywej temperatury (wygładzona spline z anty-aliasingiem)
    canvas->setTextColor(COL_CYAN);
    // Interpoluj spline między punktami dla gładszej krzywej
    int prevX = -1, prevY = -1;
    uint16_t bgColor = 0xF7BE; // Tło wykresu
    uint16_t midColor1 = blendColors(COL_CYAN, bgColor, 0.5f); // 50% blend
    uint16_t midColor2 = blendColors(COL_CYAN, bgColor, 0.25f); // 25% blend

    for (int i = 0; i < 71; i++) {
        int idx0 = (i - 1 < 0) ? 0 : i - 1;
        int idx3 = (i + 2 > 72) ? 72 : i + 2;
        float p0 = temps[idx0];
        float p1 = temps[i];
        float p2 = temps[i + 1];
        float p3 = temps[idx3];

        // Rysuj krótkie segmenty między punktami dla gładkości
        for (float t = 0; t < 1; t += 0.1) {
            float temp = catmullRom(p0, p1, p2, p3, t);
            int x = chartX + (i + t) * (chartW / 72.0);
            int y = (chartY + chartH) - ((temp - minT) / range * chartH);

            // Rysuj wygładzoną linię od poprzedniego punktu do obecnego
            if (prevX >= 0) {
                drawSmoothLine(canvas, prevX, prevY, x, y, COL_CYAN); // Główna linia
                // Grubsza linia - dodatkowe linie równoległe z blendowaniem
                drawSmoothLine(canvas, prevX, prevY + 1, x, y + 1, midColor1); // 50% blend
                drawSmoothLine(canvas, prevX, prevY - 1, x, y - 1, midColor1); // 50% blend
                drawSmoothLine(canvas, prevX, prevY + 2, x, y + 2, midColor2); // 25% blend
                drawSmoothLine(canvas, prevX, prevY - 2, x, y - 2, midColor2); // 25% blend
            }
            prevX = x;
            prevY = y;
        }
    }

    // ✅ DUŻA AKTUALNA TEMPERATURA NA ŚRODKU WYKRESU
    float currentTemp = temps[72];
    // Idealny środek obszaru wykresu
    const int centerX = chartX + (chartW / 2);
    const int centerY = chartY + (chartH / 2);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(4);
    // Wyśrodkowanie - każdy znak ma 24px szerokości w size 4
    int textLen = String(currentTemp, 1).length();
    int textX = centerX - (textLen * 12);
    canvas->setCursor(textX, centerY - 16);
    canvas->printf("%.1f", currentTemp);
    // Ręcznie narysowany znaczek stopni w stylu fontu size 4 (6x6px)
    int degX = canvas->getCursorX();
    int degY = centerY - 16 + 2;
    canvas->drawRect(degX, degY, 6, 6, COL_TEXT);
    canvas->drawFastHLine(degX + 1, degY + 1, 4, COL_TEXT);
    canvas->drawFastHLine(degX + 1, degY + 4, 4, COL_TEXT);
    canvas->drawFastVLine(degX + 1, degY + 1, 4, COL_TEXT);
    canvas->drawFastVLine(degX + 4, degY + 1, 4, COL_TEXT);
    // Literka C po stopniu
    canvas->setCursor(degX + 10, centerY - 16);
    canvas->print("C");

    // Aktualna temperatura (ostatni punkt = teraz)
    int curY = (chartY + chartH) - ((currentTemp - minT) / range * chartH);
    canvas->fillCircle(chartX + chartW, curY, 3, COL_ORANGE);

    // Pasek nawigacyjny na dole
    canvas->fillRect(0, 210, 320, 30, COL_NAV_BG);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(1);
    canvas->setCursor(5, 218);
    canvas->printf("B:%d%%", (br * 100) / 255);
    canvas->setCursor(80, 218);
    canvas->print("< DOTKNIJ DLA RADIO >");
    canvas->setCursor(270, 218);
    canvas->printf("V:%d", vol);
}

#endif
