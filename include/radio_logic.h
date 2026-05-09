#ifndef RADIO_LOGIC_H
#define RADIO_LOGIC_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "logger.h"

// Forward declaration of isConnected to avoid circular include if any
bool isConnected();

#include <Audio.h>

extern QueueHandle_t audioQueue;

enum StreamQuality { Q_LQ, Q_MQ, Q_HQ, Q_SSL };

struct RadioStream {
    char url[256];
    StreamQuality quality;
    char label[32];
};

struct RadioStation {
    char name[64];
    RadioStream streams[3]; 
    int streamCount;
    bool isTuba;           
};

const RadioStation STATIONS[] = {
    {"RMF FM",      {{"http://stream11.radiostream.pl/tuba1-1.mp3",    Q_HQ, "192 MP3"}}, 1, true},
    {"Radio ZET",   {{"http://stream11.radiostream.pl/tuba2-1.mp3",    Q_HQ, "224 MP3"}}, 1, true},
    {"Radio 357",   {{"http://stream.radio357.pl/m3u8",   Q_HQ, "224 AAC"}}, 1, true},
    {"Złote Przeboje", {{"http://stream11.radiostream.pl/tuba3-1.mp3", Q_HQ, "192 MP3"}}, 1, true},
    {"Antyradio",   {{"http://an01.cdn.eurozet.pl/ant-waw.mp3",    Q_HQ, "128 MP3"}}, 1, true},
    {"TOK FM",      {{"http://stream30.radiostream.pl/tuba10-1.mp3",   Q_MQ, "128 MP3"}}, 1, true},
    {"Eska Rock",   {{"http://stream11.radiostream.pl/tuba8-1.mp3",    Q_HQ, "192 MP3"}}, 1, true},
    {"TOK FM 2",      {{"https://stream30.radiostream.pl/tuba10-1.mp3",   Q_MQ, "128 MP3"}}, 1, true},
};

const int TOTAL_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);
extern Audio audio;

static int scrollY = 0;
static int lastTouchY = -1;
static int activeIdx = -1;
static bool showOnlyTuba = false; 
static StreamQuality qualityFilter = Q_MQ; 
const int itemH = 50;

// ==============================================
// 🔧 KONFIGURACJA GÓRNEJ BELKI - WSZYSTKO TU!
// ==============================================
const int HEADER_H             = 40;

const int PLAY_ICON_X          = 8;
const int PLAY_ICON_Y          = 16;

const int STATION_NAME_X       = 8;
const int STATION_NAME_Y       = 4;

const int VOL_BTN_SIZE         = 16;
const int VOL_MINUS_X          = 35;
const int VOL_BAR_X            = VOL_MINUS_X + VOL_BTN_SIZE + 5;
const int VOL_BAR_Y            = 25;
const int VOL_BAR_W            = 90;
const int VOL_BAR_H            = 7;
const int VOL_PLUS_X           = VOL_BAR_X + VOL_BAR_W + 5;

const int BR_ICON_X            = VOL_PLUS_X + VOL_BTN_SIZE + 45;
const int BR_ICON_Y            = 8;
const int BR_ICON_SIZE         = 18;
const int BR_ICON_GAP          = 25;
const int BR_TEXT_X            = BR_ICON_X + BR_ICON_SIZE + BR_ICON_GAP + BR_ICON_SIZE + 15;
const int BR_TEXT_Y            = 10;

const int VOL_MINUS_TOUCH_X    = VOL_MINUS_X;
const int VOL_PLUS_TOUCH_X     = VOL_PLUS_X;
const int VOL_TOUCH_Y          = VOL_BAR_Y - 6;
// ==============================================

// Audio connection state tracking
static bool isConnecting = false;
static unsigned long lastConnectionAttempt = 0;
static const unsigned long CONNECTION_COOLDOWN = 3000; // MIN 3 sekundy między próbami SSL
static const unsigned long CONNECTION_TIMEOUT = 8000;  // fallback reset flagi

int getBestStreamForStation(int stationIdx);

