// Host unit tests for the protocol helpers.
//   g++ -std=c++17 -Wall -Wextra -I receiver test/test_protocol.cpp -o /tmp/t && /tmp/t
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dji_sdk.h"
#include "crsf.h"
#include "link_map.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void testDjiExample() {
  // Example from the DJI R SDK document: absolute position, yaw 3.2, roll 4.8,
  // pitch 6.4 deg, 2 s, seq 0x1122, reply required.
  const uint8_t want[] = {0xAA, 0x1A, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x22, 0x11, 0xA2, 0x42, 0x0E,
                          0x00, 0x20, 0x00, 0x30, 0x00, 0x40, 0x00, 0x01, 0x14, 0x7B, 0x40, 0x97, 0xBE};
  uint8_t d[8], out[64];
  size_t dl = dji::posData(d, 32, 48, 64, 0x01, 20);
  size_t n = dji::buildPacket(dji::CT_NEED_REPLY, dji::SET_GIMBAL, dji::ID_POSITION, d, dl, 0x1122, out);
  CHECK(n == sizeof(want));
  CHECK(memcmp(out, want, sizeof(want)) == 0);
}

static int got = 0;
static void onPkt(uint8_t ct, uint16_t seq, const uint8_t *data, size_t dlen, void *) {
  got++;
  CHECK(ct == 0x03);
  CHECK(seq == 0x1122);
  CHECK(data[0] == 0x0E && data[1] == 0x00);
  CHECK(dlen == 10);
}

static void testReassembly() {
  uint8_t d[8], pkt[64];
  size_t dl = dji::posData(d, 32, 48, 64, 0x01, 20);
  size_t n = dji::buildPacket(0x03, 0x0E, 0x00, d, dl, 0x1122, pkt);
  dji::Reassembler r(onPkt, nullptr);
  const uint8_t junk[] = {0x00, 0xAA, 0x05};
  r.push(junk, sizeof junk);                       // garbage first
  for (size_t i = 0; i < n; i += 8) r.push(pkt + i, (n - i) < 8 ? (n - i) : 8);  // CAN-sized pieces
  r.push(pkt, n);                                  // and a second whole packet
  CHECK(got == 2);
  pkt[15] ^= 1;                                    // corrupt -> rejected
  r.push(pkt, n);
  CHECK(got == 2);
}

static uint16_t seen[16];
static int frames = 0;
static void onCrsf(uint8_t type, const uint8_t *p, uint8_t n, void *) {
  if (type == crsf::T_CHANNELS && n == 22) { crsf::unpackChannels(p, seen); frames++; }
}

static void testCrsf() {
  uint16_t ch[16];
  for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(172 + i * 100);
  uint8_t f[26];
  size_t n = crsf::buildChannelsFrame(crsf::ADDR_FC, ch, f);
  CHECK(n == 26);
  CHECK(f[1] == 24 && f[2] == 0x16);
  crsf::Parser p(onCrsf, nullptr);
  p.push(0x55);
  for (size_t i = 0; i < n; i++) p.push(f[i]);
  CHECK(frames == 1);
  CHECK(memcmp(seen, ch, sizeof ch) == 0);
  for (int s = 0; s < 32; s++) CHECK(crsf::toStep(crsf::fromStep(s)) == s);
  for (int s = 0; s < 16; s++) CHECK(crsf::toStep(crsf::fromStep(s, 100), 100) == s);
  CHECK(crsf::toStep(crsf::fromStep(7) + 20) == 7);  // tolerant to 10-bit rounding
  CHECK(crsf::toStep(crsf::fromStep(7) - 20) == 7);
  // every command/toggle pair survives the round trip on CH4
  for (int c = 0; c <= 8; c++)
    for (int t = 0; t < 2; t++) {
      uint8_t st = crsf::toStep(crsf::fromStep(cmdStep(c, t)));
      CHECK(cmdFromStep(st) == c && (st & 1) == t);
    }
  for (int i = 0; i < 16; i++) CHECK(recallFromNorm(crsf::toNorm(crsf::fromNorm(recallToNorm(i)))) == i);
  CHECK(crsf::fromNorm(0) == 992 && crsf::fromNorm(1) == 1811 && crsf::fromNorm(-1) == 173);
  uint8_t t[32];
  size_t tn = crsf::buildTextFrame(crsf::ADDR_FC, "OK 1101", t);
  CHECK(tn == 4 + 8 && t[2] == 0x21 && t[3 + 7] == 0);
  // CRC8 known value: CRSF CRC8 (poly 0xD5) of "123456789" is 0xBC
  CHECK(crsf::crc8((const uint8_t *)"123456789", 9) == 0xBC);
}

int main() {
  testDjiExample();
  testReassembly();
  testCrsf();
  if (fails) { printf("%d check(s) failed\n", fails); return 1; }
  printf("all protocol tests passed\n");
  return 0;
}
