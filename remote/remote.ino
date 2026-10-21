/*
  rs4-elrs — REMOTE (hand unit)
  https://github.com/RafieRosliputra/rs4-elrs

  Board : ESP32-S3 DevKitC N16R8  (Arduino IDE: "ESP32S3 Dev Module")
  Link  : RadioMaster Ranger Micro, driven directly with CRSF (no radio handset)

  What it does
    * Joystick X/Y  -> pan/tilt speed     (deadzone, expo, ramp, invert)
    * Joystick twist-> zoom rate          (Focus Pro Motor on the RS4)
    * Joystick button: tap = movement speed 1>2>3>4, hold 1 s = recall P1
    * P1..P4       : tap = recall, hold 2 s = save (pan + tilt + zoom)
    * MOVE 1..4    : movement speed       ZOOM 1..4 : zoom max speed
    * WiFi button  : settings page on your phone (http://192.168.4.1)
    * Screen       : movement speed, zoom speed, LQ/RSSI, receiver status,
                     which presets are stored, "SAVED P2" style notices.

  Libraries (Library Manager): "Adafruit ST7735 and ST7789 Library",
                               "Adafruit GFX Library"
*/

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

#include "config.h"
#include "crsf.h"
#include "link_map.h"
#include "web_page.h"

#if ENABLE_TFT
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
#endif

// =====================================================================
//  Settings (flash)
// =====================================================================
struct Settings {
  uint8_t  version   = 3;
  uint16_t cMin[3]   = {0, 0, 0};
  uint16_t cMid[3]   = {2048, 2048, 2048};
  uint16_t cMax[3]   = {4095, 4095, 4095};
  bool     inv[3]    = {false, false, false};   // pan, tilt, zoom
  float    deadzone  = 0.06f;
  float    expo      = 0.40f;
  float    ramp      = 4.0f;                    // full scale per second, 0 = off
  uint8_t  movePct[4] = {10, 25, 55, 100};      // % of receiver max speed
  uint8_t  zoomPct[4] = {15, 35, 65, 100};
  uint8_t  recallIdx = 6;                       // RECALL_DPS[6] = 30 deg/s
  uint8_t  moveLevel = 2;                       // 1..4
  uint8_t  zoomLevel = 2;                       // 1..4
};

Settings    cfg;
Preferences prefs;
bool        cfgDirty = false;
uint32_t    cfgDirtyAt = 0;

