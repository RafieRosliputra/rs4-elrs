/*
  rs4-elrs — RECEIVER (on the gimbal)
  https://github.com/RafieRosliputra/rs4-elrs

  Board : ESP32-C3 Super Mini  (Arduino IDE: "ESP32C3 Dev Module",
          USB CDC On Boot: Enabled)
  In    : BetaFPV ELRS Nano RX, CRSF 420000 baud
  Out   : DJI RS4 over CAN (DJI R SDK) through an SN65HVD230 transceiver,
          plugged into the focus wheel / RSA port

  What it does
    * CH1/CH2 -> pan/tilt SPEED control (the RS4 holds each command 0.5 s)
    * CH3     -> zoom: moves the Focus Pro Motor position (0..4095)
    * CH6/CH7 -> preset commands: save / recall P1..P4 (pan, tilt, zoom),
                 stored in flash. Recall moves with ease-in/ease-out at the
                 speed chosen on the remote (CH4). Touching the joystick
                 cancels a recall.
    * Failsafe: no ELRS channels for 0.3 s -> gimbal stops.
    * Sends "OK 1101"-style status back to the remote over ELRS telemetry.
    * Serial Monitor console (type "help") to drive the gimbal from the PC.
*/

#include <Arduino.h>
#include <Preferences.h>
#include "driver/twai.h"

#include "config.h"
#include "crsf.h"
#include "link_map.h"
#include "dji_sdk.h"

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// =====================================================================
//  ELRS / CRSF input
// =====================================================================
#define CR Serial1

uint16_t ch[16];
uint32_t lastChMs = 0;
uint32_t chFrames = 0;
crsf::LinkStats lstats;
uint32_t lastLinkMs = 0;

void onCrsf(uint8_t type, const uint8_t *p, uint8_t n, void *) {
  if (type == crsf::T_CHANNELS && n == 22) {
    crsf::unpackChannels(p, ch);
    lastChMs = millis();
    chFrames++;
  } else if (type == crsf::T_LINK_STATS) {
    if (crsf::parseLinkStats(p, n, lstats)) lastLinkMs = millis();
  }
}
crsf::Parser crsfParser(onCrsf, nullptr);

bool linkOk() { return lastChMs && millis() - lastChMs < LINK_TIMEOUT_MS; }

void sendStatusText(const char *s) {
#if USE_CRSF
  uint8_t f[24];
  size_t n = crsf::buildTextFrame(crsf::ADDR_FC, s, f);
  CR.write(f, n);
#endif
}

// =====================================================================
//  CAN / DJI R SDK
// =====================================================================
bool     canRunning = false;
uint16_t seq = 1;
uint32_t lastGimbalMs = 0;        // last valid DJI packet from the gimbal
uint32_t canTxOk = 0, canTxFail = 0;
bool     rawPrint = false;

int16_t  gYaw = 0, gRoll = 0, gPitch = 0;   // 0.1 deg
uint32_t lastAngleMs = 0;

struct Pending { uint16_t seq; uint8_t id; uint32_t t; };
Pending pending[8];
int     pendingIdx = 0;

void onPacket(uint8_t cmdType, uint16_t s, const uint8_t *data, size_t dlen, void *);
dji::Reassembler reasm(onPacket, nullptr);

bool canBegin() {
#if USE_CAN
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
  g.rx_queue_len = 32;
  g.tx_queue_len = 16;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) { Serial.println("[CAN] driver install failed"); return false; }
  if (twai_start() != ESP_OK) { Serial.println("[CAN] start failed"); return false; }
  canRunning = true;
  Serial.println("[CAN] running at 1 Mbit/s");
  return true;
#else
  return false;
#endif
}

void canSendPacket(uint8_t cmdType, uint8_t set, uint8_t id, const uint8_t *data, size_t dlen) {
#if USE_CAN
  if (!canRunning) return;
  uint8_t pkt[64];
  uint16_t s = seq++;
  size_t n = dji::buildPacket(cmdType, set, id, data, dlen, s, pkt);
  if (cmdType != dji::CT_NO_REPLY) {
    pending[pendingIdx] = {s, id, millis()};
    pendingIdx = (pendingIdx + 1) % 8;
  }
  for (size_t off = 0; off < n; off += 8) {
    twai_message_t m;
    memset(&m, 0, sizeof m);
    m.identifier = dji::CAN_ID_TX;
    m.data_length_code = (n - off) < 8 ? (n - off) : 8;
    memcpy(m.data, pkt + off, m.data_length_code);
    if (twai_transmit(&m, pdMS_TO_TICKS(3)) == ESP_OK) canTxOk++; else { canTxFail++; return; }
  }
#else
  (void)cmdType; (void)set; (void)id; (void)data; (void)dlen;
#endif
}

