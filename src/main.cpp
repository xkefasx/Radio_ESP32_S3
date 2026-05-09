#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <Audio.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "wifi_logic.h"
#include "radio_logic.h"
#include "weather_logic.h"
#include "clock_screen.h"
#include "logger.h"
#include "config_manager.h"

// Forward declaration (setupAudio defined in radio_logic.h)
void setupAudio();

// Obiekty
Audio audio;
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
Arduino_GFX *display = new Arduino_ILI9341(bus, TFT_RST, 1, false);
Arduino_Canvas *canvas = new Arduino_Canvas(320, 240, display);

// Stan globalny
AppMode currentMode = MODE_WEATHER;
int currentVolume = 6;
int brightness = 150;
unsigned long lastWeatherUpdate = 0;
TaskHandle_t AudioTask;
QueueHandle_t audioQueue;
bool weatherLoaded = false;
unsigned long lastTouchAction = 0; // Globalna zmienna dla radio_logic.h
Preferences preferences;

// Statystyki i stan audio dla Rdzenia 1 (Atomic)
std::atomic<bool> audioPlaying{false};
std::atomic<uint32_t> audioBufferFilled{0};
std::atomic<uint32_t> audioBufferFree{0};
std::atomic<uint32_t> audioBitrate{0};
volatile bool audioInitialized = false;

// Akcesory dla loggera (Rdzeń 1)
bool getAudioPlaying() { return audioPlaying.load(); }
uint32_t getAudioBufferFilled() { return audioBufferFilled.load(); }
uint32_t getAudioBufferFree() { return audioBufferFree.load(); }
uint32_t getAudioBitrate() { return audioBitrate.load(); }

void beep(uint16_t freq, uint16_t ms) {
    ledcSetup(1, freq, 8);
    ledcAttachPin(I2S_BCLK, 1);
    ledcWrite(1, 128);

    pinMode(I2S_DOUT, OUTPUT);
    digitalWrite(I2S_DOUT, HIGH);
    
    delay(ms);
    
    digitalWrite(I2S_DOUT, LOW);
    ledcWrite(1, 0);
    ledcDetachPin(I2S_BCLK);

    // ✅ PRZYWRÓCENIE KONTROLI DO BIBLIOTEKI AUDIO
    pinMode(I2S_DOUT, INPUT);
    pinMode(I2S_BCLK, INPUT);
    pinMode(I2S_LRC, INPUT);
    
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
}

// Akcesory dla WiFi (Rdzeń 1)
int getWiFiRSSI() { return WiFi.RSSI(); }
String getWiFiIP() { return WiFi.localIP().toString(); }

static uint32_t hashUrl(const char* s) {
    uint32_t hash = 2166136261u;
    if (!s) return hash;
    while (*s) {
        hash ^= static_cast<uint8_t>(*s++);
        hash *= 16777619u;
    }
    return hash;
}

static bool validateAudioUrl(const char* url, String& reason) {
    if (!url) {
        reason = "NULL";
        return false;
    }
    size_t len = strnlen(url, 256);
    if (len == 0) {
        reason = "EMPTY";
        return false;
    }
    if (len >= 255) {
        reason = "TOO_LONG";
        return false;
    }
    bool isHttp = (strncmp(url, "http://", 7) == 0) || (strncmp(url, "https://", 8) == 0);
    if (!isHttp) {
        reason = "INVALID_SCHEME";
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = static_cast<unsigned char>(url[i]);
        if (c < 32 || c > 126) {
            reason = "NON_PRINTABLE";
            return false;
        }
    }
    return true;
}

