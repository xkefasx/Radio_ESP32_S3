#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// --- GŁÓWNE PRZEŁĄCZNIKI LOGÓW ---
#define DEBUG_ENABLED           true   // Master switch - całkowite włączenie/wyłączenie debugowania
#define LOG_ENABLE_SYSTEM       true   // Start systemu, WiFi, IP, błędy krytyczne

#define LOG_ENABLE_AUDIO_CMD    true   // Komendy AUDIO_CMD (PLAY, STOP, VOLUME) - zdarzenia
#define LOG_ENABLE_AUDIO_HEALTH false  // Zdrowie audio i statystyki bufora (co 10s) - okresowe

#define LOG_ENABLE_ENC_EVENTS   true   // Obrót i przyciski enkoderów - zdarzenia
#define LOG_ENABLE_ENC_PERIODIC false  // Diagnostyka stanu/pinów enkoderów (co 2-8s) - okresowe

#define LOG_ENABLE_PLAYLIST     false  // Szczegółowe parsowanie playlist .pls/.m3u (bardzo dużo tekstu)
#define LOG_ENABLE_TOUCH        false  // Zdarzenia dotykowe EKRANU (tap, scroll)
#define LOG_ENABLE_TELEMETRY    true   // Raporty TLM (pamięć, CPU) co 10s - okresowe
#define LOG_ENABLE_DIAG_TIMING  false  // Timingi pętli i opóźnienia systemu - diagnostyka

#define LOG_BUFFER_LINES 20
#define LOG_LINE_LENGTH 80

// Deklaracje zewnętrzne zmiennych (bez WiFi.h i std::atomic w nagłówku)
// aby uniknąć konfliktów lintera/kompilatora w nagłówkach .h
extern bool getAudioPlaying();
extern uint32_t getAudioBufferFilled();
extern uint32_t getAudioBufferFree();
extern uint32_t getAudioBitrate();
extern int getWiFiRSSI();
extern String getWiFiIP();

static unsigned long lastReportTime = 0;
static unsigned long cpuWorkTime = 0;
static unsigned long cpuLoopStart = 0;

// Circular log buffer for on-screen display
static char logBuffer[LOG_BUFFER_LINES][LOG_LINE_LENGTH];
static int logBufferIndex = 0;
static int logBufferCount = 0;

inline void writeToLogBuffer(const char* logLine) {
    strncpy(logBuffer[logBufferIndex], logLine, LOG_LINE_LENGTH - 1);
    logBuffer[logBufferIndex][LOG_LINE_LENGTH - 1] = '\0';
    logBufferIndex = (logBufferIndex + 1) % LOG_BUFFER_LINES;
    if (logBufferCount < LOG_BUFFER_LINES) logBufferCount++;
}

inline void begin(unsigned long baud = 115200) {
    if (DEBUG_ENABLED && LOG_ENABLE_SYSTEM) {
        Serial.begin(baud);
        delay(1000);
        Serial.println("\n--- [SYSTEM MONITOR ACTIVATED] ---");
    }
}

inline void info(String tag, String msg) {
    if (!DEBUG_ENABLED) return;
    
    // Decyzja o wyświetleniu na podstawie kategorii
    bool enabled = LOG_ENABLE_SYSTEM;
    if (tag == "AUDIO") enabled = LOG_ENABLE_AUDIO_CMD;
    else if (tag == "PLAYLIST") enabled = LOG_ENABLE_PLAYLIST;

    if (enabled) {
        String logLine = "[" + String(millis()/1000) + "][INFO][" + tag + "] " + msg;
        Serial.println(logLine);
        // Do bufora ekranowego trafiają tylko logi systemowe i błędy
        if (tag != "PLAYLIST") writeToLogBuffer(logLine.c_str());
    }
}

inline void error(String tag, String msg) {
    if (DEBUG_ENABLED) {
        String logLine = "[" + String(millis()/1000) + "][ERROR][" + tag + "] " + msg;
        Serial.println(logLine);
        writeToLogBuffer(logLine.c_str());
    }
}

inline void debugCore(String tag, String msg) {
    if (DEBUG_ENABLED && LOG_ENABLE_SYSTEM) {
        int core = xPortGetCoreID();
        Serial.printf("[%lu][CORE%d][%s] %s\n", millis()/1000, core, tag.c_str(), msg.c_str());
    }
}