void canPoll() {
#if USE_CAN
  if (!canRunning) return;
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {
    if (rawPrint) {
      Serial.printf("[RAW] %03lX:", (unsigned long)m.identifier);
      for (int i = 0; i < m.data_length_code; i++) Serial.printf(" %02X", m.data[i]);
      Serial.println();
    }
    if (m.identifier == dji::CAN_ID_RX && !m.extd) reasm.push(m.data, m.data_length_code);
  }
  static uint32_t lastChk = 0;
  if (millis() - lastChk > 500) {
    lastChk = millis();
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
      if (st.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
      else if (st.state == TWAI_STATE_STOPPED) twai_start();
    }
  }
#endif
}

bool gimbalOk() { return lastGimbalMs && millis() - lastGimbalMs < 2000; }

void storeAngles(const uint8_t *p) {
  gYaw = dji::get16(p);
  gRoll = dji::get16(p + 2);
  gPitch = dji::get16(p + 4);
  lastAngleMs = millis();
}

void onPacket(uint8_t cmdType, uint16_t s, const uint8_t *data, size_t dlen, void *) {
  lastGimbalMs = millis();
  if (dlen < 2) return;
  uint8_t set = data[0], id = data[1];
  const uint8_t *p = data + 2;
  size_t n = dlen - 2;

  if (cmdType & dji::CT_IS_REPLY) {
    // find which request this answers
    int idMatch = -1;
    for (int i = 0; i < 8; i++) if (pending[i].seq == s) { idMatch = pending[i].id; break; }
    if (set == dji::SET_GIMBAL && (id == dji::ID_GET_ANGLE || idMatch == dji::ID_GET_ANGLE)) {
      // reply: ret, data_type, yaw, roll, pitch
      if (n >= 8 && p[0] == 0x00 && p[1] != 0x00) storeAngles(p + 2);
    } else if (n >= 1 && p[0] != 0x00 && rawPrint) {
      Serial.printf("[DJI] cmd 0x%02X 0x%02X returned error 0x%02X\n", set, id, p[0]);
    }
  } else if (set == dji::SET_GIMBAL && id == dji::ID_PUSH_DATA) {
    // push: ctrl, yaw, roll, pitch (attitude), yaw/roll/pitch joint, ...
    if (n >= 13 && (p[0] & 0x01)) storeAngles(ANGLE_SOURCE == 0x02 ? p + 7 : p + 1);
  }
}

void requestAngles() {
  uint8_t d[1] = {ANGLE_SOURCE};
  canSendPacket(dji::CT_NEED_REPLY, dji::SET_GIMBAL, dji::ID_GET_ANGLE, d, 1);
}

void sendSpeed(float yawDps, float pitchDps, bool takeOver) {
  uint8_t d[8];
  uint8_t ctrl = takeOver ? (0x80 | (SPEED_IGNORE_FOCAL ? 0x08 : 0x00)) : 0x00;
  size_t n = dji::speedData(d, (int16_t)lroundf(clampf(yawDps, -360, 360) * 10), 0,
                            (int16_t)lroundf(clampf(pitchDps, -360, 360) * 10), ctrl);
  canSendPacket(dji::CT_NO_REPLY, dji::SET_GIMBAL, dji::ID_SPEED, d, n);
}

void sendPosition(int16_t yaw, int16_t roll, int16_t pitch, uint8_t time01s, bool useRoll) {
  uint8_t d[8];
  uint8_t ctrl = 0x01 | (useRoll ? 0x00 : 0x04);
  size_t n = dji::posData(d, yaw, roll, pitch, ctrl, time01s);
  canSendPacket(dji::CT_NO_REPLY, dji::SET_GIMBAL, dji::ID_POSITION, d, n);
}

void sendFocus(uint16_t pos) {
  uint8_t d[8];
  size_t n = dji::focusData(d, pos);
  canSendPacket(dji::CT_NO_REPLY, dji::SET_GIMBAL, dji::ID_FOCUS, d, n);
}