// Zadanie dla Audio (Rdzeń 0)
void AudioLoopTask(void * pvParameters) {
    info("CORE", "Audio Task startuje na rdzeniu 0");
    AudioCommand cmd;
    
    // Watchdog już zainicjalizowany w setup() - tylko subskrypcja
    esp_task_wdt_add(NULL);
    
    static bool lastAudioState = false;
    static unsigned long cmdCount = 0;
    static unsigned long loopCount = 0;
    static unsigned long maxLoopTime = 0;
    static unsigned long maxConnectTime = 0;

    // Audio health telemetry
    static unsigned long lastHealthLog = 0;
    static unsigned long secLoopCount = 0;
    static unsigned long secAudioLoopCalls = 0;
    static unsigned long secAudioLoopUsAccum = 0;
    static unsigned long secAudioLoopUsMax = 0;
    static unsigned long lastUnderrunCauseLog = 0;
    static unsigned long lastAudioLoopCallUs = 0;
    static uint8_t lastBufferPercent = 0;
    
    for(;;) {
        unsigned long loopStart = micros();
        loopCount++;
        secLoopCount++;
        uint8_t bufferPercent = 0;
        uint32_t filled = 0;
        uint32_t free = 0;
        uint32_t bitrate = 0;
        unsigned long audioLoopTime = 0;
        unsigned long audioLoopGapUs = 0;
        
        // Odbierz komendę z kolejki jeśli jest dostępna (non-blocking)
        if(xQueueReceive(audioQueue, &cmd, 0) == pdTRUE) {
            cmdCount++;
            switch(cmd.type) {
                case AUDIO_CMD_CONNECT: {
                    String urlReason, finalReason;
                    if (!validateAudioUrl(cmd.data.url, urlReason)) {
                        error("AUDIO", "CONNECT rejected - invalid source URL");
                        break;
                    }

                    unsigned long timelineStart = millis();
                    logAudioCmd("CONNECT", String(cmd.data.url));

                    // Pierwszy CONNECT - zainicjalizuj audio (setPinout, setBufsize) tylko raz
                    if (!audioInitialized) {
                        info("AUDIO", "First CONNECT - initializing I2S");
                        setupAudio();
                        audioInitialized = true;
                    }

                    unsigned long resolveStart = millis();
                    String finalUrl = extractStreamFromPlaylist(cmd.data.url);
                    unsigned long resolveTime = millis() - resolveStart;

                    if (!validateAudioUrl(finalUrl.c_str(), finalReason)) {
                        error("AUDIO", "CONNECT rejected - invalid resolved URL");
                        break;
                    }

                    logConnectTimeline("RESOLVE", resolveTime);

                    // Reset WDT PRZED blokującym connecttohost — SSL handshake może trwać >5s
                    esp_task_wdt_reset();
                    unsigned long connectStart = millis();
                    audio.connecttohost(finalUrl.c_str());
                    unsigned long connectTime = millis() - connectStart;
                    unsigned long totalTimeline = millis() - timelineStart;
                    if (connectTime > maxConnectTime) maxConnectTime = connectTime;
                    logConnectTimeline("CONNECT", connectTime, totalTimeline, maxConnectTime);
                    break;
                }
                case AUDIO_CMD_STOP:
                    logAudioCmd("STOP", "");
                    if (audioInitialized) audio.stopSong();
                    break;
                case AUDIO_CMD_VOLUME:
                    // VOLUME log removed - too noisy when held
                    if (audioInitialized) audio.setVolume(cmd.data.volume);
                    break;
            }
        }
        
        // audio.loop() - agresywnie przy aktywnym audio, aby nie dopuścić do underrun
        bool currentState = false;
        
        if (audioInitialized) {
            filled = audio.inBufferFilled();
            free = audio.inBufferFree();
            uint32_t total = filled + free;
            bufferPercent = total > 0 ? (filled * 100) / total : 0;

            unsigned long audioStart = micros();
            if (lastAudioLoopCallUs > 0) {
                audioLoopGapUs = audioStart - lastAudioLoopCallUs;
            }
            audio.loop();
            lastAudioLoopCallUs = audioStart;

            currentState = audio.isRunning();
            audioLoopTime = micros() - audioStart;
            bitrate = audio.getBitRate();

            secAudioLoopCalls++;
            secAudioLoopUsAccum += audioLoopTime;
            if (audioLoopTime > secAudioLoopUsMax) secAudioLoopUsMax = audioLoopTime;
            if (audioLoopTime > maxLoopTime) maxLoopTime = audioLoopTime;

            // Event-driven log: jednoznaczny powód underrun
            if (currentState && bufferPercent < 5 && (millis() - lastUnderrunCauseLog > 1000)) {
                UBaseType_t qDepth = uxQueueMessagesWaiting(audioQueue);
                logAudioUnderrun(bufferPercent, lastBufferPercent, bitrate, audioLoopGapUs, audioLoopTime, 
                                static_cast<unsigned long>(qDepth), isConnecting);
                lastUnderrunCauseLog = millis();
            }
        }
        
        // Śledzenie zmiany stanu audio
        if (currentState != lastAudioState) {
            logStateChange("AUDIO", lastAudioState ? "PLAYING" : "STOP", currentState ? "PLAYING" : "STOP");
            lastAudioState = currentState;
        }
        
        // Aktualizacja stanów atomowych dla Rdzenia 1
        audioPlaying = currentState;
        if (audioInitialized) {
            audioBufferFilled = filled;
            audioBufferFree = free;
            audioBitrate = bitrate;
        }

        lastBufferPercent = bufferPercent;
        
        // Health log (zredukowany do co 10s)
        unsigned long now = millis();
        if (now - lastHealthLog >= 10000) {
            UBaseType_t qDepth = uxQueueMessagesWaiting(audioQueue);
            if (audioInitialized) {
                unsigned long avgLoopUs = secAudioLoopCalls > 0 ? (secAudioLoopUsAccum / secAudioLoopCalls) : 0;
                logAudioHealth(currentState ? "PLAYING" : "IDLE", isConnecting, bufferPercent, filled, free, bitrate,
                               secAudioLoopCalls, avgLoopUs, secAudioLoopUsMax, static_cast<unsigned long>(qDepth), secLoopCount);
            } else {
                logAudioHealth("WAIT_CONNECT", isConnecting, 0, 0, 0, 0, 0, 0, 0, static_cast<unsigned long>(qDepth), secLoopCount);
            }
            lastHealthLog = now;
            secLoopCount = 0;
            secAudioLoopCalls = 0;
            secAudioLoopUsAccum = 0;
            secAudioLoopUsMax = 0;
            maxLoopTime = 0;
        }
        
        unsigned long workTime = micros() - loopStart;
        esp_task_wdt_reset();

        // Scheduling policy: playback/connect -> aggressive loop, idle -> short sleep
        bool idleAudio = (!currentState && !isConnecting && uxQueueMessagesWaiting(audioQueue) == 0);
        if (idleAudio) {
            vTaskDelay(2);
        } else {
            (void)workTime;
            taskYIELD();
        }
    }
}

// 🔘 ENKODER EC11 #1 (głośność) - FALLING na S1, odczyt S2 dla kierunku
volatile int encoderPosition = 0;
portMUX_TYPE encoderMutex = portMUX_INITIALIZER_UNLOCKED;
// Debug counters – count how many times each ISR is entered (IRAM‑safe)
volatile uint32_t enc1_isr_counter = 0;

// gpio_get_level() is IRAM‑safe on ESP32‑S3 and handles all pin ranges correctly

// Diagnostic ring buffer for encoder transitions (IRAM-safe, no Serial in ISR)
#define ENC_DIAG_BUF 32
static DRAM_ATTR struct {
    uint8_t prevState;
    uint8_t newState;
    int8_t delta;
    uint32_t counter;
} enc1Diag[ENC_DIAG_BUF];
static DRAM_ATTR int enc1DiagIdx = 0;
static DRAM_ATTR uint32_t enc1DiagCounter = 0;

static DRAM_ATTR struct {
    uint8_t prevState;
    uint8_t newState;
    int8_t delta;
    uint32_t counter;
} enc2Diag[ENC_DIAG_BUF];
static DRAM_ATTR int enc2DiagIdx = 0;
static DRAM_ATTR uint32_t enc2DiagCounter = 0;

