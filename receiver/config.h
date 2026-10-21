// rs4-elrs receiver (ESP32-C3 Super Mini #1, on the RS4) — pins and options.
// Wiring table: docs/wiring.md
#pragma once

// ---------------- Build switches (debug phases) ----------------
// Phase 2: USE_CAN 0 -> only prints what arrives from the ELRS receiver.
// Phase 3: USE_CAN 1 -> drives the RS4. Type "help" in Serial Monitor to
//          move the gimbal from the PC without the remote (no buttons needed).
#define USE_CAN        1
#define DEBUG_PRINT    1    // print link/channels/gimbal state every 0.5 s
#define USE_CRSF       1    // 0 = ignore the ELRS receiver (serial console only)

// ---------------- Pins ----------------
#define PIN_CRSF_RX    20   // <- BetaFPV Nano RX "TX"
#define PIN_CRSF_TX    21   // -> BetaFPV Nano RX "RX" (status back to the remote)
#define PIN_CAN_TX     5    // -> SN65HVD230 CTX
#define PIN_CAN_RX     4    // <- SN65HVD230 CRX
#define PIN_CAM_TX     6    // -> ESP32-C3 #2 (camera, phase 4, not used yet)
#define PIN_CAM_RX     7
#define PIN_LED        8    // on-board LED, active LOW

#define CRSF_BAUD      420000   // ELRS receiver default

// ---------------- Gimbal behaviour ----------------
#define MAX_PAN_DPS      120.0f  // deg/s at full stick and movement speed 100 %
#define MAX_TILT_DPS      90.0f
#define ZOOM_FULL_RATE  1500.0f  // focus-motor units per second at full twist (range 0..4095)
#define SPEED_IGNORE_FOCAL 1     // 1 = same speed whatever the lens focal length
#define ANGLE_SOURCE     0x01    // 0x01 attitude angles, 0x02 joint angles (used for presets)
#define RECALL_ROLL        0     // 0 = presets leave roll alone (horizon stays level)
#define MIN_RECALL_S     0.6f    // shortest recall move
#define OVERRIDE_THRESH  0.05f   // stick above this cancels a recall

// ---------------- Safety / timing ----------------
#define LINK_TIMEOUT_MS  300     // no ELRS channels for this long = stop the gimbal
#define SPEED_PERIOD_MS   40     // speed command rate (each lasts 0.5 s on the gimbal)
#define ANGLE_PERIOD_MS  100     // gimbal angle polling
#define STATUS_PERIOD_MS 500     // status text back to the remote
