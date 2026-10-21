// rs4-elrs — CRSF helpers shared by the remote and the receiver.
// The same file is copied into remote/ and receiver/ (Arduino IDE needs
// each sketch folder to be self-contained). Keep both copies identical.
//
// Plain C++ only (no Arduino calls) so it can be unit-tested on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace crsf {

// ---- addresses / frame types ----
constexpr uint8_t ADDR_FC        = 0xC8;  // flight controller (our receiver ESP32)
constexpr uint8_t ADDR_RADIO     = 0xEA;  // handset (our remote ESP32)
constexpr uint8_t ADDR_RX        = 0xEC;
constexpr uint8_t ADDR_TX_MODULE = 0xEE;  // ELRS TX module (Ranger Micro)

constexpr uint8_t T_BATTERY    = 0x08;
constexpr uint8_t T_LINK_STATS = 0x14;
constexpr uint8_t T_CHANNELS   = 0x16;
constexpr uint8_t T_FLIGHTMODE = 0x21;
constexpr uint8_t T_RADIO_ID   = 0x3A;

// ---- channel values (CRSF "ticks") ----
constexpr uint16_t CH_MIN = 172;
constexpr uint16_t CH_MID = 992;
constexpr uint16_t CH_MAX = 1811;
constexpr float    CH_HALF = 819.0f;

inline uint8_t crc8(const uint8_t *p, size_t n) {
  uint8_t c = 0;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0xD5) : (uint8_t)(c << 1);
  }
  return c;
}

// -1..1  <->  172..1811
inline uint16_t fromNorm(float v) {
  if (v > 1.f) v = 1.f;
  if (v < -1.f) v = -1.f;
  float t = CH_MID + v * CH_HALF;
  return (uint16_t)(t + 0.5f);
}
inline float toNorm(uint16_t v) {
  float f = ((float)v - CH_MID) / CH_HALF;
  return f > 1.f ? 1.f : (f < -1.f ? -1.f : f);
}

// Discrete values sent on a channel: 200, 200+step, 200+2*step ...
// step 50 -> 0..31, step 100 -> 0..15. Far apart enough to survive ELRS
// 10-bit resolution (CH1-4 are always 10-bit, in every switch mode).
inline uint16_t fromStep(uint8_t s, uint16_t step = 50) {
  uint8_t maxS = (uint8_t)((1750 - 200) / step);
  return (uint16_t)(200 + (s > maxS ? maxS : s) * step);
}
inline uint8_t toStep(uint16_t v, uint16_t step = 50) {
  int maxS = (1750 - 200) / step;
  int s = ((int)v - 200 + (int)step / 2) / (int)step;
  return (uint8_t)(s < 0 ? 0 : (s > maxS ? maxS : s));
}

// 16 x 11-bit channels -> 22 bytes, LSB first
inline void packChannels(const uint16_t ch[16], uint8_t out[22]) {
  memset(out, 0, 22);
  uint32_t bit = 0;
  for (int i = 0; i < 16; i++) {
    uint16_t v = ch[i] & 0x07FF;
    for (int k = 0; k < 11; k++, bit++)
      if (v & (1u << k)) out[bit >> 3] |= (uint8_t)(1u << (bit & 7));
  }
}
inline void unpackChannels(const uint8_t in[22], uint16_t ch[16]) {
  uint32_t bit = 0;
  for (int i = 0; i < 16; i++) {
    uint16_t v = 0;
    for (int k = 0; k < 11; k++, bit++)
      if (in[bit >> 3] & (1u << (bit & 7))) v |= (uint16_t)(1u << k);
    ch[i] = v;
  }
}

// Full frame: [addr][len][type][payload...][crc]  len = type + payload + crc
inline size_t buildFrame(uint8_t addr, uint8_t type, const uint8_t *payload, size_t plen, uint8_t *out) {
  out[0] = addr;
  out[1] = (uint8_t)(plen + 2);
  out[2] = type;
  if (plen) memcpy(out + 3, payload, plen);
  out[3 + plen] = crc8(out + 2, plen + 1);
  return plen + 4;
}

inline size_t buildChannelsFrame(uint8_t addr, const uint16_t ch[16], uint8_t out[26]) {
  uint8_t p[22];
  packChannels(ch, p);
  return buildFrame(addr, T_CHANNELS, p, 22, out);
}

// Flight-mode text (null terminated, max 15 chars of text)
inline size_t buildTextFrame(uint8_t addr, const char *text, uint8_t *out) {
  uint8_t p[16];
  size_t n = strlen(text);
  if (n > 15) n = 15;
  memcpy(p, text, n);
  p[n] = 0;
  return buildFrame(addr, T_FLIGHTMODE, p, n + 1, out);
}

struct LinkStats {
  uint8_t upRssi1, upRssi2, upLq;  // rssi as positive number, meaning -dBm
  int8_t  upSnr;
  uint8_t antenna, rfMode, txPower;
  uint8_t downRssi, downLq;
  int8_t  downSnr;
};

inline bool parseLinkStats(const uint8_t *p, size_t n, LinkStats &s) {
  if (n < 10) return false;
  s.upRssi1 = p[0]; s.upRssi2 = p[1]; s.upLq = p[2]; s.upSnr = (int8_t)p[3];
  s.antenna = p[4]; s.rfMode = p[5]; s.txPower = p[6];
  s.downRssi = p[7]; s.downLq = p[8]; s.downSnr = (int8_t)p[9];
  return true;
}

// Byte-by-byte frame parser. Calls cb(type, payload, payloadLen, user) for
// every frame with a valid CRC.
typedef void (*FrameCb)(uint8_t type, const uint8_t *payload, uint8_t plen, void *user);

class Parser {
 public:
  Parser(FrameCb cb, void *user) : cb_(cb), user_(user) {}
  uint32_t good = 0, bad = 0;

  void push(uint8_t b) {
    if (n_ == 0) {
      if (b == ADDR_FC || b == ADDR_RADIO || b == ADDR_RX || b == ADDR_TX_MODULE) buf_[n_++] = b;
      return;
    }
    if (n_ == 1) {
      if (b < 2 || b > 62) { n_ = 0; return; }
      buf_[n_++] = b;
      return;
    }
    buf_[n_++] = b;
    uint8_t len = buf_[1];
    if (n_ == (size_t)len + 2) {
      if (crc8(buf_ + 2, len - 1) == buf_[len + 1]) {
        good++;
        if (cb_) cb_(buf_[2], buf_ + 3, (uint8_t)(len - 2), user_);
      } else {
        bad++;
      }
      n_ = 0;
    }
  }
  void reset() { n_ = 0; }

 private:
  FrameCb cb_;
  void *user_;
  uint8_t buf_[64];
  size_t n_ = 0;
};

}  // namespace crsf
