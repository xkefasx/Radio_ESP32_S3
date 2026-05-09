#ifndef CONFIG_H
#define CONFIG_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>

// ===== WiFi =====
const char* WIFI_SSID = "PLAY_Swiatlowod_36EE";
const char* WIFI_PASS = "XT7SUqGt";

// ===== Ekran ILI9341 (kolejność wg złącza z ekran.md) =====
//  6) miso       → TFT_MISO  (SPI MISO - wspólne z dotykiem T_do)
//  7) led        → TFT_BL    (podświetlenie)
//  8) sck        → TFT_SCK   (SPI SCK  - wspólne z dotykiem T_clk)
//  9) mosi       → TFT_MOSI  (SPI MOSI - wspólne z dotykiem T_din)
// 10) dc         → TFT_DC    (Data/Command)
// 11) reset      → TFT_RST   (Reset)
// 12) cs         → TFT_CS    (Chip Select)
#define TFT_MISO    13
#define TFT_BL      4
#define TFT_SCK     12
#define TFT_MOSI    11
#define TFT_DC      14
#define TFT_RST     9
#define TFT_CS      10

// ===== Dotyk XPT2046 (kolejność wg złącza z ekran.md) =====
//  1) T_irq      → TOUCH_IRQ (przerwanie dotyku)
//  2) T_do       → (współdzielone SPI MISO - zdef. w ekranie)
//  3) T_din      → (współdzielone SPI MOSI - zdef. w ekranie)
//  4) T_cs       → TOUCH_CS  (Chip Select dotyku)
//  5) T_clk      → (współdzielone SPI SCK  - zdef. w ekranie)
#define TOUCH_IRQ   6
#define TOUCH_CS    5

// ===== Audio I2S (MAX98357A) =====
#define I2S_LRC 15
#define I2S_BCLK 16
#define I2S_DOUT 17

// ===== Enkoder obrotowy EC11 #1 (Górny) =====
#define ENC1_PIN_S1      40  // Consecutive with 41, 42 - all on same side
#define ENC1_PIN_S2      41
#define ENC1_PIN_KEY     42

// ===== Enkoder obrotowy EC11 #2 (Dolny) =====
#define ENC2_PIN_S1      38  // All on same side with enc1 (38,39,40,41,42,48)
#define ENC2_PIN_S2      39
#define ENC2_PIN_KEY     48

// ===== Tryby aplikacji =====
enum AppMode { MODE_WEATHER, MODE_RADIO, MODE_CLOCK };

// ===== Kolejka komend audio (thread-safety Core1→Core0) =====
enum AudioCmdType { AUDIO_CMD_CONNECT, AUDIO_CMD_STOP, AUDIO_CMD_VOLUME };

struct AudioCommand {
    AudioCmdType type;
    struct {
        char url[256];  // Fixed-size buffer for URL to avoid dangling pointer
        int volume;
    } data;
};

extern std::atomic<bool> audioPlaying;
extern SemaphoreHandle_t audioMutex;

// ===== Paleta kolorów UI (RGB565) - INWERTOWANE dla kompensacji inwersji wyświetlacza =====
#define COL_BG          0xFFFF  // Biały (wyświetla się jako czarny) - tło główne
#define COL_BG_CARD     0xE73C  // Jasna karta (wyświetla się jako ciemna)
#define COL_BG_HEADER   0xEF7D  // Nagłówek/stopka (wyświetla się jako ciemny)
#define COL_TEXT         0x0000  // Czarny (wyświetla się jako biały) - tekst
#define COL_TEXT_SEC     0x52AA  // Szary tekst drugorzędny
#define COL_TEXT_DIM     0x8430  // Przyciemniony tekst
#define COL_GREEN        0xF81F  // Jaskrawy zielony
#define COL_RED          0x07FF  // Czerwony
#define COL_ORANGE       0x041F  // Pomarańczowy
#define COL_BLUE         0xC400  // Niebieski / zimno
#define COL_YELLOW       0x001F  // Żółty
#define COL_CYAN         0xF800  // Cyjan
#define COL_GRID         0xDEFB  // Linie siatki
#define COL_DIVIDER      0xB5B6  // Linia podziału
#define COL_PLAYING_BG   0xFCDF  // Ciemny zielony (aktywna stacja)
#define COL_CONNECT_BG   0xBDFF  // Ciemny żółty (łączenie)
#define COL_VOL_BAR      0xF81F  // Pasek głośności
#define COL_VOL_BG       0xDEFB  // Tło paska głośności
#define COL_NAV_BG       0xEF5D  // Pasek nawigacji

#endif