void sendRecenter() {
  uint8_t d[2] = {0xFE, 0x01};
  canSendPacket(dji::CT_NEED_REPLY, dji::SET_GIMBAL, dji::ID_MODE, d, 2);
}

void enablePush() {
  uint8_t d[1] = {0x01};
  canSendPacket(dji::CT_NEED_REPLY, dji::SET_GIMBAL, dji::ID_PUSH_EN, d, 1);
}

// =====================================================================
//  Zoom (focus motor position, remembered in flash)
// =====================================================================
Preferences prefs;
float    zoomPos = 2048;
uint16_t zoomSent = 0xFFFF;
bool     zoomDirty = false;
uint32_t zoomSavedAt = 0;

void zoomSet(float v) {
  zoomPos = clampf(v, 0, 4095);
  uint16_t z = (uint16_t)lroundf(zoomPos);
  if (z != zoomSent) { sendFocus(z); zoomSent = z; zoomDirty = true; }
}

// =====================================================================
//  Presets
// =====================================================================
struct Preset { bool valid; int16_t yaw, roll, pitch; uint16_t zoom; };
Preset presets[4];

void loadPresets() {
  prefs.begin("rs4rx", true);
  if (prefs.getBytes("presets", presets, sizeof presets) != sizeof presets) memset(presets, 0, sizeof presets);
  zoomPos = prefs.getFloat("zoom", 2048);
  prefs.end();
}
void savePresets() {
  prefs.begin("rs4rx", false);
  prefs.putBytes("presets", presets, sizeof presets);
  prefs.end();
}
void saveZoom() {
  prefs.begin("rs4rx", false);
  prefs.putFloat("zoom", zoomPos);
  prefs.end();
  zoomDirty = false;
  zoomSavedAt = millis();
}

// ---- recall trajectory ----
struct Recall {
  bool active = false;
  int slot = 0;
  uint32_t startMs = 0, durMs = 0, lastStep = 0;
  float y0, p0, r0, z0, dy, dp, dr, dz;
} rc;

char statusWord[12] = "OK";
uint32_t statusUntil = 0;
void flashStatus(const char *w, uint32_t ms = 1500) {
  strncpy(statusWord, w, sizeof statusWord - 1);
  statusUntil = millis() + ms;
}

static float wrap1800(float a) {
  while (a > 1800) a -= 3600;
  while (a < -1800) a += 3600;
  return a;
}

bool anglesFresh() { return lastAngleMs && millis() - lastAngleMs < 500; }

void doSave(int i) {
  if (!anglesFresh()) {
    Serial.printf("[PRESET] save P%d FAILED: no angle data from the gimbal\n", i + 1);
    char w[8]; snprintf(w, sizeof w, "ERR%d", i + 1); flashStatus(w);
    return;
  }
  presets[i] = {true, gYaw, gRoll, gPitch, (uint16_t)lroundf(zoomPos)};
  savePresets();
  Serial.printf("[PRESET] saved P%d  yaw %.1f  pitch %.1f  roll %.1f  zoom %u\n", i + 1,
                gYaw / 10.0, gPitch / 10.0, gRoll / 10.0, presets[i].zoom);
  char w[8]; snprintf(w, sizeof w, "SAVED%d", i + 1); flashStatus(w);
}

void doRecall(int i, uint8_t speedIdx) {
  if (!presets[i].valid) {
    Serial.printf("[PRESET] P%d is empty\n", i + 1);
    char w[8]; snprintf(w, sizeof w, "EMPTY%d", i + 1); flashStatus(w);
    return;
  }
  if (!anglesFresh()) {
    Serial.printf("[PRESET] recall P%d FAILED: no angle data from the gimbal\n", i + 1);
    char w[8]; snprintf(w, sizeof w, "ERR%d", i + 1); flashStatus(w);
    return;
  }
  sendSpeed(0, 0, false);   // hand speed control back before position moves
  const Preset &p = presets[i];
  rc.y0 = gYaw; rc.p0 = gPitch; rc.r0 = gRoll; rc.z0 = zoomPos;
  rc.dy = wrap1800((float)p.yaw - gYaw);
  rc.dp = (float)p.pitch - gPitch;
  rc.dr = RECALL_ROLL ? (float)p.roll - gRoll : 0;
  rc.dz = (float)p.zoom - zoomPos;
  float maxDeg = fmaxf(fabsf(rc.dy), fmaxf(fabsf(rc.dp), fabsf(rc.dr))) / 10.f;
  float dps = RECALL_DPS[speedIdx > 15 ? 15 : speedIdx];
  // ease-in/out peaks at 1.5x the average speed, so stretch the time to match
  float secs = fmaxf(MIN_RECALL_S, 1.5f * maxDeg / dps);
  rc.durMs = (uint32_t)(secs * 1000);
  rc.startMs = millis();
  rc.lastStep = 0;
  rc.slot = i;
  rc.active = true;
  Serial.printf("[PRESET] recall P%d: %.1f deg in %.1f s\n", i + 1, maxDeg, secs);
}

