// rs4-elrs — DJI R SDK packet helpers (RS 2 / RS 3 Pro / RS 4 over CAN)
//
// Packet: AA | len(10 bit)+ver | cmdType | enc | res x3 | seq x2 | crc16 x2 |
//         cmdSet | cmdId | data... | crc32 x4          (all little-endian)
// CAN: 1 Mbit/s, standard IDs. We send on 0x223, the gimbal answers on 0x222.
// The packet is cut into 8-byte CAN frames in order.
//
// CRCs (from the DJI R SDK document, checked against its example packet):
//   CRC-16: poly 0x8005, init 0xC55C, reflected in/out, xorout 0
//   CRC-32: poly 0x04C11DB7, init 0xC55C0000, reflected in/out, xorout 0
// In the reflected (LSB-first) form used below the init values become
// 0x3AA3 and 0x00003AA3.
//
// Plain C++ (no Arduino calls) so it can be unit-tested on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace dji {

constexpr uint32_t CAN_ID_TX = 0x223;   // ESP32 -> gimbal
constexpr uint32_t CAN_ID_RX = 0x222;   // gimbal -> ESP32

constexpr uint8_t CT_NO_REPLY   = 0x00;
constexpr uint8_t CT_NEED_REPLY = 0x03;
constexpr uint8_t CT_IS_REPLY   = 0x20;  // bit 5 of cmdType

// Command set 0x0E: handheld gimbal
constexpr uint8_t SET_GIMBAL    = 0x0E;
constexpr uint8_t ID_POSITION   = 0x00;
constexpr uint8_t ID_SPEED      = 0x01;
constexpr uint8_t ID_GET_ANGLE  = 0x02;
constexpr uint8_t ID_PUSH_EN    = 0x07;
constexpr uint8_t ID_PUSH_DATA  = 0x08;
constexpr uint8_t ID_MODE       = 0x0E;  // recenter / follow modes
constexpr uint8_t ID_FOCUS      = 0x12;  // focus motor

inline uint16_t crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0x3AA3;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
  }
  return c;
}

inline uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0x00003AA3;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1);
  }
  return c;
}

inline void put16(uint8_t *p, int16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((uint16_t)v >> 8); }
inline int16_t get16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

// Returns total packet length (16 + 2 + dlen). `out` needs that many bytes.
inline size_t buildPacket(uint8_t cmdType, uint8_t cmdSet, uint8_t cmdId,
                          const uint8_t *data, size_t dlen, uint16_t seq, uint8_t *out) {
  size_t len = 12 + 2 + dlen + 4;
  out[0] = 0xAA;
  out[1] = (uint8_t)(len & 0xFF);
  out[2] = (uint8_t)((len >> 8) & 0x03);   // version 0 in the upper 6 bits
  out[3] = cmdType;
  out[4] = 0;                              // no encryption
  out[5] = out[6] = out[7] = 0;
  out[8] = (uint8_t)(seq & 0xFF);
  out[9] = (uint8_t)(seq >> 8);
  uint16_t c16 = crc16(out, 10);
  out[10] = (uint8_t)(c16 & 0xFF);
  out[11] = (uint8_t)(c16 >> 8);
  out[12] = cmdSet;
  out[13] = cmdId;
  if (dlen) memcpy(out + 14, data, dlen);
  uint32_t c32 = crc32(out, len - 4);
  out[len - 4] = (uint8_t)(c32);
  out[len - 3] = (uint8_t)(c32 >> 8);
  out[len - 2] = (uint8_t)(c32 >> 16);
  out[len - 1] = (uint8_t)(c32 >> 24);
  return len;
}

// ---- command payloads ----

// Position control. Angles in 0.1 deg (-1800..1800). time in 0.1 s.
// ctrl: bit0 1 = absolute; bit1 yaw invalid; bit2 roll invalid; bit3 pitch invalid
inline size_t posData(uint8_t *d, int16_t yaw, int16_t roll, int16_t pitch, uint8_t ctrl, uint8_t time) {
  put16(d, yaw); put16(d + 2, roll); put16(d + 4, pitch);
  d[6] = ctrl; d[7] = time;
  return 8;
}

// Speed control in 0.1 deg/s. Each command lasts 0.5 s.
// ctrl: 0x80 = take over speed control, 0x00 = release; |0x08 = ignore focal length
inline size_t speedData(uint8_t *d, int16_t yaw, int16_t roll, int16_t pitch, uint8_t ctrl) {
  put16(d, yaw); put16(d + 2, roll); put16(d + 4, pitch);
  d[6] = ctrl;
  return 7;
}

// Focus motor absolute position 0..4095 (Focus Pro Motor on the zoom ring)
inline size_t focusData(uint8_t *d, uint16_t pos) {
  if (pos > 4095) pos = 4095;
  d[0] = 0x01;   // sub command: position control
  d[1] = 0x00;   // control type: focus
  d[2] = 0x02;   // data length
  d[3] = (uint8_t)(pos & 0xFF);
  d[4] = (uint8_t)(pos >> 8);
  return 5;
}

// ---- receive side: rebuild packets from 8-byte CAN frames ----
typedef void (*PacketCb)(uint8_t cmdType, uint16_t seq, const uint8_t *data, size_t dlen, void *user);
// data points at cmdSet (data[0]) / cmdId (data[1]) / payload...

class Reassembler {
 public:
  Reassembler(PacketCb cb, void *user) : cb_(cb), user_(user) {}
  uint32_t good = 0, bad = 0;

  void push(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
      if (n_ >= sizeof(buf_)) drop(1);
      buf_[n_++] = p[i];
    }
    process();
  }
  void reset() { n_ = 0; }

 private:
  void drop(size_t k) {
    if (k >= n_) { n_ = 0; return; }
    memmove(buf_, buf_ + k, n_ - k);
    n_ -= k;
  }
  void process() {
    for (;;) {
      size_t s = 0;
      while (s < n_ && buf_[s] != 0xAA) s++;
      if (s) drop(s);
      if (n_ < 12) return;
      size_t len = buf_[1] | ((buf_[2] & 0x03) << 8);
      uint16_t c16 = (uint16_t)(buf_[10] | (buf_[11] << 8));
      if (len < 18 || len > sizeof(buf_) || crc16(buf_, 10) != c16) { bad++; drop(1); continue; }
      if (n_ < len) return;
      uint32_t c32 = (uint32_t)buf_[len - 4] | ((uint32_t)buf_[len - 3] << 8) |
                     ((uint32_t)buf_[len - 2] << 16) | ((uint32_t)buf_[len - 1] << 24);
      if (crc32(buf_, len - 4) != c32) { bad++; drop(1); continue; }
      good++;
      if (cb_) cb_(buf_[3], (uint16_t)(buf_[8] | (buf_[9] << 8)), buf_ + 12, len - 16, user_);
      drop(len);
    }
  }

  PacketCb cb_;
  void *user_;
  uint8_t buf_[300];
  size_t n_ = 0;
};

}  // namespace dji