String extractStreamFromPlaylist(const char* playlistUrl) {
    if (!isConnected()) {
        error("PLAYLIST", "No WiFi - skipping playlist extraction");
        return playlistUrl ? String(playlistUrl) : "";
    }

    HTTPClient http;
    String result = playlistUrl; // Default: return original URL if parsing fails

    if (!playlistUrl) {
        logPlaylist("ERROR: NULL playlist URL");
        return result;
    }

    logPlaylist("Input URL", playlistUrl);

    // Check if URL contains .pls or .m3u (handles query parameters)
    String url = String(playlistUrl);
    bool isPLS = url.indexOf(".pls") >= 0;
    bool isM3U = url.indexOf(".m3u") >= 0;

    if (!isPLS && !isM3U) {
        logPlaylist("Not a playlist (.pls/.m3u), returning as-is");
        return result; // Not a playlist, return as-is
    }

    logPlaylistF("Detected %s format, fetching...", isPLS ? "PLS" : "M3U");

    if (http.begin(playlistUrl)) {
        http.setTimeout(5000); // ms
        logPlaylist("HTTP connection started");

        esp_task_wdt_reset(); // przed potencjalnie blokującym GET
        int httpCode = http.GET();
        esp_task_wdt_reset(); // po GET

        logPlaylistF("HTTP response code: %d", httpCode);
        
        if (httpCode == 200) {
            String payload = http.getString();
            logPlaylistF("Fetched %d bytes", payload.length());
            logPlaylistF("Payload preview: %s", payload.substring(0, 100).c_str());

            // Parse .pls format
            if (isPLS) {
                int fileIdx = payload.indexOf("File1=");
                logPlaylistF("PLS: File1= at index %d", fileIdx);
                if (fileIdx != -1) {
                    int endIdx = payload.indexOf("\n", fileIdx);
                    if (endIdx != -1) {
                        result = payload.substring(fileIdx + 6, endIdx);
                        result.trim();
                        logPlaylist("Parsed PLS", result.c_str());
                    } else {
                        logPlaylist("PLS ERROR: No newline after File1=");
                    }
                } else {
                    logPlaylist("PLS ERROR: File1= not found");
                }
            }
            // Parse .m3u format
            else if (isM3U) {
                // Find first http:// or https:// line
                int httpIdx = payload.indexOf("http");
                logPlaylistF("M3U: http at index %d", httpIdx);
                if (httpIdx != -1) {
                    int endIdx = payload.indexOf("\n", httpIdx);
                    if (endIdx != -1) {
                        result = payload.substring(httpIdx, endIdx);
                        result.trim();
                        logPlaylist("Parsed M3U", result.c_str());
                    } else {
                        logPlaylist("M3U ERROR: No newline after http");
                    }
                } else {
                    logPlaylist("M3U ERROR: http not found");
                }
            }
        } else {
            logPlaylistF("HTTP error: %d", httpCode);
        }
        http.end();
        logPlaylist("HTTP connection closed");
    } else {
        logPlaylist("ERROR: Failed to begin HTTP");
    }

    logPlaylist("Final result", result.c_str());
    return result;
}

inline bool canAttemptConnection() {
    unsigned long now = millis();
    if (isConnecting && (now - lastConnectionAttempt < CONNECTION_TIMEOUT)) return false;
    if (now - lastConnectionAttempt < CONNECTION_COOLDOWN) return false;
    return true;
}

inline void startStationConnection(int stationIdx) {
    if (!isConnected()) {
        error("RADIO", "Cannot connect - No WiFi!");
        return;
    }

    logRadioConnect("Starting connection for station %d (%s)", stationIdx, STATIONS[stationIdx].name);
    
    int sIdx = getBestStreamForStation(stationIdx);
    logRadioConnect("Selected stream index %d", sIdx);
    
    isConnecting = true;
    lastConnectionAttempt = millis();
    
    const char* playlistUrl = STATIONS[stationIdx].streams[sIdx].url;
    logRadioConnect("Original URL: %s", playlistUrl);

    // NOTE: Playlist parsing moved to Core 0 (Audio task) to keep UI fully responsive.
    // Core 1 now only sends connection intent + source URL.
    
    AudioCommand cmd;
    cmd.type = AUDIO_CMD_CONNECT;
    strncpy(cmd.data.url, playlistUrl, 255);
    cmd.data.url[255] = '\0';  // Ensure null termination
    cmd.data.volume = 0;
    
    logRadioConnect("Sending CONNECT command to audio queue");
    xQueueSend(audioQueue, &cmd, 0);
    logRadioConnect("CONNECT command sent successfully");
}

inline void maintainConnectionState() {
    if (isConnecting && (millis() - lastConnectionAttempt >= CONNECTION_TIMEOUT)) {
        isConnecting = false;
    }
    if (audioPlaying.load()) {
        isConnecting = false;
    }
}