void IRAM_ATTR encoderISR() {
    // Standardowa metoda: 1 detent = 1 count z debounce 5ms
    static uint8_t state = 0;
    static unsigned long last_time = 0;
    
    uint32_t gpioIn = REG_READ(GPIO_IN_REG);
    uint32_t gpioIn1 = REG_READ(GPIO_IN1_REG);
    uint8_t s1 = (ENC1_PIN_S1 < 32) ? ((gpioIn >> ENC1_PIN_S1) & 1) : ((gpioIn1 >> (ENC1_PIN_S1 - 32)) & 1);
    uint8_t s2 = (ENC1_PIN_S2 < 32) ? ((gpioIn >> ENC1_PIN_S2) & 1) : ((gpioIn1 >> (ENC1_PIN_S2 - 32)) & 1);
    uint8_t newState = (s1 << 1) | s2;
    
    // Wykrywanie pełnego detentu (powrót do stanu 00)
    if (newState == 0 && state != 0) {
        unsigned long now = millis();
        if (now - last_time > 5) {  // 5ms debounce
            portENTER_CRITICAL_ISR(&encoderMutex);
            // Kierunek na podstawie poprzedniego stanu
            if (state == 1) encoderPosition++;      // CW: 00→01→11→10→00
            else if (state == 2) encoderPosition--; // CCW: 00→10→11→01→00
            portEXIT_CRITICAL_ISR(&encoderMutex);
            enc1_isr_counter++;
            last_time = now;
        }
    }
    state = newState;
}

volatile int navEncoderPosition = 0;
portMUX_TYPE navEncoderMutex = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t enc2_isr_counter = 0;

void IRAM_ATTR navEncoderISR() {
    // Standardowa metoda: 1 detent = 1 count z debounce 5ms
    static uint8_t state = 0;
    static unsigned long last_time = 0;
    
    uint8_t s1 = gpio_get_level((gpio_num_t)ENC2_PIN_S1);
    uint8_t s2 = gpio_get_level((gpio_num_t)ENC2_PIN_S2);
    uint8_t newState = (s1 << 1) | s2;
    
    // Wykrywanie pełnego detentu (powrót do stanu 00)
    if (newState == 0 && state != 0) {
        unsigned long now = millis();
        if (now - last_time > 5) {  // 5ms debounce
            portENTER_CRITICAL_ISR(&navEncoderMutex);
            // Kierunek na podstawie poprzedniego stanu
            if (state == 1) navEncoderPosition++;      // CW: 00→01→11→10→00
            else if (state == 2) navEncoderPosition--; // CCW: 00→10→11→01→00
            portEXIT_CRITICAL_ISR(&navEncoderMutex);
            enc2_isr_counter++;
            last_time = now;
        }
    }
    state = newState;
}

// Funkcja odczytu pinu z wielokrotnym próbkowaniem (debounce)
static bool stableDigitalRead(int pin, int samples = 5, int delayUs = 200) {
    bool first = digitalRead(pin);
    for (int i = 1; i < samples; i++) {
        delayMicroseconds(delayUs);
        if (digitalRead(pin) != first) return first; // wróć do pierwszego jeśli niestabilne
    }
    return first;
}

