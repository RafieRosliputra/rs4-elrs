// rs4-elrs — what each ELRS channel carries. Shared by remote and receiver
// (same file in both sketch folders; keep them identical).
//
// CH1-CH4 are sent at full 10-bit resolution in every ELRS switch mode
// (Hybrid, Wide, Full Res), so everything that must be exact lives there.
#pragma once
#include <stdint.h>

// Channel index, 0-based (CH1 = 0)
enum : uint8_t {
  CH_PAN    = 0,  // CH1  -1..1  pan speed, already scaled by movement speed 1-4
  CH_TILT   = 1,  // CH2  -1..1  tilt speed, scaled the same way
  CH_ZOOM   = 2,  // CH3  -1..1  zoom rate, scaled by zoom speed 1-4
  CH_CMD    = 3,  // CH4  step (50 ticks): command * 2 + toggle bit, see below
  CH_ARM    = 4,  // CH5  always high (ELRS treats CH5 as the arm switch)
  CH_RECALL = 5,  // CH6  recall speed index 0..15 sent as an analog value
  CH_CAM1   = 6,  // CH7  reserved for camera ISO (phase 4)
  CH_CAM2   = 7,  // CH8  reserved for camera EV  (phase 4)
};

// Commands. CH4 carries (command * 2 + toggle). The toggle flips on every
// press, so pressing the same button twice is still seen as a new command.
enum : uint8_t {
  CMD_NONE     = 0,
  CMD_RECALL_1 = 1,  // 1..4 = recall P1..P4
  CMD_SAVE_1   = 5,  // 5..8 = save P1..P4
};

inline uint8_t  cmdStep(uint8_t cmd, uint8_t toggle) { return (uint8_t)(cmd * 2 + (toggle & 1)); }
inline uint8_t  cmdFromStep(uint8_t step) { return step / 2; }

// recall speed index 0..15 <-> -1..1 on CH6 (coarse but usable in Hybrid mode)
inline float   recallToNorm(uint8_t idx) { return (idx > 15 ? 15 : idx) / 7.5f - 1.f; }
inline uint8_t recallFromNorm(float v) {
  int i = (int)((v + 1.f) * 7.5f + 0.5f);
  return (uint8_t)(i < 0 ? 0 : (i > 15 ? 15 : i));
}

// Recall speed choices in degrees per second
static const uint8_t RECALL_DPS[16] = {5, 8, 10, 15, 20, 25, 30, 40, 50, 60, 75, 90, 110, 130, 160, 180};

// Status text the receiver sends back over ELRS telemetry (flight-mode frame):
//   "<STATE> <mask>"   e.g. "OK 1101", "NOCAN 0000", "SAVED2 1101", "MOVE1 1101"
//   mask = 4 characters, '1' if preset P1..P4 is stored on the receiver.