void recallStep() {
  uint32_t now = millis();
  if (now - rc.lastStep < 50) return;
  rc.lastStep = now;
  float t = (now - rc.startMs) / (float)rc.durMs;
  bool done = t >= 1.f;
  if (done) t = 1.f;
  float s = t * t * (3.f - 2.f * t);           // smoothstep: ease in + ease out
  int16_t y = (int16_t)lroundf(wrap1800(rc.y0 + rc.dy * s));
  int16_t p = (int16_t)lroundf(rc.p0 + rc.dp * s);
  int16_t r = (int16_t)lroundf(rc.r0 + rc.dr * s);
  sendPosition(y, r, p, done ? 3 : 1, RECALL_ROLL);
  zoomSet(rc.z0 + rc.dz * s);
  if (done) {
    rc.active = false;
    Serial.printf("[PRESET] P%d reached\n", rc.slot + 1);
  }
}

void cancelRecall(const char *why) {
  if (!rc.active) return;
  rc.active = false;
  Serial.printf("[PRESET] recall cancelled (%s)\n", why);
}

// =====================================================================
//  Commands from the remote (CH4 = command * 2 + toggle)
// =====================================================================
int      lastStep = -1, candStep = -1;
uint8_t  candCount = 0;
bool     wasLinked = false;

void handleCommands() {
  static uint32_t lastFrameSeen = 0;
  if (!linkOk()) { wasLinked = false; return; }
  if (chFrames == lastFrameSeen) return;          // only look at new frames
  lastFrameSeen = chFrames;

  int step = crsf::toStep(ch[CH_CMD]);
  if (!wasLinked) {               // link (re)gained: sync, do not replay an old press
    wasLinked = true;
    lastStep = step;
    candStep = -1;
    Serial.println("[LINK] channels arriving");
    return;
  }
  if (step == lastStep) { candStep = -1; return; }
  // need the same new value in 2 frames in a row (ignores a single bad frame)
  if (step != candStep) { candStep = step; candCount = 1; return; }
  if (++candCount < 2) return;
  lastStep = step;
  candStep = -1;

  uint8_t cmd = cmdFromStep((uint8_t)step);
  uint8_t spd = recallFromNorm(crsf::toNorm(ch[CH_RECALL]));
  if (cmd >= CMD_RECALL_1 && cmd < CMD_RECALL_1 + 4) doRecall(cmd - CMD_RECALL_1, spd);
  else if (cmd >= CMD_SAVE_1 && cmd < CMD_SAVE_1 + 4) doSave(cmd - CMD_SAVE_1);
}

// =====================================================================
//  Motion
// =====================================================================
bool     speedActive = false;
uint32_t speedZeroSince = 0, lastSpeedMs = 0, lastZoomMs = 0;

// test motion from the serial console
float    testYaw = 0, testPitch = 0;
uint32_t testUntil = 0;