void setup() {
    // ✅ PIERWSZE CO ROBIMY PRZED WSZYSTKIM: USTAW PINY ENKODERÓW NA HIGH
    // Zapobiega świeceniu diody RGB podczas bootowania
    pinMode(ENC1_PIN_S1, OUTPUT);
    pinMode(ENC1_PIN_KEY, OUTPUT);
    digitalWrite(ENC1_PIN_S1, HIGH);
    digitalWrite(ENC1_PIN_KEY, HIGH);
    pinMode(ENC2_PIN_S1, OUTPUT);
    pinMode(ENC2_PIN_KEY, OUTPUT);
    digitalWrite(ENC2_PIN_S1, HIGH);
    digitalWrite(ENC2_PIN_KEY, HIGH);
    delayMicroseconds(100);

    // ✅ DIAGNOSTYKA PINS STRAPPING (PIERWSZA RZECZ PO URUCHOMIENIU)
    Serial.begin(115200);
    delay(100);

    if (DEBUG_ENABLED && LOG_ENABLE_SYSTEM) {
        uint32_t strap_reg = READ_PERI_REG(0x60008038);
        Serial.println("\n\n╔═══════════════════════════════════════════╗");
        Serial.println(  "║           STRAP PIN DIAGNOSTIC           ║");
        Serial.println(  "╚═══════════════════════════════════════════╝");
        Serial.printf("RAW STRAP REGISTER: 0x%08lX\n", strap_reg);
        Serial.println("╔═════════╦════════╦══════════════════════╗");
        Serial.println("║ PIN     ║  STATE ║  PRZYCZYNA BŁĘDU     ║");
        Serial.println("╠═════════╬════════╬══════════════════════╣");

        // Wszystkie pinów strappingu ESP32-S3 (poza GPIO4 który to jest podswietlenie ekranu)
        const int strap_pins[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 45, 46, 47, 48};
        for (int i=0; i<sizeof(strap_pins)/sizeof(strap_pins[0]); i++) {
            int pin = strap_pins[i];
            pinMode(pin, INPUT_PULLUP);
            delayMicroseconds(10);
            int state = digitalRead(pin);
            if (state == LOW) {
                Serial.printf("║ GPIO %-2d  ║   LOW  ║  ❌ PROBLEM        ║\n", pin);
            } else {
                Serial.printf("║ GPIO %-2d  ║  HIGH  ║  ✅ OK             ║\n", pin);
            }
        }
        Serial.println("╚═════════╩════════╩══════════════════════╝");
        Serial.println("\n");
    }

    // 0. Enkoder EC11 #1 (głośność) - FALLING na S1, S2 czytany w ISR dla kierunku
    pinMode(ENC1_PIN_S1, INPUT_PULLUP);
    pinMode(ENC1_PIN_S2, INPUT_PULLUP);
    pinMode(ENC1_PIN_KEY, INPUT_PULLUP);
    // ENC1 - CHANGE on both pins for quadrature state machine
    attachInterrupt(digitalPinToInterrupt(ENC1_PIN_S1), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC1_PIN_S2), encoderISR, CHANGE);

    // 0a. Enkoder EC11 #2 (nawigacja) - CHANGE on both pins for quadrature state machine
    pinMode(ENC2_PIN_S1, INPUT_PULLUP);
    pinMode(ENC2_PIN_S2, INPUT_PULLUP);
    pinMode(ENC2_PIN_KEY, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC2_PIN_S1), navEncoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC2_PIN_S2), navEncoderISR, CHANGE);
    
    delay(2000);
    
    // Banner startowy z architekturą rdzeni
    if (DEBUG_ENABLED && LOG_ENABLE_SYSTEM) {
        Serial.println("\n\n========================================");
        Serial.println(">>> Moduł ESP32-S3 DevKitC-1 WROOM-1 N16R8 + WEATHER STATION <<<");
        Serial.println("========================================");
        Serial.println("[ARCH] Rdzeń 0: Audio Task (I2S + HTTP)");
        Serial.println("[ARCH] Rdzeń 1: UI Task (Touch + GFX)");
        Serial.println("[ARCH] Sync: Queue (8 slots) + Volatile");
        Serial.println("----------------------------------------");
        Serial.printf("[BOOT] Core ID: %d (Setup running here)\n", xPortGetCoreID());
        Serial.printf("[BOOT] Free SRAM: %d KB\n", ESP.getFreeHeap() / 1024);
        Serial.printf("[BOOT] CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
        Serial.println(">>> BOOTING...");
    }

    // 2. Bardzo wczesna inicjalizacja PSRAM
    if (!psramInit()) {
        Serial.println(">>> FATAL: PSRAM NOT FOUND! Check IDE Settings (OPI/QSPI)");
        while(1) delay(100);
    }
    
    begin();
    info("SYS", "PSRAM Ready: " + String(ESP.getPsramSize()/1024) + "KB");

    // WDT init NA POCZĄTKU setup(): Arduino framework domyślnie ustawia ~5s.
    // Przełączamy od razu na 30s, aby dłuższe operacje startowe (WiFi/HTTP) nie resetowały MCU.
    esp_task_wdt_deinit();
    esp_task_wdt_init(30, true);  // 30 sekund, panic on timeout

    // Subskrypcja WDT dla głównego setup/loop task (Core 1)
    esp_task_wdt_add(NULL);

    // 3. Inicjalizacja ekranu BEZ bufora (na próbę)
    display->begin(100000000L);
    display->setRotation(1);  // Rotation 1 = landscape, matches touch.setRotation(1)
    if (!canvas->begin()) {
        error("GFX", "Canvas alloc failed!");
    }
    
    pinMode(TFT_BL, OUTPUT);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, brightness);

    touch.begin();
    touch.setRotation(1);

    // Load config from NVS (WiFi, stations, volume)
    loadConfig();
    currentVolume = cfg_volume;
    info("CFG", "Config loaded: vol=" + String(currentVolume) + ", stations=" + String(cfg_station_count));

    // 4. Inicjalizacja kolejki audio
    audioQueue = xQueueCreate(8, sizeof(AudioCommand));
    
    // 5. WiFi - audio inicjalizujemy dopiero w AudioLoopTask przy pierwszym CONNECT
    setupWiFi();

    // Show log screen during startup
    drawLogScreen(canvas);
    canvas->flush();

    // Reset WDT po setupWiFi() - zapobiega crash jeśli WiFi timeout
    esp_task_wdt_reset();

    // Pobranie pogody tylko jeśli WiFi żyje
    if (isConnected()) {
        if (updateWeather()) {
            weatherLoaded = true;
            lastWeatherUpdate = millis(); // Ustaw timer po pierwszym pobraniu - zapobiega spamowi co 7s
        }
    }

    // 5. KLUCZOWE: Opóźnienie przed startem drugiego rdzenia
    info("SYS", "Waiting for core stability...");
    delay(1000);

    xTaskCreatePinnedToCore(
        AudioLoopTask, 
        "AudioTask", 
        40960,      // 40KB stosu - wymagane dla biblioteki audio
        NULL, 
        5,          // Wyższy priorytet dla stabilnego audio streaming
        &AudioTask, 
        0           // Rdzeń 0
    );
    
    // ✅ Wysyłamy zapamiętaną głośność do audio task zaraz po starcie
    AudioCommand initVolumeCmd;
    initVolumeCmd.type = AUDIO_CMD_VOLUME;
    initVolumeCmd.data.volume = currentVolume;
    xQueueSend(audioQueue, &initVolumeCmd, 0);
    info("SYS", "Initial volume sent: " + String(currentVolume));
    
    info("SYS", "Setup Finished. Audio Task on Core 0");

    info("SYS", "WDT active on both cores");
}