inline void logAudioCmd(String cmdType, String details = "") {
    if (DEBUG_ENABLED && LOG_ENABLE_AUDIO_CMD) {
        int core = xPortGetCoreID();
        Serial.printf("[%lu][CORE%d][AUDIO_CMD] %s %s\n", millis()/1000, core, cmdType.c_str(), details.c_str());
    }
}

inline void logTouchEvent(String event, int tx, int ty, int duration = 0) {
    if (DEBUG_ENABLED && LOG_ENABLE_TOUCH) {
        Serial.printf("[%lu][TOUCH] %s at(%d,%d) dur:%dms\n", millis()/1000, event.c_str(), tx, ty, duration);
    }
}

inline void logStateChange(String component, String oldState, String newState) {
    bool enabled = (component == "AUDIO") ? LOG_ENABLE_AUDIO_CMD : LOG_ENABLE_SYSTEM;
    if (DEBUG_ENABLED && enabled) {
        Serial.printf("[%lu][STATE][%s] %s -> %s\n", millis()/1000, component.c_str(), oldState.c_str(), newState.c_str());
    }
}

// --- NOWE FUNKCJE SPECJALISTYCZNE ---

inline void logAudioHealth(const char* state, bool conn, uint8_t buf, uint32_t filled, uint32_t free, uint32_t br, 
                         unsigned long calls, unsigned long avg, unsigned long max, unsigned long q, unsigned long loops) {
    if (DEBUG_ENABLED && LOG_ENABLE_AUDIO_HEALTH) {
        Serial.printf("[AUDIO_HEALTH] state:%s conn:%d buf:%u%% filled:%u free:%u br:%u calls/s:%lu avgLoop:%lu us maxLoop:%lu us qDepth:%lu loops/s:%lu\n",
                     state, conn, buf, filled, free, br, calls, avg, max, q, loops);
    }
}

inline void logAudioUnderrun(uint8_t buf, uint8_t prev, uint32_t br, unsigned long gap, unsigned long loop, unsigned long q, bool conn) {
    if (DEBUG_ENABLED && LOG_ENABLE_AUDIO_HEALTH) {
        Serial.printf("[AUDIO_UNDERRUN_CAUSE] buf:%u%% prev:%u%% br:%u loopGap:%lu us loopTime:%lu us qDepth:%lu connecting:%d\n",
                     buf, prev, br, gap, loop, q, conn);
    }
}

inline void logConnectTimeline(const char* stage, unsigned long time, unsigned long total = 0, unsigned long max = 0) {
    if (DEBUG_ENABLED && LOG_ENABLE_AUDIO_HEALTH) {
        if (total == 0)
            Serial.printf("[AUDIO_CONNECT_TIMELINE] stage:%s done:%lu ms\n", stage, time);
        else
            Serial.printf("[AUDIO_CONNECT_TIMELINE] stage:%s done:%lu ms total:%lu ms max:%lu ms\n", stage, time, total, max);
    }
}

inline void logPlaylist(const char* msg, const char* detail = "") {
    if (DEBUG_ENABLED && LOG_ENABLE_PLAYLIST) {
        if (detail[0] == '\0') Serial.printf("[PLAYLIST] %s\n", msg);
        else Serial.printf("[PLAYLIST] %s: %s\n", msg, detail);
    }
}

inline void logPlaylistF(const char* format, ...) {
    if (DEBUG_ENABLED && LOG_ENABLE_PLAYLIST) {
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print("[PLAYLIST] ");
        Serial.println(buf);
    }
}

inline void logRadioConnect(const char* format, ...) {
    if (DEBUG_ENABLED && LOG_ENABLE_AUDIO_CMD) {
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print("[RADIO_CONNECT] ");
        Serial.println(buf);
    }
}

inline void logRadioAction(const char* format, ...) {
    if (DEBUG_ENABLED && LOG_ENABLE_SYSTEM) {
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print("[RADIO_ACTION] ");
        Serial.println(buf);
    }
}

inline void logEncEvent(const char* format, ...) {
    if (DEBUG_ENABLED && LOG_ENABLE_ENC_EVENTS) {
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print(buf);
    }
}

inline void logEncPeriodic(const char* format, ...) {
    if (DEBUG_ENABLED && LOG_ENABLE_ENC_PERIODIC) {
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print(buf);
    }
}