void setupAudio() {
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    
    // Wymuszenie użycia PSRAM dla buforów audio - KLUCZOWE dla płynności!
    if (ESP.getPsramSize() > 0) {
        // Rezerwujemy 1.5MB na bufor (S3 spokojnie to udźwignie)
        audio.setBufsize(1024*1024, 1024*256); 
        info("AUDIO", "PSRAM buffer: 1.MB initialized");
    }
    
    
    audio.setConnectionTimeout(5000, 2);  // 5s timeout, 2 retries - szybsze fail przy problemach
    audio.setVolume(3);
}

int getBestStreamForStation(int stationIdx) {
    for(int i = 0; i < STATIONS[stationIdx].streamCount; i++) {
        if(STATIONS[stationIdx].streams[i].quality == qualityFilter) return i;
    }
    return 0;
}

void handleRadioScroll(int ty, bool isTouched) {
    if (!isTouched) { lastTouchY = -1; return; }
    if (lastTouchY != -1) {
        int delta = ty - lastTouchY;
        scrollY += delta;
        int visibleCount = 0;
        for(int i = 0; i < TOTAL_STATIONS; i++) if(!showOnlyTuba || STATIONS[i].isTuba) visibleCount++;
        int minScroll = -(visibleCount * itemH - 160 - 50); // Add buffer to ensure at least 3 stations visible
        // Zablokuj przewijanie w górę powyżej Y=40 (górny pasek)
        // scrollY = 0 oznacza początek listy na Y=40
        // scrollY > 0 oznacza przewijanie w górę (zasłania pasek) - ZABLOKOWANE
        if (scrollY > 0) scrollY = 0;
        if (scrollY < minScroll) scrollY = minScroll;
    }
    lastTouchY = ty;
}

// Współrzędne ikon jasności - AUTOMATYCZNIE Z KONFIGURACJI!
const int brIconX = BR_ICON_X;
const int brIconY = BR_ICON_Y;
const int brIconW = BR_ICON_SIZE;
const int brIconH = BR_ICON_SIZE;
const int brMinusIconX = BR_ICON_X;
const int brPlusIconX = BR_ICON_X + BR_ICON_SIZE + BR_ICON_GAP;

extern int brightness; // Zmienna globalna z main.cpp
extern unsigned long lastTouchAction; // Zmienna globalna z main.cpp

void handleRadioBrightness(int tx, int ty, unsigned long now, unsigned long touchDuration) {
    const unsigned long TOUCH_DEBOUNCE_MS = 50;
    static bool brMinusPressed = false;
    static bool brPlusPressed = false;
    static unsigned long lastBrChange = 0;
    const unsigned long BR_REPEAT_DELAY = 100; // ms między zmianami jasności przy przytrzymaniu

    bool touchingBrMinus = (tx >= brMinusIconX && tx <= brMinusIconX + brIconW && ty >= brIconY && ty <= brIconY + brIconH);
    bool touchingBrPlus = (tx >= brPlusIconX && tx <= brPlusIconX + brIconW && ty >= brIconY && ty <= brIconY + brIconH);

    if (touchingBrMinus && !brMinusPressed && (now - lastTouchAction > TOUCH_DEBOUNCE_MS)) {
        brMinusPressed = true;
        lastBrChange = now;
        // Logarytmiczna zmniejszanie jasności
        int newBr = (int)(brightness * 0.8);
        if (newBr < 1) newBr = 1;
        brightness = newBr;
        ledcWrite(0, brightness);
        lastTouchAction = now;
    } else if (touchingBrPlus && !brPlusPressed && (now - lastTouchAction > TOUCH_DEBOUNCE_MS)) {
        brPlusPressed = true;
        lastBrChange = now;
        // Logarytmiczne zwiększanie jasności
        int newBr = (int)(brightness * 1.2);
        if (newBr == brightness) newBr = brightness + 1; // Zapobiegaj utknięciu przy min
        if (newBr > 255) newBr = 255;
        brightness = newBr;
        ledcWrite(0, brightness);
        lastTouchAction = now;
    }

    // Przytrzymanie - ciągła zmiana jasności
    if ((brMinusPressed || brPlusPressed) && (now - lastBrChange >= BR_REPEAT_DELAY)) {
        if (brMinusPressed) {
            int newBr = (int)(brightness * 0.8);
            if (newBr < 1) newBr = 1;
            brightness = newBr;
            ledcWrite(0, brightness);
            lastBrChange = now;
        } else if (brPlusPressed) {
            int newBr = (int)(brightness * 1.2);
            if (newBr == brightness) newBr = brightness + 1; // Zapobiegaj utknięciu przy min
            if (newBr > 255) newBr = 255;
            brightness = newBr;
            ledcWrite(0, brightness);
            lastBrChange = now;
        }
    }

    // Reset stanu przycisków jasności
    if (!touchingBrMinus) brMinusPressed = false;
    if (!touchingBrPlus) brPlusPressed = false;
}