void motion() {
  uint32_t now = millis();
  bool lk = linkOk();
  float pan = lk ? crsf::toNorm(ch[CH_PAN]) : 0;
  float tilt = lk ? crsf::toNorm(ch[CH_TILT]) : 0;
  float zoom = lk ? crsf::toNorm(ch[CH_ZOOM]) : 0;
  if (fabsf(pan) < 0.01f) pan = 0;
  if (fabsf(tilt) < 0.01f) tilt = 0;
  if (fabsf(zoom) < 0.01f) zoom = 0;

  float yawDps = pan * MAX_PAN_DPS, pitchDps = tilt * MAX_TILT_DPS;
  if (now < testUntil) { yawDps = testYaw; pitchDps = testPitch; }

  if (rc.active) {
    if (fabsf(pan) > OVERRIDE_THRESH || fabsf(tilt) > OVERRIDE_THRESH || fabsf(zoom) > OVERRIDE_THRESH)
      cancelRecall("joystick moved");
    else if (!lk && lastChMs)
      cancelRecall("link lost");
    else { recallStep(); return; }
  }

  // zoom: integrate twist into a motor position
  if (now - lastZoomMs >= 20) {
    float dt = (now - lastZoomMs) / 1000.f;
    if (dt > 0.1f) dt = 0.1f;
    lastZoomMs = now;
    if (zoom != 0) zoomSet(zoomPos + zoom * ZOOM_FULL_RATE * dt);
  }

  // pan/tilt speed
  if (now - lastSpeedMs < SPEED_PERIOD_MS) return;
  lastSpeedMs = now;
  bool moving = yawDps != 0 || pitchDps != 0;
  if (moving) {
    sendSpeed(yawDps, pitchDps, true);
    speedActive = true;
    speedZeroSince = 0;
  } else if (speedActive) {
    // hold zero for a moment (clean stop), then give control back to the RS4
    if (!speedZeroSince) speedZeroSince = now;
    if (now - speedZeroSince < 300) sendSpeed(0, 0, true);
    else { sendSpeed(0, 0, false); speedActive = false; }
  }
}

// =====================================================================
//  Status back to the remote
// =====================================================================
void statusText(char *out, size_t len) {
  const char *w;
  if (!USE_CAN) w = "NOCAN";
  else if (millis() < statusUntil) w = statusWord;
  else if (!gimbalOk()) w = "NOCAN";
  else if (rc.active) { static char m[8]; snprintf(m, sizeof m, "MOVE%d", rc.slot + 1); w = m; }
  else w = "OK";
  char mask[5];
  for (int i = 0; i < 4; i++) mask[i] = presets[i].valid ? '1' : '0';
  mask[4] = 0;
  snprintf(out, len, "%s %s", w, mask);
}

// =====================================================================
//  Serial console
// =====================================================================
void printHelp() {
  Serial.println(
    "\nCommands (Serial Monitor, 115200, Newline):\n"
    "  help            this list\n"
    "  a               print gimbal angles\n"
    "  s1..s4          save preset 1..4 from the current position\n"
    "  r1..r4          recall preset 1..4 (speed 30 deg/s)\n"
    "  list            show stored presets\n"
    "  clear           erase all presets\n"
    "  c               recenter the gimbal\n"
    "  v <pan> <tilt>  move at deg/s for 2 s, e.g. v 10 0\n"
    "  p <yaw> <pitch> go to absolute angles in 3 s, e.g. p 30 -10\n"
    "  z <0-4095>      focus motor (zoom) position\n"
    "  stop            stop everything\n"
    "  raw             toggle printing of every CAN frame\n");
}

void console() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (line.length() < 60) line += c; continue; }
    line.trim();
    String cmd = line;
    line = "";
    if (!cmd.length()) continue;
    Serial.printf("> %s\n", cmd.c_str());

    if (cmd == "help" || cmd == "?") printHelp();
    else if (cmd == "a") {
      Serial.printf("yaw %.1f  pitch %.1f  roll %.1f  (%s, %lu ms old)  zoom %.0f\n", gYaw / 10.0, gPitch / 10.0,
                    gRoll / 10.0, ANGLE_SOURCE == 2 ? "joint" : "attitude",
                    (unsigned long)(lastAngleMs ? millis() - lastAngleMs : 0), zoomPos);
    } else if (cmd.length() == 2 && (cmd[0] == 's' || cmd[0] == 'r') && cmd[1] >= '1' && cmd[1] <= '4') {
      int i = cmd[1] - '1';
      if (cmd[0] == 's') doSave(i); else doRecall(i, 6);
    } else if (cmd == "list") {
      for (int i = 0; i < 4; i++) {
        if (presets[i].valid)
          Serial.printf("P%d  yaw %.1f  pitch %.1f  roll %.1f  zoom %u\n", i + 1, presets[i].yaw / 10.0,
                        presets[i].pitch / 10.0, presets[i].roll / 10.0, presets[i].zoom);
        else Serial.printf("P%d  (empty)\n", i + 1);
      }
    } else if (cmd == "clear") {
      memset(presets, 0, sizeof presets); savePresets(); Serial.println("presets erased");
    } else if (cmd == "c") {
      sendRecenter();
    } else if (cmd.startsWith("v ")) {
      float a = 0, b = 0;
      sscanf(cmd.c_str() + 2, "%f %f", &a, &b);
      testYaw = a; testPitch = b; testUntil = millis() + 2000;
    } else if (cmd.startsWith("p ")) {
      float a = 0, b = 0;
      sscanf(cmd.c_str() + 2, "%f %f", &a, &b);
      cancelRecall("console");
      sendSpeed(0, 0, false);
      sendPosition((int16_t)lroundf(a * 10), 0, (int16_t)lroundf(b * 10), 30, false);
    } else if (cmd.startsWith("z ")) {
      zoomSet(cmd.substring(2).toFloat());
    } else if (cmd == "stop") {
      cancelRecall("console"); testUntil = 0; sendSpeed(0, 0, true); sendSpeed(0, 0, false); speedActive = false;
    } else if (cmd == "raw") {
      rawPrint = !rawPrint; Serial.printf("raw CAN print %s\n", rawPrint ? "ON" : "OFF");
    } else Serial.println("unknown command, type help");
  }
}