inline void logTiming(const char* tag, unsigned long maintain, unsigned long touch) {
    if (DEBUG_ENABLED && LOG_ENABLE_DIAG_TIMING) {
        Serial.printf("[CORE1][TIMING][%s] Maintain:%lu Touch:%lu us\n", tag, maintain, touch);
    }
}

inline void trackCPUStart() {
    cpuLoopStart = micros();
}

inline void trackCPUEnd() {
    unsigned long now = micros();
    if (cpuLoopStart > 0 && now > cpuLoopStart) {
        cpuWorkTime += (now - cpuLoopStart);
    }
}

inline float getCPUUsage() {
    unsigned long now = millis();
    unsigned long intervalMs = (lastReportTime == 0) ? 10000 : (now - lastReportTime);
    if (intervalMs == 0) intervalMs = 1;
    float cpuLoad = (cpuWorkTime / (intervalMs * 1000.0)) * 100.0;
    if (cpuLoad > 100.0) cpuLoad = 100.0;
    return cpuLoad;
}

inline void reportStatus(TaskHandle_t audioTaskHandle) {
    if (!DEBUG_ENABLED || !LOG_ENABLE_TELEMETRY) return;
    unsigned long now = millis();
    
    if (now - lastReportTime >= 10000) {
        size_t sramFree = ESP.getFreeHeap() / 1024;
        size_t psramTotal = ESP.getPsramSize() / 1024;
        size_t psramFree = ESP.getFreePsram() / 1024;
        
        bool playing = getAudioPlaying();
        uint32_t bufFilled = getAudioBufferFilled();
        uint32_t bufFree = getAudioBufferFree();
        uint32_t br = getAudioBitrate();
        
        if (bufFree > 10000000) bufFree = 0;
        if (bufFilled > 10000000) bufFilled = 0;
        
        unsigned long stackRemaining = uxTaskGetStackHighWaterMark(audioTaskHandle);
        int rssi = getWiFiRSSI();
        String ip = getWiFiIP();
        
        float cpuLoad = getCPUUsage();

        uint32_t bufTotal = bufFilled + bufFree;
        uint32_t bufPercent = bufTotal > 0 ? (bufFilled * 100) / bufTotal : 0;
        uint32_t bufFilledKB = bufFilled / 1024;
        uint32_t bufFreeKB = bufFree / 1024;

        // Zwięzły, czytelny log telemetryczny (2 linie)
        Serial.printf("[TLM %lus] WiFi %ddBm %s | Audio %s %ukbps | Buf %u%% (%uKB/%uKB)\n",
                      now / 1000,
                      rssi,
                      ip.c_str(),
                      playing ? "GRANE" : "STOP",
                      br / 1000,
                      bufPercent,
                      bufFilledKB,
                      bufFreeKB);

        Serial.printf("[TLM] MEM RAM free: %uKB | PSRAM free: %uKB (total: %uKB) | AudioStack free: %luB | UI: %.1f%%\n",
                      (uint32_t)sramFree,
                      (uint32_t)psramFree,
                      (uint32_t)psramTotal,
                      (unsigned long)stackRemaining,
                      cpuLoad);
        
        if (stackRemaining < 2000) {
            Serial.printf("[WARN] AudioStack low: %luB\n", (unsigned long)stackRemaining);
        }

        if (bufPercent < 10) {
            Serial.printf("[WARN] Audio buffer low: %u%%\n", bufPercent);
        }
        
        cpuWorkTime = 0;
        lastReportTime = now;
    }
}

inline void drawLogScreen(Arduino_Canvas *canvas) {
    canvas->fillScreen(0x0000); // Black background

    canvas->setTextColor(0xFFFF); // White text
    canvas->setTextSize(1);
    canvas->setCursor(5, 5);
    canvas->print("STARTUP LOGS:");

    int y = 20;
    int startIdx = (logBufferIndex - logBufferCount + LOG_BUFFER_LINES) % LOG_BUFFER_LINES;

    for (int i = 0; i < logBufferCount && i < 12; i++) {
        int idx = (startIdx + i) % LOG_BUFFER_LINES;
        canvas->setCursor(5, y);
        canvas->print(logBuffer[idx]);
        y += 15;
    }

    // Bottom status bar
    canvas->fillRect(0, 210, 320, 30, 0x4208); // Dark gray
    canvas->setTextColor(0xFFFF);
    canvas->setCursor(10, 218);
    canvas->print("Initializing...");
}

#endif