void loop() {
    // Reset WDT dla Rdzenia 1 - ZAWSZE na początku loop()
    esp_task_wdt_reset();

    trackCPUStart();
    reportStatus(AudioTask);

    static unsigned long touchStart = 0;
    static unsigned long lastVolumeAction = 0;
    static unsigned long lastBrightnessAction = 0;
    static unsigned long lastAudioNotReadyLog = 0;
    static unsigned long lastUiRender = 0;
    static unsigned long lastYield = 0;
    static bool uiDirty = true;
    static AppMode prevMode = currentMode;
    static int prevVolume = currentVolume;
    static int prevBrightness = brightness;
    static bool prevAudioPlaying = false;
    static bool prevConnecting = false;
    static int prevActiveIdx = -1;
    static int prevScrollY = 0;
    
    // Zmienne dla detekcji podwójnego kliknięcia
    static unsigned long lastTapTime = 0;
    static int lastTapX = 0, lastTapY = 0;
    static bool waitingForSecondTap = false;
    const unsigned long DOUBLE_TAP_MAX_DELAY = 500;  // ms między kliknięciami (zwiększone z 300)
    const int DOUBLE_TAP_MAX_DISTANCE = 60;          // px tolerancji pozycji (zwiększone z 30)

    const unsigned long now = millis();

    // ★ DIAGNOSTYKA ENKODERÓW co 2s
    static unsigned long lastEncDiag = 0;
    if (now - lastEncDiag >= 2000) {
        int e1pos, e2pos;
        portENTER_CRITICAL(&encoderMutex);
        e1pos = encoderPosition;
        portEXIT_CRITICAL(&encoderMutex);
        portENTER_CRITICAL(&navEncoderMutex);
        e2pos = navEncoderPosition;
        portEXIT_CRITICAL(&navEncoderMutex);
        logEncPeriodic("[ENC_DIAG] pos E1:%d E2:%d\n", e1pos, e2pos);
        lastEncDiag = now;
    }
  // ★ DIAGNOSTYKA TABLICY KWADRATUROWEJ co 8s (ostatnie 16 przejść)
  static unsigned long lastQuadDiag = 0;
  if (now - lastQuadDiag >= 8000) {
      logEncPeriodic("\n[QUAD_DIAG] === ENC1 transitions (prev->new delta) ===\n");
      int start = (enc1DiagIdx - 16 + ENC_DIAG_BUF) % ENC_DIAG_BUF;
      for (int i = 0; i < 16; i++) {
          int idx = (start + i) % ENC_DIAG_BUF;
          int d = enc1Diag[idx].delta;
          if (d != 0) {
              logEncPeriodic("  #%lu %u->%u d=%+d (S1=%u S2=%u)\n",
                  enc1Diag[idx].counter,
                  enc1Diag[idx].prevState, enc1Diag[idx].newState, d,
                  (enc1Diag[idx].newState >> 1) & 1,
                  enc1Diag[idx].newState & 1);
          }
      }
      logEncPeriodic("[QUAD_DIAG] === ENC2 transitions (prev->new delta) ===\n");
      start = (enc2DiagIdx - 16 + ENC_DIAG_BUF) % ENC_DIAG_BUF;
      for (int i = 0; i < 16; i++) {
          int idx = (start + i) % ENC_DIAG_BUF;
          int d = enc2Diag[idx].delta;
          if (d != 0) {
              logEncPeriodic("  #%lu %u->%u d=%+d (S1=%u S2=%u)\n",
                  enc2Diag[idx].counter,
                  enc2Diag[idx].prevState, enc2Diag[idx].newState, d,
                  (enc2Diag[idx].newState >> 1) & 1,
                  enc2Diag[idx].newState & 1);
          }
      }
      logEncPeriodic("[QUAD_DIAG] =========================\n");
      lastQuadDiag = now;
  }
  // ---------------------------------------------------------------------
  // Full encoder pin debug – digitalRead vs gpio_get_level every 5 s
  static unsigned long lastEnc1Debug = 0;
  if (now - lastEnc1Debug >= 5000) {
    logEncPeriodic("[ENC_DEBUG] isr1:%lu isr2:%lu | E1 S1(dr:%d gpio:%d) S2(dr:%d gpio:%d) KEY(dr:%d) | E2 S1(dr:%d gpio:%d) S2(dr:%d gpio:%d) KEY(dr:%d)\n",
        enc1_isr_counter, enc2_isr_counter,
        digitalRead(ENC1_PIN_S1), gpio_get_level((gpio_num_t)ENC1_PIN_S1),
        digitalRead(ENC1_PIN_S2), gpio_get_level((gpio_num_t)ENC1_PIN_S2),
        digitalRead(ENC1_PIN_KEY),
        digitalRead(ENC2_PIN_S1), gpio_get_level((gpio_num_t)ENC2_PIN_S1),
        digitalRead(ENC2_PIN_S2), gpio_get_level((gpio_num_t)ENC2_PIN_S2),
        digitalRead(ENC2_PIN_KEY));
    lastEnc1Debug = now;
  }
    const unsigned long TOUCH_DEBOUNCE_MS = 250;
    const unsigned long VOLUME_STEP_MS = 120;
    const unsigned long BRIGHTNESS_STEP_MS = 120;
    const unsigned long UI_FRAME_MS = 1000; // heartbeat redraw gdy brak zmian
    const unsigned long YIELD_MS = 5;      // Co 5ms yield do systemu

    // Yield co jakiś czas aby odciążyć rdzeń 1
    if (now - lastYield >= YIELD_MS) {
        delay(1);
        lastYield = now;
        esp_task_wdt_reset(); // Reset WDT po yield
    }

    // Wywoływanie w każdym cyklu do obsługi timeoutów połączenia audio
    unsigned long t1 = micros();
    maintainConnectionState();
    esp_task_wdt_reset(); // Reset WDT po maintainConnectionState
    unsigned long t2 = micros();

    bool isTouched = touch.touched();
    esp_task_wdt_reset(); // Reset WDT po touch.touched()
    unsigned long t3 = micros();

    if (isTouched) {
        auto p = touch.getPoint();
        int tx = map(p.x, 200, 3800, 0, 320);
        int ty = map(p.y, 200, 3800, 0, 240);
        
        unsigned long touchReadTime = micros() - t3;
        if (touchReadTime > 2000) {
            logEncPeriodic("[CORE1][TOUCH_RAW] %lu us raw(%d,%d) mapped(%d,%d)\n", 
                         touchReadTime, p.x, p.y, tx, ty);
        }

        if (touchStart == 0) {
            touchStart = millis();
        }
        unsigned long touchDuration = now - touchStart;

        if (ty > 210) { // Dolny pasek nawigacji
            if (touchDuration > 50 && touchDuration < 500 && (now - lastTouchAction) > TOUCH_DEBOUNCE_MS) {
                // Cykl: WEATHER → RADIO → CLOCK → WEATHER...
                AppMode oldMode = currentMode;
                if (currentMode == MODE_WEATHER) currentMode = MODE_RADIO;
                else if (currentMode == MODE_RADIO) currentMode = MODE_CLOCK;
                else currentMode = MODE_WEATHER;
                
                // Beep potwierdzający przy przejściu na tryb radio
                if(oldMode == MODE_WEATHER && currentMode == MODE_RADIO) {
                    beep(1500, 250);
                }
                
                // Radio gra dalej w tle we wszystkich trybach
                lastTouchAction = now;
                logTouchEvent("NAV_TAP", tx, ty, touchDuration);
            }
        }
        else if (ty < 35 && tx < 30 && currentMode == MODE_RADIO) {
            // Ikona play/pause w headerze - toggle play/pause
            if (touchDuration > 50 && touchDuration < 500 && (now - lastTouchAction) > TOUCH_DEBOUNCE_MS) {
                if (audioPlaying.load()) {
                    // Zatrzymaj
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_STOP;
                    logAudioCmd("SENDING", "STOP (header tap)");
                    xQueueSend(audioQueue, &cmd, 0);
                } else if (activeIdx >= 0) {
                    // Zacznij odtwarzanie aktywnej stacji
                    int sIdx = getBestStreamForStation(activeIdx);
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_CONNECT;
                    strncpy(cmd.data.url, STATIONS[activeIdx].streams[sIdx].url, 255);
                    cmd.data.url[255] = '\0';
                    logAudioCmd("SENDING", "CONNECT (header tap): " + String(STATIONS[activeIdx].name));
                    xQueueSend(audioQueue, &cmd, 0);
                }
                lastTouchAction = now;
                logTouchEvent("HEADER_TAP", tx, ty, touchDuration);
            }
        }
        else if (ty < HEADER_H && currentMode == MODE_RADIO) {
            // Przyciski głośności w headerze - tryb przytrzymania
            static bool volMinusPressed = false;
            static bool volPlusPressed = false;
            static unsigned long volPressStart = 0;
            static unsigned long lastVolChange = 0;
            const unsigned long VOL_REPEAT_DELAY = 200; // ms między zmianami głośności przy przytrzymaniu

            bool touchingMinus = (tx >= VOL_MINUS_TOUCH_X && tx <= VOL_MINUS_TOUCH_X + VOL_BTN_SIZE && ty >= VOL_TOUCH_Y && ty <= VOL_TOUCH_Y + VOL_BTN_SIZE);
            bool touchingPlus = (tx >= VOL_PLUS_TOUCH_X && tx <= VOL_PLUS_TOUCH_X + VOL_BTN_SIZE && ty >= VOL_TOUCH_Y && ty <= VOL_TOUCH_Y + VOL_BTN_SIZE);

            if (touchingMinus && !volMinusPressed) {
                volMinusPressed = true;
                volPressStart = now;
                lastVolChange = now;
                // Pierwsze kliknięcie - zmień o 1
                if (currentVolume > 0) {
                    currentVolume--;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    // Save volume to Preferences
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                }
                logTouchEvent("VOL_MINUS", tx, ty, touchDuration);
                lastTouchAction = now;
            } else if (touchingPlus && !volPlusPressed) {
                volPlusPressed = true;
                volPressStart = now;
                lastVolChange = now;
                // Pierwsze kliknięcie - zmień o 1
                if (currentVolume < 21) {
                    currentVolume++;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    // Save volume to Preferences
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                }
                logTouchEvent("VOL_PLUS", tx, ty, touchDuration);
                lastTouchAction = now;
            }

            // Przytrzymanie - ciągła zmiana głośności
            if ((volMinusPressed || volPlusPressed) && (now - lastVolChange >= VOL_REPEAT_DELAY)) {
                if (volMinusPressed && currentVolume > 0) {
                    currentVolume--;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    lastVolChange = now;
                    // Save volume to Preferences
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                } else if (volPlusPressed && currentVolume < 21) {
                    currentVolume++;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    lastVolChange = now;
                    // Save volume to Preferences
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                }
            }

            // Reset stanu przycisków gdy nie są dotykane
            if (!touchingMinus) volMinusPressed = false;
            if (!touchingPlus) volPlusPressed = false;

            // Kontrola jasności w headerze (ikony słońca)
            handleRadioBrightness(tx, ty, now, touchDuration);
        }
        else if (currentMode == MODE_RADIO) {
            if (touchDuration > 200) {
                // Scroll mode - only scroll when touch lasts > 200ms
                handleRadioScroll(ty, true);
            } else {
                // Tap mode - only tap interaction when touch <= 200ms
                if (touchDuration > 25 && (now - lastTouchAction) > TOUCH_DEBOUNCE_MS) {
                    // Sprawdź czy to podwójne kliknięcie
                    if (waitingForSecondTap && (now - lastTapTime) < DOUBLE_TAP_MAX_DELAY) {
                        // Sprawdź czy w podobnej pozycji
                        int dx = abs(tx - lastTapX);
                        int dy = abs(ty - lastTapY);
                        if (dx < DOUBLE_TAP_MAX_DISTANCE && dy < DOUBLE_TAP_MAX_DISTANCE) {
                            // PODWÓJNE KLIKNIĘCIE - zatwierdź stację
                            logTouchEvent("DOUBLE_TAP", tx, ty, now - lastTapTime);
                            handleRadioActions(tx, ty);
                            waitingForSecondTap = false;
                        } else {
                            // Za daleko, traktuj jako nowe pierwsze kliknięcie
                            waitingForSecondTap = true;
                            lastTapTime = now;
                            lastTapX = tx;
                            lastTapY = ty;
                        }
                    } else {
                        // PIERWSZE KLIKNIĘCIE - czekaj na drugie
                        waitingForSecondTap = true;
                        lastTapTime = now;
                        lastTapX = tx;
                        lastTapY = ty;
                    }
                    lastTouchAction = now;
                }
            }

        }
        else if (currentMode == MODE_WEATHER && ty < HEADER_H) {
            // ✅ Obsługa przycisków głośności tak samo jak w radiu
            static bool volMinusPressed = false;
            static bool volPlusPressed = false;
            static unsigned long volPressStart = 0;
            static unsigned long lastVolChange = 0;
            const unsigned long VOL_REPEAT_DELAY = 200;

            bool touchingMinus = (tx >= VOL_MINUS_TOUCH_X && tx <= VOL_MINUS_TOUCH_X + VOL_BTN_SIZE && ty >= VOL_TOUCH_Y && ty <= VOL_TOUCH_Y + VOL_BTN_SIZE);
            bool touchingPlus = (tx >= VOL_PLUS_TOUCH_X && tx <= VOL_PLUS_TOUCH_X + VOL_BTN_SIZE && ty >= VOL_TOUCH_Y && ty <= VOL_TOUCH_Y + VOL_BTN_SIZE);

            if (touchingMinus && !volMinusPressed) {
                volMinusPressed = true;
                volPressStart = now;
                lastVolChange = now;
                if (currentVolume > 0) {
                    currentVolume--;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                }
                lastTouchAction = now;
            } else if (touchingPlus && !volPlusPressed) {
                volPlusPressed = true;
                volPressStart = now;
                lastVolChange = now;
                if (currentVolume < 21) {
                    currentVolume++;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                }
                lastTouchAction = now;
            }

            if ((volMinusPressed || volPlusPressed) && (now - lastVolChange >= VOL_REPEAT_DELAY)) {
                if (volMinusPressed && currentVolume > 0) {
                    currentVolume--;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    lastVolChange = now;
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                } else if (volPlusPressed && currentVolume < 21) {
                    currentVolume++;
                    AudioCommand cmd;
                    cmd.type = AUDIO_CMD_VOLUME;
                    cmd.data.volume = currentVolume;
                    xQueueSend(audioQueue, &cmd, 0);
                    lastVolChange = now;
                    preferences.begin("radio", false);
                    preferences.putInt("volume", currentVolume);
                    preferences.end();
                }
            }

            if (!touchingMinus) volMinusPressed = false;
            if (!touchingPlus) volPlusPressed = false;

            // Obsługa przycisków jasności tak samo jak w radiu
            handleRadioBrightness(tx, ty, now, touchDuration);
        }
    } else {
        touchStart = 0;
        if (currentMode == MODE_RADIO) handleRadioScroll(0, false);
        
        // Reset oczekiwania na drugie kliknięcie po upływie czasu
        if (waitingForSecondTap && (now - lastTapTime) >= DOUBLE_TAP_MAX_DELAY) {
            waitingForSecondTap = false;
            logTouchEvent("SINGLE_TAP_TIMEOUT", lastTapX, lastTapY);
        }
    }

    // Dirty-check UI state (render only on change + heartbeat)
    bool currAudioPlaying = audioPlaying.load();
    bool currConnecting = isConnecting;
    if (currentMode != prevMode ||
        currentVolume != prevVolume ||
        brightness != prevBrightness ||
        currAudioPlaying != prevAudioPlaying ||
        currConnecting != prevConnecting ||
        activeIdx != prevActiveIdx ||
        scrollY != prevScrollY) {
        uiDirty = true;
        prevMode = currentMode;
        prevVolume = currentVolume;
        prevBrightness = brightness;
        prevAudioPlaying = currAudioPlaying;
        prevConnecting = currConnecting;
        prevActiveIdx = activeIdx;
        prevScrollY = scrollY;
    }

    // Renderowanie
    if (uiDirty || (now - lastUiRender >= UI_FRAME_MS)) {
        if (currentMode == MODE_WEATHER) {
            if (weatherLoaded) {
                drawWeatherUI(canvas, brightness, currentVolume);
            } else {
                drawLogScreen(canvas);
            }
        } else if (currentMode == MODE_CLOCK) {
            drawClockUI(canvas, brightness, currentVolume, currAudioPlaying, currConnecting);
        } else {
            drawRadioUI(canvas, currentVolume, brightness, currAudioPlaying, currConnecting);
        }
        canvas->flush();
        lastUiRender = now;
        uiDirty = false;
        // RENDER+FLUSH log removed - too noisy (every 100ms)

        // ZAWSZE yield po renderze - audio ma 1MB bufor, przetrwa przerwę
        delay(1);  // 1ms yield dla Rdzenia 0
    }

    // Specjalna logika dla pierwszego pobrania pogody po starcie
    const unsigned long updateInterval = weatherLoaded ? 60000 : 2000;
    
    if (millis() - lastWeatherUpdate > updateInterval) {
        if (isConnected()) {
            esp_task_wdt_reset(); // Reset WDT przed updateWeather()
            if (updateWeather()) {
                weatherLoaded = true;
            }
            lastWeatherUpdate = millis();
        }
        else if (!weatherLoaded) {
            // Gdy jeszcze nie mamy pogody, resetuj timer aby próbowac ponownie za 2s
            lastWeatherUpdate = millis() - 58000;
        }
    }

    trackCPUEnd();

    // Log czasów sekcji jeśli któraś trwała za długo (>10ms)
    unsigned long totalTouch = t3 - t2;
    unsigned long totalMaintain = t2 - t1;
    if (totalTouch > 10000 || totalMaintain > 10000) {
        logTiming("LOOP", totalMaintain, totalTouch);
    }

    // 🔘 Obsługa przycisku enkodera EC11 #1 (głośność) - zmiana trybu
    static bool lastEncButton = true;
    static unsigned long lastEncButtonTime = 0;
    bool encButton = stableDigitalRead(ENC1_PIN_KEY, 5, 200);

    // GUARD: jeśli w ostatnich 30ms był obrót ENC2, zignoruj ENC1_BUTTON (crosstalk przez VCC)
    if (encButton == LOW && lastEncButton == HIGH && (now - lastEncButtonTime) > 300) {
        // Cykl: WEATHER → RADIO → CLOCK → WEATHER...
        AppMode oldMode = currentMode;
        if (currentMode == MODE_WEATHER) currentMode = MODE_RADIO;
        else if (currentMode == MODE_RADIO) currentMode = MODE_CLOCK;
        else currentMode = MODE_WEATHER;
        
        if(oldMode == MODE_WEATHER && currentMode == MODE_RADIO) {
            beep(1500, 250);
        }
        
        uiDirty = true;
        lastEncButtonTime = now;
        logEncEvent("[CORE1][ENC1_BUTTON] mode change\n");
    }
    lastEncButton = encButton;

    // 🔘 Obsługa obrotu Enkodera EC11 #1 (głośność)
    // 1 detent = 1 count (zgodnie z nowym ISR)
    static int lastEncoderPos = 0;
    int currentEncPos;

    portENTER_CRITICAL(&encoderMutex);
    currentEncPos = encoderPosition;
    portEXIT_CRITICAL(&encoderMutex);

    if (currentEncPos != lastEncoderPos) {
        int delta = currentEncPos - lastEncoderPos;
        int steps = delta;  // Bezpośrednio 1:1
        if (steps >= 1) {
            logEncEvent("[CORE1][ENC1_ROTATION] RIGHT steps:%d newVol:%d\n", steps, (currentVolume + steps > 21) ? 21 : currentVolume + steps);
            while (steps > 0 && currentVolume < 21) {
                currentVolume++;
                steps--;
            }
            AudioCommand cmd;
            cmd.type = AUDIO_CMD_VOLUME;
            cmd.data.volume = currentVolume;
            xQueueSend(audioQueue, &cmd, 0);
            preferences.begin("radio", false);
            preferences.putInt("volume", currentVolume);
            preferences.end();
            uiDirty = true;
            lastTouchAction = now;
        }
        if (steps <= -1) {
            steps = -steps;
            logEncEvent("[CORE1][ENC1_ROTATION] LEFT steps:%d newVol:%d\n", steps, (currentVolume - steps < 0) ? 0 : currentVolume - steps);
            while (steps > 0 && currentVolume > 0) {
                currentVolume--;
                steps--;
            }
            AudioCommand cmd;
            cmd.type = AUDIO_CMD_VOLUME;
            cmd.data.volume = currentVolume;
            xQueueSend(audioQueue, &cmd, 0);
            preferences.begin("radio", false);
            preferences.putInt("volume", currentVolume);
            preferences.end();
            uiDirty = true;
            lastTouchAction = now;
        }
        lastEncoderPos = currentEncPos;
    }

    // 🔘 ENKODER EC11 #2 (nawigacja)
    // 1 detent = 1 count (zgodnie z nowym ISR)
    static int lastNavPos = 0;
    int currentNavPos;

    portENTER_CRITICAL(&navEncoderMutex);
    currentNavPos = navEncoderPosition;
    portEXIT_CRITICAL(&navEncoderMutex);

    if (currentNavPos != lastNavPos) {
        int delta = currentNavPos - lastNavPos;
        int steps = delta;  // Bezpośrednio 1:1
        if (steps >= 1) {
            logEncEvent("[CORE1][ENC2_ROTATION] RIGHT steps:%d dir:NEXT\n", steps);
            while (steps > 0) {
                if (currentMode == MODE_RADIO) {
                    int newIdx = activeIdx + 1;
                    if (newIdx >= TOTAL_STATIONS) newIdx = 0;
                    activeIdx = newIdx;
                    ensureActiveStationVisible();
                } else if (currentMode == MODE_WEATHER || currentMode == MODE_CLOCK) {
                    // Zwiększ jasność logarytmicznie (jak ikona +)
                    int newBr = (int)(brightness * 1.2);
                    if (newBr == brightness) newBr = brightness + 1;
                    if (newBr > 255) newBr = 255;
                    brightness = newBr;
                    ledcWrite(0, brightness);
                }
                steps--;
            }
            uiDirty = true;
            lastTouchAction = now;
        }
        if (steps <= -1) {
            steps = -steps;
            logEncEvent("[CORE1][ENC2_ROTATION] LEFT steps:%d dir:PREV\n", steps);
            while (steps > 0) {
                if (currentMode == MODE_RADIO) {
                    int newIdx = activeIdx - 1;
                    if (newIdx < 0) newIdx = TOTAL_STATIONS - 1;
                    activeIdx = newIdx;
                    ensureActiveStationVisible();
                } else if (currentMode == MODE_WEATHER || currentMode == MODE_CLOCK) {
                    // Zmniejsz jasność logarytmicznie (jak ikona -)
                    int newBr = (int)(brightness * 0.8);
                    if (newBr < 1) newBr = 1;
                    brightness = newBr;
                    ledcWrite(0, brightness);
                }
                steps--;
            }
            uiDirty = true;
            lastTouchAction = now;
        }
        lastNavPos = currentNavPos;
    }

    // 🔘 ENKODER EC11 #2 (nawigacja) - Przycisk (short/long press)
    static bool lastNavEncButton = true;
    static unsigned long navEncPressTime = 0;
    static bool navEncLongPressHandled = false;
    static bool navEncButtonHeld = false;
    bool navEncButton = stableDigitalRead(ENC2_PIN_KEY, 5, 200);

    // GUARD: jeśli w ostatnich 30ms był obrót ENC2, zignoruj ENC2_KEY (crosstalk)
    if (navEncButton == LOW && lastNavEncButton == HIGH) {
        // Wciśnięcie - zacznij mierzyć czas
        navEncPressTime = now;
        navEncLongPressHandled = false;
        navEncButtonHeld = true;
    }

    if (navEncButton == LOW && navEncButtonHeld && !navEncLongPressHandled) {
        // Sprawdź czy to długie wciśnięcie (>= 400ms)
        if (now - navEncPressTime >= 400) {
            // Długie wciśnięcie - zmiana trybu
            AppMode oldMode = currentMode;
            currentMode = (currentMode == MODE_WEATHER) ? MODE_RADIO : MODE_WEATHER;
            
            if(oldMode == MODE_WEATHER && currentMode == MODE_RADIO) {
                beep(1500, 250);
            }
            
            navEncLongPressHandled = true;
            uiDirty = true;
            logEncEvent("[CORE1][ENC2_BUTTON] LONG PRESS dur:%lu ms\n", now - navEncPressTime);
        }
    }

    if (navEncButton == HIGH && lastNavEncButton == LOW) {
        if (navEncButtonHeld && !navEncLongPressHandled) {
            unsigned long pressDuration = now - navEncPressTime;
            if (pressDuration < 400) {
                // Krótkie wciśnięcie - zatwierdź
                if (currentMode == MODE_RADIO) {
                    if (activeIdx >= 0) {
                        if (canAttemptConnection()) {
                            startStationConnection(activeIdx);
                            uiDirty = true;
                        }
                    }
                } else {
                    // MODE_WEATHER: reset widoku do "teraz"
                    // TODO: reset weather time offset
                    uiDirty = true;
                }
                logEncEvent("[CORE1][ENC2_BUTTON] SHORT PRESS dur:%lu ms\n", pressDuration);
            }
        }
        navEncButtonHeld = false;
        navEncLongPressHandled = false;
    }
    lastNavEncButton = navEncButton;

    // Krótki yield dla systemu - kluczowe dla stabilności Core 1
    esp_task_wdt_reset(); // Reset WDT przed vTaskDelay
    vTaskDelay(1);  // 1 tick = ~10-15ms, daje czas innym zadaniom
}

void audio_info(const char *msg) { /* silenced - too noisy */ }
void audio_bitrate(const char *br) { /* silenced - too noisy */ }