void loadSettings() {
  Settings tmp;
  prefs.begin("rs4remote", true);
  size_t n = prefs.getBytes("cfg", &tmp, sizeof(tmp));
  prefs.end();
  if (n == sizeof(tmp) && tmp.version == Settings().version) cfg = tmp;
}
void saveSettings() {
  prefs.begin("rs4remote", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
  cfgDirty = false;
}
void markDirty() { cfgDirty = true; cfgDirtyAt = millis(); }

// =====================================================================
//  Joystick
// =====================================================================
const uint8_t AXIS_PIN[3] = {PIN_JOY_X, PIN_JOY_Y, PIN_JOY_Z};
float rawF[3] = {2048, 2048, 2048};   // filtered raw ADC
float norm[3];                        // after calibration, deadzone, expo, invert
float out[3];                         // after speed level + ramp (sent on CH1..3)
bool  calRange = false;               // recording min/max from the web page

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float axisNorm(int i) {
  float r = rawF[i], mid = cfg.cMid[i];
  float v;
  if (r >= mid) v = (r - mid) / fmaxf(1.f, (float)cfg.cMax[i] - mid);
  else          v = (r - mid) / fmaxf(1.f, mid - (float)cfg.cMin[i]);
  v = clampf(v, -1.f, 1.f);
  float a = fabsf(v), dz = clampf(cfg.deadzone, 0.f, 0.5f);
  if (a < dz) return 0.f;
  a = (a - dz) / (1.f - dz);
  float e = clampf(cfg.expo, 0.f, 1.f);
  a = (1.f - e) * a + e * a * a * a;
  v = v < 0 ? -a : a;
  return cfg.inv[i] ? -v : v;
}

void readJoystick(float dt) {
  for (int i = 0; i < 3; i++) {
    int r = analogRead(AXIS_PIN[i]);
    rawF[i] = rawF[i] * 0.6f + r * 0.4f;
    if (calRange) {
      if (rawF[i] < cfg.cMin[i]) cfg.cMin[i] = (uint16_t)rawF[i];
      if (rawF[i] > cfg.cMax[i]) cfg.cMax[i] = (uint16_t)rawF[i];
    }
    norm[i] = axisNorm(i);
  }
  float lvlMove = cfg.movePct[constrain(cfg.moveLevel, 1, 4) - 1] / 100.f;
  float lvlZoom = cfg.zoomPct[constrain(cfg.zoomLevel, 1, 4) - 1] / 100.f;
  float target[3] = {norm[0] * lvlMove, norm[1] * lvlMove, norm[2] * lvlZoom};
  for (int i = 0; i < 3; i++) {
    if (cfg.ramp <= 0.f) { out[i] = target[i]; continue; }
    float step = cfg.ramp * dt;
    if (target[i] > out[i] + step) out[i] += step;
    else if (target[i] < out[i] - step) out[i] -= step;
    else out[i] = target[i];
  }
}

// =====================================================================
//  Buttons
// =====================================================================
struct Button {
  uint8_t  pin;
  bool     down = false, rawLast = false, holdFired = false;
  uint32_t changedAt = 0, pressedAt = 0;
  bool     pressEdge = false, releaseEdge = false;
  uint32_t heldMs = 0;

  void begin() { pinMode(pin, INPUT_PULLUP); }
  void update(uint32_t now) {
    pressEdge = releaseEdge = false;
    bool raw = digitalRead(pin) == LOW;
    if (raw != rawLast) { rawLast = raw; changedAt = now; }
    if (raw != down && now - changedAt >= DEBOUNCE_MS) {
      down = raw;
      if (down) { pressEdge = true; pressedAt = now; holdFired = false; }
      else      { releaseEdge = true; }
    }
    heldMs = down ? now - pressedAt : 0;
  }
};

enum { B_P1, B_P2, B_P3, B_P4, B_M1, B_M2, B_M3, B_M4, B_Z1, B_Z2, B_Z3, B_Z4, B_WIFI, B_JOY, B_COUNT };
Button btn[B_COUNT] = {
  {PIN_P1}, {PIN_P2}, {PIN_P3}, {PIN_P4},
  {PIN_MOVE1}, {PIN_MOVE2}, {PIN_MOVE3}, {PIN_MOVE4},
  {PIN_ZOOM1}, {PIN_ZOOM2}, {PIN_ZOOM3}, {PIN_ZOOM4},
  {PIN_WIFI}, {PIN_JOY_BTN},
};
const char *BTN_NAME[B_COUNT] = {"P1", "P2", "P3", "P4", "MOVE1", "MOVE2", "MOVE3", "MOVE4",
                                 "ZOOM1", "ZOOM2", "ZOOM3", "ZOOM4", "WIFI", "JOY"};

// =====================================================================
//  Commands to the receiver (CH4 = command * 2 + toggle)
// =====================================================================
uint8_t  cmdValue = CMD_NONE;
uint8_t  cmdToggle = 0;
char     notice[20] = "";
uint32_t noticeAt = 0;

void showNotice(const char *s) {
  strncpy(notice, s, sizeof(notice) - 1);
  notice[sizeof(notice) - 1] = 0;
  noticeAt = millis();
}

void sendCommand(uint8_t value, const char *label) {
  cmdValue = value;
  cmdToggle ^= 1;
  showNotice(label);
#if DEBUG_SERIAL
  Serial.printf("[CMD] %s (CH4 step %u)\n", label, cmdStep(cmdValue, cmdToggle));
#endif
}

// =====================================================================
//  CRSF to the Ranger Micro (half-duplex on one wire)
// =====================================================================
#define CR Serial1

crsf::LinkStats link;
uint32_t lastLinkMs = 0, lastStatusMs = 0;
char     rxStatus[20] = "";          // text from the receiver, e.g. "OK 1101"
uint16_t chOut[16];

void onCrsfFrame(uint8_t type, const uint8_t *p, uint8_t n, void *) {
  if (type == crsf::T_LINK_STATS) {
    if (crsf::parseLinkStats(p, n, link)) lastLinkMs = millis();
  } else if (type == crsf::T_FLIGHTMODE) {
    size_t k = 0;
    while (k < n && k < sizeof(rxStatus) - 1 && p[k]) { rxStatus[k] = (char)p[k]; k++; }
    rxStatus[k] = 0;
    lastStatusMs = millis();
  }
}
crsf::Parser crsfParser(onCrsfFrame, nullptr);

#if ENABLE_CRSF && CRSF_TELEMETRY
static void lineTx() {
  gpio_set_direction((gpio_num_t)PIN_CRSF, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_gpio_connect_out_signal(PIN_CRSF, U1TXD_OUT_IDX, false, false);
}
static void lineRx() {
  esp_rom_gpio_connect_out_signal(PIN_CRSF, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_direction((gpio_num_t)PIN_CRSF, GPIO_MODE_INPUT);
  gpio_pullup_en((gpio_num_t)PIN_CRSF);
  esp_rom_gpio_connect_in_signal(PIN_CRSF, U1RXD_IN_IDX, false);
}
#endif

void crsfBegin() {
#if ENABLE_CRSF
#if CRSF_TELEMETRY
  CR.begin(CRSF_BAUD, SERIAL_8N1, PIN_CRSF, PIN_CRSF);
  lineRx();
#else
  CR.begin(CRSF_BAUD, SERIAL_8N1, PIN_CRSF_UNUSED_RX, PIN_CRSF);
#endif
#endif
}

void buildChannels() {
  for (int i = 0; i < 16; i++) chOut[i] = crsf::CH_MID;
  chOut[CH_PAN]     = crsf::fromNorm(out[0]);
  chOut[CH_TILT]    = crsf::fromNorm(out[1]);
  chOut[CH_ZOOM]    = crsf::fromNorm(out[2]);
  chOut[CH_CMD]     = crsf::fromStep(cmdStep(cmdValue, cmdToggle));
  chOut[CH_ARM]     = crsf::CH_MAX;
  chOut[CH_RECALL]  = crsf::fromNorm(recallToNorm(cfg.recallIdx));
}

void crsfService() {
#if ENABLE_CRSF
  // feed telemetry bytes to the parser
  while (CR.available()) crsfParser.push((uint8_t)CR.read());

  static uint32_t lastSend = 0;
  uint32_t now = micros();
  if (now - lastSend < CRSF_PERIOD_US) return;
  lastSend = now;

  buildChannels();
  uint8_t f[26];
  size_t n = crsf::buildChannelsFrame(crsf::ADDR_TX_MODULE, chOut, f);
#if CRSF_TELEMETRY
  lineTx();
  CR.write(f, n);
  CR.flush();
  // our own bytes come back on the shared wire: throw them away
  uint32_t t0 = micros();
  size_t echo = 0;
  while (echo < n && micros() - t0 < 400) {
    if (CR.available()) { CR.read(); echo++; }
  }
  lineRx();
#else
  CR.write(f, n);
#endif
#endif
}

bool linkUp() { return lastLinkMs && millis() - lastLinkMs < LINK_LOST_MS && link.upLq > 0; }

// stored presets as reported by the receiver: "OK 1101" -> mask
uint8_t presetMask() {
  if (!lastStatusMs || millis() - lastStatusMs > 3000) return 0;
  const char *sp = strchr(rxStatus, ' ');
  if (!sp) return 0;
  uint8_t m = 0;
  for (int i = 0; i < 4 && sp[1 + i]; i++) if (sp[1 + i] == '1') m |= 1 << i;
  return m;
}
void rxState(char *dst, size_t len) {
  if (!lastStatusMs || millis() - lastStatusMs > 3000) { strncpy(dst, "--", len); return; }
  size_t k = 0;
  while (rxStatus[k] && rxStatus[k] != ' ' && k < len - 1) { dst[k] = rxStatus[k]; k++; }
  dst[k] = 0;
}

// =====================================================================
//  Settings Wi-Fi + web page
// =====================================================================
WebServer        web(80);
HTTPUpdateServer updater;
bool             wifiOn = false;

String settingsJson() {
  String j = "{";
  j += "\"deadzone\":" + String(cfg.deadzone, 2);
  j += ",\"expo\":" + String(cfg.expo, 2);
  j += ",\"ramp\":" + String(cfg.ramp, 1);
  j += ",\"inv\":[" + String(cfg.inv[0] ? 1 : 0) + "," + String(cfg.inv[1] ? 1 : 0) + "," + String(cfg.inv[2] ? 1 : 0) + "]";
  j += ",\"movePct\":[";
  for (int i = 0; i < 4; i++) { if (i) j += ","; j += String(cfg.movePct[i]); }
  j += "],\"zoomPct\":[";
  for (int i = 0; i < 4; i++) { if (i) j += ","; j += String(cfg.zoomPct[i]); }
  j += "],\"recallIdx\":" + String(cfg.recallIdx);
  j += ",\"recallDps\":[";
  for (int i = 0; i < 16; i++) { if (i) j += ","; j += String(RECALL_DPS[i]); }
  j += "],\"cal\":[";
  for (int i = 0; i < 3; i++) {
    if (i) j += ",";
    j += "[" + String(cfg.cMin[i]) + "," + String(cfg.cMid[i]) + "," + String(cfg.cMax[i]) + "]";
  }
  j += "]}";
  return j;
}

String liveJson() {
  String j = "{\"raw\":[" + String((int)rawF[0]) + "," + String((int)rawF[1]) + "," + String((int)rawF[2]) + "]";
  j += ",\"out\":[" + String(out[0], 2) + "," + String(out[1], 2) + "," + String(out[2], 2) + "]";
  j += ",\"move\":" + String(cfg.moveLevel) + ",\"zoom\":" + String(cfg.zoomLevel);
  j += ",\"calRange\":" + String(calRange ? "true" : "false");
  j += ",\"link\":" + String(linkUp() ? "true" : "false");
  j += ",\"lq\":" + String(link.upLq) + ",\"rssi\":-" + String(link.upRssi1);
  j += ",\"rx\":\"" + String(rxStatus) + "\"}";
  return j;
}

void webSend(const String &s) {
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", s);
}

void hSettings() {
  if (web.method() == HTTP_POST) {
    auto fl = [](const char *k, float &v, float lo, float hi) {
      if (web.hasArg(k)) v = clampf(web.arg(k).toFloat(), lo, hi);
    };
    fl("deadzone", cfg.deadzone, 0, 0.5f);
    fl("expo", cfg.expo, 0, 1);
    fl("ramp", cfg.ramp, 0, 20);
    for (int i = 0; i < 3; i++) {
      char k[6] = {'i', 'n', 'v', char('0' + i), 0};
      if (web.hasArg(k)) cfg.inv[i] = web.arg(k) == "1";
    }
    for (int i = 0; i < 4; i++) {
      char k1[4] = {'m', char('1' + i), 0};
      char k2[4] = {'z', char('1' + i), 0};
      if (web.hasArg(k1)) cfg.movePct[i] = constrain(web.arg(k1).toInt(), 1, 100);
      if (web.hasArg(k2)) cfg.zoomPct[i] = constrain(web.arg(k2).toInt(), 1, 100);
    }
    if (web.hasArg("recall")) cfg.recallIdx = constrain(web.arg("recall").toInt(), 0, 15);
    saveSettings();
  }
  webSend(settingsJson());
}

void hCal() {
  String a = web.arg("a");
  if (a == "centre") {
    for (int i = 0; i < 3; i++) cfg.cMid[i] = (uint16_t)rawF[i];
    saveSettings();
  } else if (a == "range_start") {
    for (int i = 0; i < 3; i++) { cfg.cMin[i] = cfg.cMid[i]; cfg.cMax[i] = cfg.cMid[i]; }
    calRange = true;
  } else if (a == "range_stop") {
    calRange = false;
    for (int i = 0; i < 3; i++) {   // refuse a silly range (axis not moved)
      if (cfg.cMid[i] - cfg.cMin[i] < 200) cfg.cMin[i] = 0;
      if (cfg.cMax[i] - cfg.cMid[i] < 200) cfg.cMax[i] = 4095;
    }
    saveSettings();
  } else if (a == "reset") {
    for (int i = 0; i < 3; i++) { cfg.cMin[i] = 0; cfg.cMid[i] = 2048; cfg.cMax[i] = 4095; }
    calRange = false;
    saveSettings();
  }
  webSend(settingsJson());
}

void wifiStart() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  web.on("/", [] { web.send_P(200, "text/html", INDEX_HTML); });
  web.on("/api/settings", hSettings);
  web.on("/api/live", [] { webSend(liveJson()); });
  web.on("/api/cal", hCal);
  updater.setup(&web, "/update");
  web.begin();
  wifiOn = true;
  showNotice("WIFI ON");
  Serial.printf("[WIFI] \"%s\" -> http://%s  (firmware update: /update)\n", AP_SSID,
                WiFi.softAPIP().toString().c_str());
}

void wifiStop() {
  web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
  calRange = false;
  showNotice("WIFI OFF");
  Serial.println("[WIFI] off");
}

// =====================================================================
//  Screen (240 x 135, landscape)
// =====================================================================
#if ENABLE_TFT
const uint16_t C_BG = ST77XX_BLACK, C_FG = ST77XX_WHITE, C_DIM = 0x7BEF,
               C_OK = 0x07E0, C_WARN = 0xFFE0, C_BAD = 0xF800, C_ACC = 0x05FF;

struct Shown {
  int move = -1, zoom = -1, lq = -1, rssi = -1, mask = -1;
  bool link = false, wifi = false, notice = false, first = true;
  char state[12] = "";
} shown;

void tftText(int x, int y, int size, uint16_t col, const char *s) {
  tft.setTextSize(size);
  tft.setTextColor(col, C_BG);
  tft.setCursor(x, y);
  tft.print(s);
}

void drawScreen() {
  bool nowNotice = notice[0] && millis() - noticeAt < NOTICE_MS;
  bool lk = linkUp();
  char st[12];
  rxState(st, sizeof st);
  int lq = lk ? link.upLq : -1, rssi = lk ? link.upRssi1 : -1;
  int mask = presetMask();

  if (shown.first) {
    tft.fillScreen(C_BG);
    tftText(8, 30, 1, C_DIM, "MOVE");
    tftText(128, 30, 1, C_DIM, "ZOOM");
    tft.drawFastHLine(0, 24, 240, C_DIM);
  }

  // ---- top bar: link + receiver state ----
  if (shown.first || lq != shown.lq || rssi != shown.rssi || lk != shown.link) {
    tft.fillRect(0, 0, 150, 22, C_BG);
    char buf[24];
    if (lk) {
      snprintf(buf, sizeof buf, "LQ%3d -%ddBm", lq, rssi);
      tftText(4, 4, 2, lq >= 70 ? C_OK : (lq >= 40 ? C_WARN : C_BAD), buf);
    } else {
      tftText(4, 4, 2, C_BAD, ENABLE_CRSF ? "NO LINK" : "CRSF OFF");
    }
  }
  if (shown.first || strcmp(st, shown.state) || wifiOn != shown.wifi) {
    tft.fillRect(150, 0, 90, 22, C_BG);
    if (wifiOn) tftText(170, 4, 2, C_ACC, "WIFI");
    else {
      uint16_t c = !strcmp(st, "OK") ? C_OK : (!strcmp(st, "--") ? C_DIM : C_WARN);
      int w = strlen(st) * 12;
      tftText(236 - w, 4, 2, c, st);
    }
  }

  // ---- big speed numbers ----
  if (shown.first || cfg.moveLevel != shown.move) {
    char b[2] = {char('0' + cfg.moveLevel), 0};
    tftText(40, 42, 6, C_FG, b);
  }
  if (shown.first || cfg.zoomLevel != shown.zoom) {
    char b[2] = {char('0' + cfg.zoomLevel), 0};
    tftText(160, 42, 6, C_ACC, b);
  }

  // ---- bottom: presets or notice ----
  if (nowNotice) {
    if (!shown.notice || shown.first) {
      tft.fillRect(0, 98, 240, 37, C_WARN);
      tft.setTextSize(3);
      tft.setTextColor(C_BG, C_WARN);
      int w = strlen(notice) * 18;
      tft.setCursor((240 - w) / 2, 105);
      tft.print(notice);
    }
  } else if (shown.first || shown.notice || mask != shown.mask) {
    tft.fillRect(0, 98, 240, 37, C_BG);
    for (int i = 0; i < 4; i++) {
      int x = 6 + i * 58;
      bool on = mask & (1 << i);
      if (on) tft.fillRoundRect(x, 102, 52, 30, 6, C_OK);
      else    tft.drawRoundRect(x, 102, 52, 30, 6, C_DIM);
      tft.setTextSize(2);
      tft.setTextColor(on ? C_BG : C_DIM, on ? C_OK : C_BG);
      tft.setCursor(x + 14, 110);
      tft.printf("P%d", i + 1);
    }
  }

  shown.move = cfg.moveLevel; shown.zoom = cfg.zoomLevel;
  shown.lq = lq; shown.rssi = rssi; shown.link = lk; shown.mask = mask;
  shown.wifi = wifiOn; shown.notice = nowNotice; shown.first = false;
  strncpy(shown.state, st, sizeof shown.state);
}
#endif

// =====================================================================
//  Button logic
// =====================================================================
void handleButtons(uint32_t now) {
  for (int i = 0; i < B_COUNT; i++) {
    btn[i].update(now);
#if DEBUG_SERIAL
    if (btn[i].pressEdge) Serial.printf("[BTN] %s down\n", BTN_NAME[i]);
    if (btn[i].releaseEdge) Serial.printf("[BTN] %s up\n", BTN_NAME[i]);
#endif
  }

  // presets: tap = recall, hold 2 s = save
  for (int i = 0; i < 4; i++) {
    Button &b = btn[B_P1 + i];
    char label[12];
    if (b.down && !b.holdFired && b.heldMs >= HOLD_SAVE_MS) {
      b.holdFired = true;
      snprintf(label, sizeof label, "SAVE P%d", i + 1);
      sendCommand(CMD_SAVE_1 + i, label);
    }
    if (b.releaseEdge && !b.holdFired) {
      snprintf(label, sizeof label, "RECALL P%d", i + 1);
      sendCommand(CMD_RECALL_1 + i, label);
    }
  }

  // movement and zoom speed buttons
  for (int i = 0; i < 4; i++) {
    if (btn[B_M1 + i].pressEdge) { cfg.moveLevel = i + 1; markDirty(); }
    if (btn[B_Z1 + i].pressEdge) { cfg.zoomLevel = i + 1; markDirty(); }
  }

  // joystick button: tap = next movement speed, hold 1 s = recall P1
  Button &j = btn[B_JOY];
  if (j.down && !j.holdFired && j.heldMs >= JOY_LONG_MS) {
    j.holdFired = true;
    sendCommand(CMD_RECALL_1, "RECALL P1");
  }
  if (j.releaseEdge && !j.holdFired) {
    cfg.moveLevel = cfg.moveLevel % 4 + 1;
    markDirty();
  }

  // Wi-Fi settings page
  if (btn[B_WIFI].pressEdge) { if (wifiOn) wifiStop(); else wifiStart(); }
}

// =====================================================================
//  Setup / loop
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nrs4-elrs REMOTE starting");

  loadSettings();
  analogReadResolution(12);
  for (int i = 0; i < 3; i++) { pinMode(AXIS_PIN[i], INPUT); rawF[i] = analogRead(AXIS_PIN[i]); }
  for (int i = 0; i < B_COUNT; i++) btn[i].begin();

#if ENABLE_TFT
  SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
#endif

  crsfBegin();
  Serial.printf("CRSF %s, telemetry %s, screen %s\n", ENABLE_CRSF ? "ON" : "OFF",
                (ENABLE_CRSF && CRSF_TELEMETRY) ? "ON" : "OFF", ENABLE_TFT ? "ON" : "OFF");
}

void loop() {
  static uint32_t lastJoy = 0, lastDraw = 0, lastDbg = 0;
  uint32_t now = millis();

  if (now - lastJoy >= 5) {
    float dt = (now - lastJoy) / 1000.f;
    if (dt > 0.1f) dt = 0.1f;
    lastJoy = now;
    readJoystick(dt);
    handleButtons(now);
  }

  crsfService();
  if (wifiOn) web.handleClient();
  if (cfgDirty && now - cfgDirtyAt > 3000) saveSettings();

#if ENABLE_TFT
  if (now - lastDraw >= 100) { lastDraw = now; drawScreen(); }
#endif

#if DEBUG_SERIAL
  if (now - lastDbg >= 250) {
    lastDbg = now;
    Serial.printf("raw X%4d Y%4d Z%4d | out pan%+.2f tilt%+.2f zoom%+.2f | MOVE%u ZOOM%u | ",
                  (int)rawF[0], (int)rawF[1], (int)rawF[2], out[0], out[1], out[2],
                  cfg.moveLevel, cfg.zoomLevel);
    if (linkUp()) Serial.printf("LQ%u -%udBm", link.upLq, link.upRssi1);
    else Serial.print("no link");
    Serial.printf(" | rx \"%s\" | crsf ok%lu bad%lu\n", rxStatus,
                  (unsigned long)crsfParser.good, (unsigned long)crsfParser.bad);
  }
#endif
  delay(1);
}
