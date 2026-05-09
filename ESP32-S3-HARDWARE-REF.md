# ESP32-S3 Hardware Reference - WROOM-1 N16R8

> **Quick reference for ESP32-S3 DevKitC-1 with WROOM-1 N16R8 module**  
> 16MB Flash (QIO) | 8MB PSRAM (Octal SPI) | 240MHz Dual-core

---

## ⚠️ CRITICAL: PSRAM Pin Conflicts

**GPIO35, GPIO36, GPIO37 are RESERVED for Octal SPI PSRAM!**

| Pin | Internal Function | Consequence |
|-----|------------------|-------------|
| **GPIO35** | SPIIO6 (SPID/FSPID) | **Boot loop / crash** |
| **GPIO36** | SPIIO7 (FSPICLK) | **Boot loop / crash** |
| **GPIO37** | SPIDQS (FSPIQ) | **Boot loop / crash** |

**Never use GPIO35-37 for:** encoders, buttons, sensors, or any GPIO I/O.

---

## 📌 Pin Definitions (All GPIOs)

| Pin | Name | Type | Functions | Status |
|-----|------|------|-----------|--------|
| 0 | IO0 | I/O/T | RTC_GPIO0, Boot mode | ⚠️ Strapping |
| 1 | IO1 | I/O/T | RTC_GPIO1, TOUCH1, ADC1_CH0 | ✅ Safe |
| 2 | IO2 | I/O/T | RTC_GPIO2, TOUCH2, ADC1_CH1 | ✅ Safe |
| 3 | IO3 | I/O/T | RTC_GPIO3, TOUCH3, ADC1_CH2, JTAG sel | ⚠️ Strapping |
| 4 | IO4 | I/O/T | RTC_GPIO4, TOUCH4, ADC1_CH3 | ✅ Safe |
| 5 | IO5 | I/O/T | RTC_GPIO5, TOUCH5, ADC1_CH4 | ✅ Safe |
| 6 | IO6 | I/O/T | RTC_GPIO6, TOUCH6, ADC1_CH5 | ✅ Safe |
| 7 | IO7 | I/O/T | RTC_GPIO7, TOUCH7, ADC1_CH6 | ✅ Safe |
| 8 | IO8 | I/O/T | RTC_GPIO8, TOUCH8, ADC1_CH7 | ✅ Safe |
| 9 | IO9 | I/O/T | RTC_GPIO9, TOUCH9, ADC1_CH8, FSPIHD | ✅ Safe |
| 10 | IO10 | I/O/T | RTC_GPIO10, TOUCH10, ADC1_CH9, FSPICS0 | ✅ Safe |
| 11 | IO11 | I/O/T | RTC_GPIO11, TOUCH11, ADC2_CH0, FSPID | ✅ Safe |
| 12 | IO12 | I/O/T | RTC_GPIO12, TOUCH12, ADC2_CH1, FSPICLK | ✅ Safe |
| 13 | IO13 | I/O/T | RTC_GPIO13, TOUCH13, ADC2_CH2, FSPIQ | ✅ Safe |
| 14 | IO14 | I/O/T | RTC_GPIO14, TOUCH14, ADC2_CH3, FSPIWP | ✅ Safe |
| 15 | IO15 | I/O/T | RTC_GPIO15, U0RTS, ADC2_CH4, XTAL_32K_P | ✅ Safe |
| 16 | IO16 | I/O/T | RTC_GPIO16, U0CTS, ADC2_CH5, XTAL_32K_N | ✅ Safe |
| 17 | IO17 | I/O/T | RTC_GPIO17, U1TXD, ADC2_CH6 | ✅ Safe |
| 18 | IO18 | I/O/T | RTC_GPIO18, U1RXD, ADC2_CH7 | ✅ Safe |
| 19 | IO19 | I/O/T | RTC_GPIO19, U1RTS, ADC2_CH8, USB_D- | ✅ Safe |
| 20 | IO20 | I/O/T | RTC_GPIO20, U1CTS, ADC2_CH9, USB_D+ | ✅ Safe |
| 21 | IO21 | I/O/T | RTC_GPIO21 | ✅ Safe |
| 35 | IO35 | I/O/T | SPIIO6, FSPID, SUBSPID | ❌ **PSRAM** |
| 36 | IO36 | I/O/T | SPIIO7, FSPICLK, SUBSPICLK | ❌ **PSRAM** |
| 37 | IO37 | I/O/T | SPIDQS, FSPIQ, SUBSPIQ | ❌ **PSRAM** |
| 38 | IO38 | I/O/T | GPIO38, FSPIWP, SUBSPIWP | ✅ Safe |
| 39 | IO39 | I/O/T | MTCK, CLK_OUT3, SUBSPICS1 | ✅ Safe |
| 40 | IO40 | I/O/T | MTDO, CLK_OUT2 | ✅ Safe |
| 41 | IO41 | I/O/T | MTDI, CLK_OUT1 | ✅ Safe |
| 42 | IO42 | I/O/T | MTMS | ✅ Safe |
| 43 | U0TXD | I/O/T | GPIO43, CLK_OUT1, UART TX | ✅ Safe |
| 44 | U0RXD | I/O/T | GPIO44, CLK_OUT2, UART RX | ✅ Safe |
| 45 | IO45 | I/O/T | GPIO45, VDD_SPI voltage | ⚠️ Strapping |
| 46 | IO46 | I/O/T | GPIO46 | ⚠️ Strapping |
| 47 | IO47 | I/O/T | GPIO47, SPICLK_P, SUBSPICLK_P_DIFF | ✅ Safe |
| 48 | IO48 | I/O/T | GPIO48, SPICLK_N, SUBSPICLK_N_DIFF | ✅ Safe |