void drawRadioUI(Arduino_Canvas *canvas, int vol, int br, bool isPlaying, bool isConn) {
    canvas->fillScreen(COL_BG);

    // ===== GÓRNY PASEK STATUSU =====
    canvas->fillRect(0, 0, 320, HEADER_H, COL_BG_HEADER);

    // Status Play/Pause/Connecting
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


    // Nazwa aktywnej stacji (jeśli gra)
    canvas->setTextSize(1);
    if (activeIdx >= 0) {
        canvas->setTextColor(isPlaying ? COL_GREEN : COL_TEXT_SEC);
        canvas->setCursor(STATION_NAME_X, STATION_NAME_Y);
        canvas->print(STATIONS[activeIdx].name);
    }

    // Pasek głośności
    const int volBarX = VOL_BAR_X;
    const int volBarY = VOL_BAR_Y;
    const int volBarW = VOL_BAR_W;
    const int volBarH = VOL_BAR_H;
    const int volBtnW = VOL_BTN_SIZE;
    const int volBtnH = VOL_BTN_SIZE;
    
    // Przycisk - (minus)
    canvas->fillRect(VOL_MINUS_X, VOL_TOUCH_Y, volBtnW, volBtnH, COL_BG_CARD);
    canvas->drawRect(VOL_MINUS_X, VOL_TOUCH_Y, volBtnW, volBtnH, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(2);
    canvas->setCursor(VOL_MINUS_X + 3, VOL_TOUCH_Y + 1);
    canvas->print("-");
    
    // Przycisk + (plus)
    canvas->fillRect(VOL_PLUS_X, VOL_TOUCH_Y, volBtnW, volBtnH, COL_BG_CARD);
    canvas->drawRect(VOL_PLUS_X, VOL_TOUCH_Y, volBtnW, volBtnH, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(2);
    canvas->setCursor(VOL_PLUS_X + 3, VOL_TOUCH_Y + 1);
    canvas->print("+");
    
    // Pasek głośności
    canvas->fillRect(volBarX, volBarY, volBarW, volBarH, COL_VOL_BG);
    int volFill = (vol * volBarW) / 21;
    canvas->fillRect(volBarX, volBarY, volFill, volBarH, COL_VOL_BAR);
    canvas->drawRect(volBarX, volBarY, volBarW, volBarH, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT_SEC);
    canvas->setTextSize(1);
    canvas->setCursor(volBarX + volBarW / 2 - 10, volBarY - 1);
    canvas->printf("V%d", vol);

    // Kontrola jasności (prawa strona headera)
    const int brTextX = BR_TEXT_X;
    const int brTextY = BR_TEXT_Y;

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
    canvas->setCursor(brTextX, brTextY);
    canvas->printf("%d", br);

    // ===== LISTA STACJI =====
    int currentY = (HEADER_H + 5) + scrollY;
    int visibleCount = 0;
    for (int i = 0; i < TOTAL_STATIONS; i++) {
        if (showOnlyTuba && !STATIONS[i].isTuba) continue;
        // Tylko rysuj elementy które są w obszarze Y=40-210 (między headerem a nawigacją)
        if (currentY >= 40 && currentY < 210) {
            uint16_t bgColor, txtColor;
            if (i == activeIdx && isPlaying) {
                bgColor = COL_PLAYING_BG;   // ciemny zielony - gra
                txtColor = COL_GREEN;
            } else if (i == activeIdx && isConn) {
                bgColor = COL_CONNECT_BG;   // ciemny żółty - łączenie
                txtColor = COL_YELLOW;
            } else if (i == activeIdx) {
                bgColor = COL_BG_CARD;      // podświetlona wybrana
                txtColor = COL_TEXT;
            } else {
                bgColor = COL_BG;           // zwykła
                txtColor = COL_TEXT_SEC;
            }

            canvas->fillRect(5, currentY, 310, 40, bgColor);
            canvas->drawRect(5, currentY, 310, 40, COL_DIVIDER);

            // Tekst stacji
            canvas->setTextColor(txtColor);
            canvas->setTextSize(2);
            canvas->setCursor(15, currentY + 10);
            canvas->print(STATIONS[i].name);

            // Jakość streamu (mały tekst po prawej)
            canvas->setTextSize(1);
            canvas->setTextColor(COL_TEXT_DIM);
            int sIdx = getBestStreamForStation(i);
            canvas->setCursor(260, currentY + 14);
            canvas->print(STATIONS[i].streams[sIdx].label);

        }
        currentY += 45;
    }
    
    // ===== DOLNY PASEK NAWIGACJI =====
    canvas->fillRect(0, 210, 320, 30, COL_NAV_BG);
    canvas->drawFastHLine(0, 210, 320, COL_DIVIDER);
    canvas->setTextColor(COL_TEXT);
    canvas->setTextSize(1);
    canvas->setCursor(10, 218);
    canvas->print("< POGODA");
    canvas->setCursor(120, 218);
    canvas->print("2xTAP=PLAY");
    
    // Godzina w prawym rogu dolnego paska
    struct tm ti;
    if (getLocalTime(&ti)) {
        canvas->setTextColor(COL_TEXT);
        canvas->setTextSize(1);
        char ts[6]; 
        strftime(ts, 6, "%H:%M", &ti);
        canvas->setCursor(265, 218);
        canvas->print(ts);
    }
}

// Ta funkcja zastępuje getStationIndexAt i obsługuje filtry
void handleRadioActions(int tx, int ty) {
    maintainConnectionState();

    logRadioAction("Touch: tx=%d, ty=%d, scrollY=%d", tx, ty, scrollY);

    if (ty < 40) {
        if (tx < 95) { 
            // Toggle TUBA filter
            showOnlyTuba = !showOnlyTuba; 
            scrollY = 0; 
        }
        else if (tx < 185) { 
            // Quality filter buttons
            qualityFilter = (StreamQuality)((qualityFilter + 1) % 4); 
            if (activeIdx != -1) {
                if (!canAttemptConnection()) {
                    info("AUDIO", "Connection in progress or cooldown active");
                    return;
                }
                startStationConnection(activeIdx);
            }
        }
        return;
    }

    int currentY = 40 + scrollY;  // ZGODNE z drawRadioUI
    const int touchPadding = 8;  // Dodatkowe 8px tolerancji powyżej/poniżej elementu
    const int displayItemH = 45;  // ZGODNE z drawRadioUI (używa 45 zamiast itemH=50)
    for (int i = 0; i < TOTAL_STATIONS; i++) {
        if (showOnlyTuba && !STATIONS[i].isTuba) {
            continue;  // Pomiń niewidoczne stacje - NIE inkrementuj currentY (jak w drawRadioUI)
        }
        // Zwiększony obszar dotyku dla łatwiejszego zaznaczania
        if (ty > currentY - touchPadding && ty < currentY + displayItemH + touchPadding) {
            logRadioAction("Selected station idx=%d, name=%s, currentY=%d", i, STATIONS[i].name, currentY);
            activeIdx = i;
            if (!canAttemptConnection()) {
                info("AUDIO", "Connection in progress or cooldown active");
                return;
            }
            startStationConnection(i);
            return;
        }
        currentY += displayItemH;  // ZGODNE z drawRadioUI (45 zamiast itemH=50)
    }
    logRadioAction("No station selected for ty=%d, scrollY=%d", ty, scrollY);
}

// Funkcja zapewniająca że aktywna stacja jest zawsze widoczna w viewport
void ensureActiveStationVisible() {
    if (activeIdx < 0 || activeIdx >= TOTAL_STATIONS) return;

    // Policz pozycję Y aktywnej stacji (bez scrollY)
    int stationY = (HEADER_H + 5);
    for (int i = 0; i < activeIdx; i++) {
        if (showOnlyTuba && !STATIONS[i].isTuba) continue;
        stationY += 45;
    }

    // Uwzględnij scrollY żeby dostać faktyczną pozycję na ekranie
    int screenY = stationY + scrollY;

    // Jeśli powyżej viewport (Y < 40), przewiń w dół
    if (screenY < 40) {
        scrollY += (40 - screenY);
    }
    // Jeśli poniżej viewport (Y + 40 > 210), przewiń w górę
    else if (screenY + 40 > 210) {
        scrollY -= (screenY + 40 - 210);
    }
}
#endif