// =====================================================================
//  Setup / loop
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nrs4-elrs RECEIVER starting");
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  loadPresets();
  zoomSent = (uint16_t)lroundf(zoomPos);   // assume the motor is where we left it

#if USE_CRSF
  CR.begin(CRSF_BAUD, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);
#endif
  canBegin();
  Serial.printf("CRSF %s, CAN %s. Type \"help\" for console commands.\n",
                USE_CRSF ? "ON" : "OFF", USE_CAN ? "ON" : "OFF");
}

void loop() {
  static uint32_t lastAngle = 0, lastStatus = 0, lastPush = 0, lastDbg = 0;
  uint32_t now = millis();

#if USE_CRSF
  while (CR.available()) crsfParser.push((uint8_t)CR.read());
#endif
  canPoll();
  console();
  handleCommands();
  motion();

  if (USE_CAN && now - lastAngle >= ANGLE_PERIOD_MS) { lastAngle = now; requestAngles(); }
  if (USE_CAN && !gimbalOk() && now - lastPush >= 3000) { lastPush = now; enablePush(); }
  if (zoomDirty && now - zoomSavedAt > 5000) saveZoom();

  if (now - lastStatus >= STATUS_PERIOD_MS) {
    lastStatus = now;
    char s[20];
    statusText(s, sizeof s);
    sendStatusText(s);
  }

  // LED: on = link + gimbal OK, slow blink = link only, fast blink = nothing
  bool lk = linkOk(), gk = gimbalOk();
  bool lit = (lk && gk) ? true : (lk ? (now / 500) % 2 : (now / 120) % 2);
  digitalWrite(PIN_LED, lit ? LOW : HIGH);

#if DEBUG_PRINT
  if (now - lastDbg >= 500) {
    lastDbg = now;
    char s[20];
    statusText(s, sizeof s);
    Serial.printf("link %s", lk ? "OK " : "-- ");
    if (lk) {
      uint8_t st = crsf::toStep(ch[CH_CMD]);
      Serial.printf("pan%+.2f tilt%+.2f zoom%+.2f cmd%u/%u recall%udps", crsf::toNorm(ch[CH_PAN]),
                    crsf::toNorm(ch[CH_TILT]), crsf::toNorm(ch[CH_ZOOM]), cmdFromStep(st), st & 1,
                    RECALL_DPS[recallFromNorm(crsf::toNorm(ch[CH_RECALL]))]);
      if (lastLinkMs && now - lastLinkMs < 1000) Serial.printf(" LQ%u -%udBm", lstats.upLq, lstats.upRssi1);
    }
    Serial.printf(" | gimbal %s yaw%.1f pitch%.1f | zoom %.0f | can tx%lu fail%lu pk%lu bad%lu | \"%s\"\n",
                  gk ? "OK" : "--", gYaw / 10.0, gPitch / 10.0, zoomPos, (unsigned long)canTxOk,
                  (unsigned long)canTxFail, (unsigned long)reasm.good, (unsigned long)reasm.bad, s);
  }
#endif
  delay(1);
}
