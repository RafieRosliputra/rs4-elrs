// rs4-elrs remote (ESP32-S3 N16R8) — pins and build switches.
// Wiring table: docs/wiring.md
#pragma once

// ---------------- Build switches (debug phases) ----------------
// Phase 1: set ENABLE_CRSF 0 to test joystick, buttons and screen alone.
#define ENABLE_CRSF      1   // send CRSF to the Ranger Micro
#define CRSF_TELEMETRY   1   // read RSSI/LQ back on the same wire (half-duplex)
#define ENABLE_TFT       1   // 1.14" ST7789 screen
#define DEBUG_SERIAL     1   // print joystick/buttons/link to Serial Monitor

// ---------------- Joystick (ADC1 only: GPIO1-10) ----------------
#define PIN_JOY_X     1    // pan  (wiper of X pot)
#define PIN_JOY_Y     2    // tilt (wiper of Y pot)
#define PIN_JOY_Z     4    // zoom (twist, white wire)
#define PIN_JOY_BTN   5    // joystick top button (green wires)

// ---------------- Buttons (other leg to GND) ----------------
#define PIN_P1        6
#define PIN_P2        7
#define PIN_P3        13
#define PIN_P4        15
#define PIN_MOVE1     16
#define PIN_MOVE2     18
#define PIN_MOVE3     21
#define PIN_MOVE4     38
#define PIN_ZOOM1     39
#define PIN_ZOOM2     40
#define PIN_ZOOM3     41
#define PIN_ZOOM4     42
#define PIN_WIFI      0    // same as BOOT button; do not hold while powering up

// ---------------- TFT ST7789 135x240 (SPI) ----------------
#define PIN_TFT_SCK   12   // "SCL" on the board
#define PIN_TFT_MOSI  11   // "SDA" on the board
#define PIN_TFT_RST   8    // "RES"
#define PIN_TFT_DC    9
#define PIN_TFT_CS    10
// BLK -> 3V3 (backlight always on)

// ---------------- Ranger Micro (CRSF) ----------------
#define PIN_CRSF      17   // -> S.Port / Data pin of the module bay
#define PIN_CRSF_UNUSED_RX 47  // only used when CRSF_TELEMETRY is 0
#define CRSF_BAUD     400000
#define CRSF_PERIOD_US 4000   // send channels every 4 ms (250 Hz)

// ---------------- Settings Wi-Fi (press the WiFi button) ----------------
#define AP_SSID       "RS4-Remote"
#define AP_PASS       "rs4remote"   // at least 8 characters

// ---------------- Timing ----------------
#define HOLD_SAVE_MS     2000  // hold a preset button this long to SAVE
#define JOY_LONG_MS      1000  // hold the joystick button this long = recall P1
#define DEBOUNCE_MS      25
#define NOTICE_MS        1500  // how long "SAVED P2" stays on screen
#define LINK_LOST_MS     1000  // no link stats for this long = "NO LINK"