**Legend:** ✅ Safe for GPIO | ⚠️ Strapping pin | ❌ PSRAM conflict

---

## 🔧 Strapping Pins (Boot Configuration)

| Pin | Default | Function | Pull |
|-----|---------|----------|------|
| GPIO0 | Weak PU | Boot mode (0=Download, 1=SPI Boot) | External required for download |
| GPIO3 | Floating | JTAG signal source control | Must be driven externally |
| GPIO45 | Weak PD | VDD_SPI voltage (0=3.3V via RSP, 1=1.8V regulator) | Don't pull high on boot |
| GPIO46 | Weak PD | ROM messages, Boot mode | Don't drive high on boot |

**Setup/Hold time:** 3ms minimum after EN goes high before pin changes state.

---

## ⚡ Electrical Specifications

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Supply voltage | 3.0 | 3.3 | 3.6 | V |
| Operating temp | -40 | 25 | 65/85 | °C |
| Flash frequency | - | 80 | 120 | MHz |
| PSRAM type | - | Octal | - | SPI |
| PSRAM size | - | 8 | 16 | MB |

---

## 🎯 Project Pinout (Radio_ESP32_S3)

```cpp
// TFT ILI9341
#define TFT_BL      4   // PWM backlight
#define TFT_RST     9
#define TFT_CS      10
#define TFT_MOSI    11
#define TFT_SCK     12
#define TFT_MISO    13
#define TFT_DC      14

// Touch XPT2046
#define TOUCH_CS    5
#define TOUCH_IRQ   6

// Audio I2S (MAX98357A)
#define I2S_LRC     15
#define I2S_BCLK    16
#define I2S_DOUT    17

// Encoder EC11 #1 (Volume)
#define ENC1_PIN_S1  40  // ISR pin
#define ENC1_PIN_S2  41  // Direction (consecutive: 40,41,42)
#define ENC1_PIN_KEY 42  // Mode switch

// Encoder EC11 #2 (Navigation) - all on same side with enc1
#define ENC2_PIN_S1  38  // ISR pin
#define ENC2_PIN_S2  39  // Direction (consecutive on same side)
#define ENC2_PIN_KEY 48  // Short/long press
```

---

## 📝 Notes for LLMs

1. **Always check GPIO35-37** when suggesting pin changes - these cause boot loops
2. **GPIO8 is safe** for encoders (was used to replace GPIO35)
3. **GPIO40-41-42 are consecutive** for encoder #1 - easier wiring
4. **GPIO41 (MTDI)** and **GPIO39 (MTCK)** are JTAG pins but usable as GPIO if JTAG not needed
5. **GPIO1-14** support touch sensing (14 channels)
6. **Octal PSRAM** requires 8 data lines, consuming GPIO35-37 internally
7. **VDD_SPI** is 3.3V on N16R8 (GPIO45 eFuse configured)

---

## 🔗 Quick Links

- Full datasheet: `description_board.md` (original, 2700+ lines)
- Project rules: `.clinerules`
- Pinout diagram: `PINOUT_ESP32_S3.txt`
