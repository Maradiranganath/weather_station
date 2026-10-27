/*
  ROBOT FACE WEATHER STATION  -  VERSION 2
  Board  : ESP32-C3
  Display: 1.8" ST7735 TFT 128x160 (SPI)
  Sensor : DHT11 / DHT22 + 1 touch sensor (TTP223 style, digital output)

  Libraries (Arduino Library Manager):
    - Adafruit GFX Library (latest)
    - Adafruit ST7735 and ST7789 Library
    - DHT sensor library (Adafruit) + Adafruit Unified Sensor
    - ArduinoJson (version 7.x)

  TOUCH CONTROLS
    1 tap  -> HAPPY        2 taps -> LOVE        3 taps -> SURPRISED
    4 taps -> ANGRY        5+     -> DIZZY
    3 seconds idle -> weather screen (time, indoor DHT, outdoor + rain chance)
    HOLD touch for 1.5 s -> DEMO MODE on/off
        demo = all 5 faces, then 10 weather scenes (sunrise, sunny, partly cloudy,
        sunset, night, cloudy, rain, thunderstorm, snow, fog)
        tap during demo = jump to next demo step

  NEW IN V2
    - Sky colour follows the real time of day (sunrise, day, sunset, night)
    - Sun / moon travel across the sky, glow, hills, trees, stars
    - Lower SPI speed + lower WiFi power for a stable display
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <DHT.h>
#include <time.h>

// ================= USER SETTINGS =================
const char* WIFI_SSID = "OnePlus Nord CE5 F0CA";
const char* WIFI_PASS = "karadi@143";

const float LAT = 12.2958;       // Mysuru
const float LON = 76.6394;
const long  GMT_OFFSET_SEC = 19800;  // India +5:30

// ---- Pins (change if your board is wired differently) ----
#define TFT_SCK   4
#define TFT_MOSI  6    // display pin labelled SDA
#define TFT_CS    7
#define TFT_DC    0    // display pin labelled A0
#define TFT_RST   1
// display LED pin -> 3.3V, VCC -> 3.3V, GND -> GND
#define DHT_PIN   3
#define DHT_TYPE  DHT11     // change to DHT22 if you have that
#define TOUCH_PIN 5
#define TOUCH_ACTIVE HIGH

#define TFT_SPI_HZ  16000000   // if the screen still glitches, try 8000000
#define IDLE_MS     3000       // idle time before weather screen
#define TAP_WINDOW  450        // time to wait for more taps
#define LONG_PRESS  1500       // hold time for demo mode
#define FRAME_MS    40
// =================================================

#define SCR_W 128
#define SCR_H 160

enum Emotion { E_NEUTRAL, E_HAPPY, E_LOVE, E_SURPRISED, E_ANGRY, E_DIZZY };
enum Scene   { SC_CLEAR, SC_PARTLY, SC_CLOUDY, SC_FOG, SC_RAIN, SC_STORM, SC_SNOW };
enum State   { ST_FACE, ST_INFO };

// what the info screen is showing right now (live data or demo data)
// NOTE: must stay up here, before any function, or Arduino auto-prototypes fail
struct View {
  Scene sc; float hour; int code; float outT; int rain;
  bool valid, timeOk; int hh, mm; char date[20];
};

Adafruit_ST7735 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(SCR_W, SCR_H);
DHT dht(DHT_PIN, DHT_TYPE);

// ---------- shared data ----------
float inTemp = NAN, inHum = NAN;
volatile bool  wxValid = false;
volatile float wxTemp = 0;
volatile int   wxHum = 0, wxCode = 0, wxRain = 0;
volatile bool  wxDay = true;

// ---------- app state ----------
State   state = ST_FACE;
Emotion emo = E_NEUTRAL;
uint32_t lastActivity = 0, lastFrame = 0, lastTapMs = 0;
int  tapCount = 0;
bool touching = false;

// ---------- demo ----------
struct DemoItem { Scene sc; float hour; int code; float t; int rain; };
const DemoItem demoList[10] = {
  { SC_CLEAR,  6.5f,  0, 22,  5 },   // sunrise
  { SC_CLEAR,  12.0f, 0, 31,  0 },   // sunny
  { SC_PARTLY, 15.0f, 2, 29, 20 },   // partly cloudy
  { SC_CLEAR,  18.1f, 0, 27,  5 },   // sunset
  { SC_CLEAR,  23.0f, 0, 21,  0 },   // clear night
  { SC_CLOUDY, 11.0f, 3, 25, 30 },   // cloudy
  { SC_RAIN,   15.0f, 63, 22, 85 },  // rain
  { SC_STORM,  21.0f, 95, 21, 95 },  // thunderstorm
  { SC_SNOW,   10.0f, 73, -2, 70 },  // snow
  { SC_FOG,    7.5f,  45, 12, 10 }   // fog
};
const Emotion demoEmo[5] = { E_HAPPY, E_LOVE, E_SURPRISED, E_ANGRY, E_DIZZY };
#define DEMO_FACE_STEPS 5
#define DEMO_STEPS (DEMO_FACE_STEPS + 10)
bool demoMode = false;
int  demoStep = 0;
uint32_t demoNext = 0;

// ---------- helpers ----------
static inline uint16_t C565(int r, int g, int b) {
  if (r < 0) r = 0; if (r > 255) r = 255;
  if (g < 0) g = 0; if (g > 255) g = 255;
  if (b < 0) b = 0; if (b > 255) b = 255;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t WHITE = 0xFFFF;

static inline int lerpi(int a, int b, float f) { return a + (int)((b - a) * f); }

bool inRound(int px, int py, int x, int y, int w, int h, int r) {
  int cx = px < x + r ? x + r : (px > x + w - 1 - r ? x + w - 1 - r : px);
  int cy = py < y + r ? y + r : (py > y + h - 1 - r ? y + h - 1 - r : py);
  int dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= r * r;
}

enum { OP_DARK, OP_SOFT };
// pixel effects on the canvas (glass panels, fog)
void fx(int x, int y, int w, int h, int r, int op) {
  uint16_t* b = canvas.getBuffer();
  int y0 = max(0, y), y1 = min(SCR_H, y + h);
  int x0 = max(0, x), x1 = min(SCR_W, x + w);
  for (int yy = y0; yy < y1; yy++) {
    for (int xx = x0; xx < x1; xx++) {
      if (r && !inRound(xx, yy, x, y, w, h, r)) continue;
      uint16_t p = b[yy * SCR_W + xx];
      if (op == OP_DARK) p = (p >> 1) & 0x7BEF;
      else               p = ((p >> 1) & 0x7BEF) + ((p >> 2) & 0x39E7) + 0x39E7;
      b[yy * SCR_W + xx] = p;
    }
  }
}

// additive soft glow (integer maths only, the C3 has no FPU)
void glowAdd(int cx, int cy, int R, int gr, int gg, int gb, int strength /*0..255*/) {
  uint16_t* buf = canvas.getBuffer();
  int R2 = R * R;
  for (int y = max(0, cy - R); y < min(SCR_H, cy + R + 1); y++) {
    for (int x = max(0, cx - R); x < min(SCR_W, cx + R + 1); x++) {
      int dx = x - cx, dy = y - cy;
      int d2 = dx * dx + dy * dy;
      if (d2 >= R2) continue;
      int t = ((R2 - d2) * 256) / R2;        // 0..256
      int k = (t * t) >> 8;                  // 0..256 (quadratic falloff)
      k = (k * strength) >> 8;               // 0..255
      uint16_t p = buf[y * SCR_W + x];
      int r = ((p >> 11) & 0x1F) << 3;
      int g = ((p >> 5) & 0x3F) << 2;
      int b = (p & 0x1F) << 3;
      r += (gr * k) >> 8; g += (gg * k) >> 8; b += (gb * k) >> 8;
      buf[y * SCR_W + x] = C565(r, g, b);
    }
  }
}

void textAt(const char* s, int x, int y, int size, uint16_t col) {
  canvas.setTextSize(size);
  canvas.setTextColor(0x0000);
  canvas.setCursor(x + 1, y + 1);
  canvas.print(s);
  canvas.setTextColor(col);
  canvas.setCursor(x, y);
  canvas.print(s);
}

void textC(const char* s, int cx, int y, int size, uint16_t col) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setTextSize(size);
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  textAt(s, cx - w / 2, y, size, col);
}

// =====================================================
//                    WEATHER (WiFi)
// =====================================================
bool fetchWeather() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(LAT, 4) +
               "&longitude=" + String(LON, 4) +
               "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
               "&hourly=precipitation_probability&forecast_days=1&timezone=auto";
  if (!http.begin(client, url)) return false;
  http.setTimeout(10000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  wxTemp = doc["current"]["temperature_2m"] | 0.0f;
  wxHum  = doc["current"]["relative_humidity_2m"] | 0;
  wxCode = doc["current"]["weather_code"] | 0;
  wxDay  = (doc["current"]["is_day"] | 1) == 1;

  // rain chance = highest probability in the current + next 2 hours
  const char* t = doc["current"]["time"];
  int hr = t ? atoi(t + 11) : 0;
  int mx = 0;
  for (int i = hr; i < hr + 3 && i < 24; i++) {
    int v = doc["hourly"]["precipitation_probability"][i] | 0;
    if (v > mx) mx = v;
  }
  wxRain = mx;
  wxValid = true;
  return true;
}

void weatherTask(void*) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (fetchWeather()) vTaskDelay(pdMS_TO_TICKS(600000));   // refresh every 10 min
      else                vTaskDelay(pdMS_TO_TICKS(30000));    // retry in 30 s
    } else {
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

const char* condText(int c, bool day) {
  switch (c) {
    case 0: return day ? "Clear Sky" : "Clear Night";
    case 1: return day ? "Mostly Sunny" : "Mostly Clear";
    case 2: return "Partly Cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Foggy";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: case 66: case 67: return "Freezing Rain";
    case 61: return "Light Rain";
    case 63: return "Rain";
    case 65: return "Heavy Rain";
    case 71: case 73: case 75: case 77: case 85: case 86: return "Snow";
    case 80: case 81: return "Rain Showers";
    case 82: return "Heavy Showers";
    case 95: case 96: case 99: return "Thunderstorm";
  }
  return "Weather";
}

Scene getScene() {
  if (!wxValid) return SC_CLOUDY;
  int c = wxCode;
  if (c <= 1) return SC_CLEAR;
  if (c == 2) return SC_PARTLY;
  if (c == 3) return SC_CLOUDY;
  if (c == 45 || c == 48) return SC_FOG;
  if (c >= 95) return SC_STORM;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return SC_SNOW;
  return SC_RAIN;
}

// builds what the info screen shows right now (live data or demo data)
void buildView(View& v) {
  v.date[0] = 0;
  if (demoMode) {
    const DemoItem& d = demoList[demoStep - DEMO_FACE_STEPS];
    v.sc = d.sc; v.hour = d.hour; v.code = d.code; v.outT = d.t; v.rain = d.rain;
    v.valid = true; v.timeOk = true;
    v.hh = (int)d.hour; v.mm = (int)((d.hour - v.hh) * 60);
    strcpy(v.date, "DEMO MODE");
    return;
  }
  v.valid = wxValid; v.sc = getScene(); v.code = wxCode; v.outT = wxTemp; v.rain = wxRain;
  struct tm ti;
  v.timeOk = getLocalTime(&ti, 0);
  if (v.timeOk) {
    v.hh = ti.tm_hour; v.mm = ti.tm_min;
    v.hour = ti.tm_hour + ti.tm_min / 60.0f;
    strftime(v.date, sizeof v.date, "%a %d %b", &ti);
  } else {
    v.hh = 0; v.mm = 0;
    v.hour = wxDay ? 12.0f : 23.0f;
    strcpy(v.date, "Syncing time...");
  }
}

// =====================================================
//                    PARTICLES
// =====================================================
struct Drop   { float x, y, v; int len; };
struct Ripple { int x, y, age; };
struct Flake  { float x, y, v, ph; };
struct Star   { uint8_t x, y; };
struct Cloud  { float x; int y; int r; float v; };
struct Slide  { float x, y, v; };

Drop   drops[40];
Ripple rip[6];
Flake  flakes[36];
Star   stars[28];
Cloud  clouds[4];
Slide  slides[5];
int8_t hillFar[SCR_W], hillNear[SCR_W];   // hill heights (precomputed)

void initParticles() {
  for (auto& d : drops) { d.x = random(0, 140); d.y = random(-20, 160); d.v = random(40, 80) / 10.0f; d.len = random(6, 12); }
  for (auto& r : rip) { r.age = 99; r.x = 0; r.y = 0; }
  for (auto& f : flakes) { f.x = random(0, 128); f.y = random(0, 160); f.v = random(5, 15) / 10.0f; f.ph = random(0, 628) / 100.0f; }
  for (auto& s : stars) { s.x = random(0, 128); s.y = random(4, 95); }
  clouds[0] = { 20, 28, 14, 0.18f };
  clouds[1] = { 90, 62, 11, 0.28f };
  clouds[2] = { 50, 82, 12, 0.22f };
  clouds[3] = { 110, 44, 16, 0.12f };
  for (auto& s : slides) { s.x = random(8, 120); s.y = random(0, 100); s.v = random(3, 9) / 10.0f; }
  for (int x = 0; x < SCR_W; x++) {
    hillFar[x]  = 98  + (int)(5 * sinf(x * 0.05f + 1.0f) + 3 * sinf(x * 0.17f));
    hillNear[x] = 107 + (int)(4 * sinf(x * 0.08f + 3.0f) + 2 * sinf(x * 0.23f));
  }
}

// =====================================================
//                    SKY (time of day)
// =====================================================
int tr, tg, tb, br_, bg_, bb_;   // current sky gradient (top rgb, bottom rgb)
float dl = 1.0f;                 // daylight 0 (night) .. 1 (day)

struct KF { float h; int t[3]; int b[3]; float dl; };
const KF kf[10] = {
  { 0.0f,  {5, 8, 30},     {20, 30, 70},    0.0f },
  { 5.0f,  {15, 20, 60},   {90, 70, 110},   0.1f },
  { 6.3f,  {70, 110, 190}, {255, 160, 90},  0.6f },   // sunrise
  { 8.5f,  {30, 120, 225}, {150, 210, 250}, 1.0f },
  { 12.0f, {15, 105, 230}, {135, 205, 250}, 1.0f },   // noon
  { 16.5f, {35, 115, 215}, {170, 210, 240}, 1.0f },
  { 18.0f, {80, 75, 160},  {255, 135, 60},  0.6f },   // sunset
  { 19.2f, {25, 25, 75},   {120, 60, 90},   0.2f },   // dusk
  { 20.6f, {5, 8, 30},     {20, 30, 75},    0.0f },
  { 24.0f, {5, 8, 30},     {20, 30, 70},    0.0f }
};

void computeSky(float hour, Scene sc) {
  if (hour < 0) hour = 0;
  if (hour > 23.99f) hour = 23.99f;
  int i = 0;
  while (i < 8 && hour >= kf[i + 1].h) i++;
  float f = (hour - kf[i].h) / (kf[i + 1].h - kf[i].h);
  tr = lerpi(kf[i].t[0], kf[i + 1].t[0], f);
  tg = lerpi(kf[i].t[1], kf[i + 1].t[1], f);
  tb = lerpi(kf[i].t[2], kf[i + 1].t[2], f);
  br_ = lerpi(kf[i].b[0], kf[i + 1].b[0], f);
  bg_ = lerpi(kf[i].b[1], kf[i + 1].b[1], f);
  bb_ = lerpi(kf[i].b[2], kf[i + 1].b[2], f);
  dl = kf[i].dl + (kf[i + 1].dl - kf[i].dl) * f;

  // weather tint (grey sky for clouds / rain / snow ...)
  float k = 0; int t0 = 0, t1 = 0, t2 = 0, b0 = 0, b1 = 0, b2 = 0;
  switch (sc) {
    case SC_PARTLY: k = 0.20f; t0 = 120; t1 = 150; t2 = 185; b0 = 190; b1 = 205; b2 = 220; break;
    case SC_CLOUDY: k = 0.70f; t0 = 80;  t1 = 90;  t2 = 105; b0 = 150; b1 = 160; b2 = 175; break;
    case SC_FOG:    k = 0.80f; t0 = 140; t1 = 150; t2 = 160; b0 = 190; b1 = 195; b2 = 200; break;
    case SC_RAIN:   k = 0.80f; t0 = 35;  t1 = 45;  t2 = 65;  b0 = 80;  b1 = 95;  b2 = 120; break;
    case SC_STORM:  k = 0.92f; t0 = 20;  t1 = 20;  t2 = 35;  b0 = 60;  b1 = 60;  b2 = 85;  break;
    case SC_SNOW:   k = 0.75f; t0 = 100; t1 = 115; t2 = 140; b0 = 190; b1 = 200; b2 = 220; break;
    default: break;
  }
  if (k > 0) {
    float s = 0.3f + 0.7f * dl;
    tr = lerpi(tr, (int)(t0 * s), k); tg = lerpi(tg, (int)(t1 * s), k); tb = lerpi(tb, (int)(t2 * s), k);
    br_ = lerpi(br_, (int)(b0 * s), k); bg_ = lerpi(bg_, (int)(b1 * s), k); bb_ = lerpi(bb_, (int)(b2 * s), k);
  }
}

uint16_t skyAt(int y) {
  int r = tr + (br_ - tr) * y / 110;   // gradient finishes at the horizon
  int g = tg + (bg_ - tg) * y / 110;
  int b = tb + (bb_ - tb) * y / 110;
  if (y > 110) { r = br_; g = bg_; b = bb_; }
  return C565(r, g, b);
}

void drawSky() {
  for (int y = 0; y < SCR_H; y++) canvas.drawFastHLine(0, y, SCR_W, skyAt(y));
}

void cloudColors(Scene sc, uint16_t& m, uint16_t& s) {
  int mr, mg, mb, sr, sg, sb;
  switch (sc) {
    case SC_CLEAR: case SC_PARTLY: mr = 250; mg = 250; mb = 255; sr = 200; sg = 215; sb = 235; break;
    case SC_CLOUDY: case SC_FOG:   mr = 205; mg = 210; mb = 220; sr = 150; sg = 158; sb = 172; break;
    case SC_SNOW:                  mr = 225; mg = 230; mb = 238; sr = 170; sg = 178; sb = 190; break;
    default:                       mr = 105; mg = 115; mb = 135; sr = 70;  sg = 80;  sb = 100; break;
  }
  float sc_ = 0.3f + 0.7f * dl;      // darker at night
  float wm = (1.0f - dl) * 0.45f;    // sunset / night colour spills into clouds
  m = C565(lerpi((int)(mr * sc_), br_, wm), lerpi((int)(mg * sc_), bg_, wm), lerpi((int)(mb * sc_), bb_, wm));
  s = C565(lerpi((int)(sr * sc_), br_, wm), lerpi((int)(sg * sc_), bg_, wm), lerpi((int)(sb * sc_), bb_, wm));
}

void drawCloudShape(int x, int y, int r, uint16_t c) {
  canvas.fillCircle(x - r, y + r / 4, r * 6 / 10, c);
  canvas.fillCircle(x + r, y + r / 4, r * 7 / 10, c);
  canvas.fillCircle(x, y - r / 4, r, c);
  canvas.fillRect(x - r, y + r / 4, 2 * r, r * 7 / 10, c);
}

void drawClouds(uint8_t mask, uint16_t cm, uint16_t cs) {
  for (int i = 0; i < 4; i++) {
    if (!(mask & (1 << i))) continue;
    Cloud& c = clouds[i];
    c.x += c.v;
    if (c.x > SCR_W + c.r * 2) c.x = -c.r * 2;
    drawCloudShape((int)c.x, c.y + 3, c.r, cs);
    drawCloudShape((int)c.x, c.y, c.r, cm);
  }
}

void drawSun(int cx, int cy, uint32_t now) {
  glowAdd(cx, cy, 46, 255, 200, 110, 190);          // warm glow
  float rot = now / 2500.0f;
  uint16_t ray = C565(255, 200 + (int)(35 * dl), 70);
  for (int i = 0; i < 12; i++) {
    float a = rot + i * PI / 6.0f;
    float r2 = 25 + 3 * sinf(now / 250.0f + i);
    int x1 = cx + 19 * cosf(a), y1 = cy + 19 * sinf(a);
    int x2 = cx + r2 * cosf(a), y2 = cy + r2 * sinf(a);
    canvas.drawLine(x1, y1, x2, y2, ray);
    canvas.drawLine(x1 + 1, y1, x2 + 1, y2, ray);
  }
  canvas.fillCircle(cx, cy, 15, C565(255, 140 + (int)(60 * dl), 0));
  canvas.fillCircle(cx, cy, 11, C565(255, 190 + (int)(45 * dl), 60 + (int)(30 * dl)));
}

void drawMoon(int cx, int cy) {
  glowAdd(cx, cy, 34, 120, 140, 200, 150);
  canvas.fillCircle(cx, cy, 13, C565(250, 245, 200));
  canvas.fillCircle(cx + 6, cy - 4, 11, skyAt(cy - 4));
}

void drawStars(uint32_t now, float inten) {
  for (int i = 0; i < 28; i++) {
    int b = (int)((110 + 145 * (0.5f + 0.5f * sinf(now / 500.0f + i))) * inten);
    if (b < 30) continue;
    uint16_t c = C565(b, b, b);
    canvas.drawPixel(stars[i].x, stars[i].y, c);
    if (b > 190) {
      canvas.drawPixel(stars[i].x + 1, stars[i].y, c);
      canvas.drawPixel(stars[i].x - 1, stars[i].y, c);
      canvas.drawPixel(stars[i].x, stars[i].y + 1, c);
      canvas.drawPixel(stars[i].x, stars[i].y - 1, c);
    }
  }
}

// hills + trees silhouette at the horizon
void drawLand(Scene sc) {
  int fr, fg, fb, nr, ng, nb;
  switch (sc) {
    case SC_SNOW:  fr = 215; fg = 225; fb = 238; nr = 240; ng = 245; nb = 252; break;
    case SC_RAIN: case SC_STORM: fr = 55; fg = 85; fb = 85; nr = 35; ng = 70; nb = 50; break;
    case SC_FOG:   fr = 150; fg = 165; fb = 165; nr = 110; ng = 135; nb = 120; break;
    default:       fr = 85; fg = 150; fb = 125; nr = 45; ng = 120; nb = 70; break;
  }
  float s = 0.15f + 0.85f * dl * dl;
  uint16_t farC  = C565((int)(fr * s), (int)(fg * s), (int)(fb * s));
  uint16_t nearC = C565((int)(nr * s), (int)(ng * s), (int)(nb * s));
  uint16_t treeC = C565((int)(nr * s * 0.5f), (int)(ng * s * 0.55f), (int)(nb * s * 0.5f));
  for (int x = 0; x < SCR_W; x++) canvas.drawFastVLine(x, hillFar[x], SCR_H - hillFar[x], farC);
  for (int x = 0; x < SCR_W; x++) canvas.drawFastVLine(x, hillNear[x], SCR_H - hillNear[x], nearC);
  const int tx[6] = { 9, 20, 31, 98, 109, 119 };
  for (int i = 0; i < 6; i++) {
    int x = tx[i], base = hillNear[x] + 2, h = 12 + (i % 3) * 2;
    canvas.fillTriangle(x - 4, base, x + 4, base, x, base - h, treeC);
    canvas.fillTriangle(x - 3, base - h / 2, x + 3, base - h / 2, x, base - h - 3, treeC);
  }
}

void drawRain(uint32_t now) {
  int sc = 90 + (int)(165 * dl);
  uint16_t streak = C565(sc * 170 / 255, sc * 200 / 255, sc);
  for (auto& d : drops) {
    d.y += d.v;
    d.x -= d.v * 0.15f;
    if (d.y > 112) {
      for (auto& r : rip) if (r.age >= 12) { r.x = (int)d.x; r.y = 108 + random(0, 5); r.age = 0; break; }
      d.y = -d.len;
      d.x = random(0, 150);
    }
    canvas.drawLine((int)(d.x + d.len * 0.15f), (int)(d.y - d.len), (int)d.x, (int)d.y, streak);
  }
  for (auto& r : rip) {
    if (r.age >= 12) continue;
    int rx = r.age / 2 + 1, ry = r.age / 6 + 1;
    for (float a = 0; a < 6.28f; a += 0.5f)
      canvas.drawPixel(r.x + rx * cosf(a), r.y + ry * sinf(a), C565(sc * 200 / 255, sc * 220 / 255, sc));
    r.age++;
  }
  // big drops sliding down the "window glass"
  uint16_t gd = C565(sc * 205 / 255, sc * 228 / 255, sc);
  for (int i = 0; i < 5; i++) {
    Slide& s = slides[i];
    s.y += s.v * (1.0f + 0.8f * sinf(now / 400.0f + i * 2));
    if (s.y > 110) { s.y = 0; s.x = random(8, 120); s.v = random(3, 9) / 10.0f; }
    canvas.drawLine((int)s.x, (int)s.y - 9, (int)s.x, (int)s.y - 3, C565(sc * 120 / 255, sc * 150 / 255, sc * 190 / 255));
    canvas.fillCircle((int)s.x, (int)s.y, 2, gd);
  }
}

void drawSnow(uint32_t now) {
  int w = 140 + (int)(115 * dl);
  uint16_t c = C565(w, w, w);
  for (auto& f : flakes) {
    f.y += f.v;
    f.x += sinf(now / 600.0f + f.ph) * 0.4f;
    if (f.y > SCR_H) { f.y = -2; f.x = random(0, 128); }
    if (f.v > 1.0f) canvas.fillCircle((int)f.x, (int)f.y, 2, c);
    else canvas.drawPixel((int)f.x, (int)f.y, c);
  }
}

void drawFog(uint32_t now) {
  const int ys[5] = { 20, 48, 78, 98, 126 };
  for (int i = 0; i < 5; i++) {
    int x = (int)((now / 60 + i * 41) % 190) - 60;
    fx(x, ys[i], 90, 10, 4, OP_SOFT);
  }
}

void drawBolt(uint32_t now) {
  static uint32_t nextBolt = 3000, boltEnd = 0;
  if (now > nextBolt) { boltEnd = now + 140; nextBolt = now + random(3500, 8000); }
  if (now < boltEnd) {
    glowAdd(66, 60, 50, 150, 150, 220, 160);      // soft flash, no white-out
    uint16_t c = C565(255, 250, 170);
    int p[4][2] = { {74, 24}, {64, 52}, {72, 52}, {60, 92} };
    for (int o = 0; o < 2; o++) {
      canvas.drawLine(p[0][0] + o, p[0][1], p[1][0] + o, p[1][1], c);
      canvas.drawLine(p[1][0] + o, p[1][1], p[2][0] + o, p[2][1], c);
      canvas.drawLine(p[2][0] + o, p[2][1], p[3][0] + o, p[3][1], c);
    }
  }
}

// =====================================================
//                    INFO SCREEN
// =====================================================
void glass(int x, int y, int w, int h, int r) {
  fx(x, y, w, h, r, OP_DARK);
  canvas.drawRoundRect(x, y, w, h, r, C565(110, 130, 160));
}

void drawTemp(int x, int y, float v) {
  char b[8];
  if (isnan(v)) strcpy(b, "--"); else snprintf(b, sizeof b, "%d", (int)roundf(v));
  textAt(b, x, y, 2, WHITE);
  if (!isnan(v)) {
    int cx = x + strlen(b) * 12;
    canvas.drawCircle(cx + 3, y + 2, 2, WHITE);
    textAt("C", cx + 8, y, 2, WHITE);
  }
}

void wifiIcon(int x, int y) {
  bool ok = WiFi.status() == WL_CONNECTED;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2;
    if (ok) canvas.fillRect(x + i * 4, y + 9 - h, 3, h, WHITE);
    else    canvas.drawRect(x + i * 4, y + 9 - h, 3, h, C565(150, 150, 150));
  }
}

void drawInfo(uint32_t now) {
  View v;
  buildView(v);
  computeSky(v.hour, v.sc);
  drawSky();

  bool clearish = (v.sc == SC_CLEAR || v.sc == SC_PARTLY);
  bool sunUp = (v.hour >= 6.0f && v.hour < 18.6f);

  // stars, sun / moon travelling across the sky
  if (clearish && dl < 0.55f) drawStars(now, 1.0f - dl * 1.6f);
  if (clearish) {
    if (sunUp) {
      float p = (v.hour - 6.0f) / 12.6f;
      int sx = 12 + (int)(p * 104);
      int sy = 99 - (int)(sinf(p * PI) * 32);
      drawSun(sx, sy, now);
    } else {
      float hn = v.hour >= 18.6f ? v.hour - 18.6f : v.hour + 5.4f;
      float p = hn / 11.4f;
      int mx = 12 + (int)(p * 104);
      int my = 99 - (int)(sinf(p * PI) * 32);
      drawMoon(mx, my);
    }
  }

  // clouds
  uint16_t cm, cs;
  cloudColors(v.sc, cm, cs);
  switch (v.sc) {
    case SC_CLEAR:  drawClouds(0b0010, cm, cs); break;
    case SC_PARTLY: drawClouds(0b0110, cm, cs); break;
    case SC_CLOUDY: drawClouds(0b1111, cm, cs); break;
    case SC_FOG:    drawClouds(0b0001, cm, cs); break;
    case SC_RAIN:   drawClouds(0b1011, cm, cs); break;
    case SC_STORM:  drawClouds(0b1111, cm, cs); break;
    case SC_SNOW:   drawClouds(0b1011, cm, cs); break;
  }

  // land in front of the sky
  drawLand(v.sc);

  // precipitation / fog / lightning
  switch (v.sc) {
    case SC_RAIN:  drawRain(now); break;
    case SC_STORM: drawRain(now); drawBolt(now); break;
    case SC_SNOW:  drawSnow(now); break;
    case SC_FOG:   drawFog(now); break;
    default: break;
  }

  // ---------- top panel: time + date ----------
  glass(4, 3, 120, 44, 8);
  char tb[12], ap[3] = "";
  if (v.timeOk) {
    int h12 = v.hh % 12; if (h12 == 0) h12 = 12;
    bool colon = demoMode ? true : ((now / 500) % 2 == 0);
    snprintf(tb, sizeof tb, "%d%c%02d", h12, colon ? ':' : ' ', v.mm);
    strcpy(ap, v.hh >= 12 ? "PM" : "AM");
  } else {
    strcpy(tb, "--:--");
  }
  int tw = strlen(tb) * 18;
  int x0 = (SCR_W - (tw + (v.timeOk ? 14 : 0))) / 2;
  textAt(tb, x0, 7, 3, WHITE);
  if (v.timeOk) textAt(ap, x0 + tw + 2, 23, 1, C565(200, 215, 235));
  textC(v.date, 64, 34, 1, demoMode ? C565(255, 220, 0) : C565(200, 215, 235));
  wifiIcon(111, 5);

  // ---------- condition pill ----------
  const char* ct;
  if (v.valid) ct = condText(v.code, sunUp);
  else ct = (WiFi.status() == WL_CONNECTED) ? "Fetching..." : "No WiFi";
  int16_t bx, by; uint16_t bw, bh;
  canvas.setTextSize(1);
  canvas.getTextBounds(ct, 0, 0, &bx, &by, &bw, &bh);
  int pw = bw + 16;
  glass((SCR_W - pw) / 2, 50, pw, 13, 6);
  textC(ct, 64, 53, 1, WHITE);

  // ---------- bottom panel: indoor / outdoor ----------
  glass(4, 114, 120, 43, 8);
  canvas.drawFastVLine(64, 119, 30, C565(110, 130, 160));
  uint16_t lab = C565(170, 190, 215);
  textAt("INDOOR", 10, 117, 1, lab);
  textAt("OUTDOOR", 72, 117, 1, lab);
  drawTemp(10, 127, inTemp);
  drawTemp(72, 127, v.valid ? v.outT : NAN);

  char hb[14], rb[14];
  if (isnan(inHum)) strcpy(hb, "Hum --"); else snprintf(hb, sizeof hb, "Hum %d%%", (int)roundf(inHum));
  if (v.valid) snprintf(rb, sizeof rb, "Rain %d%%", v.rain); else strcpy(rb, "Rain --");
  textAt(hb, 10, 145, 1, WHITE);
  bool rainy = v.valid && v.rain >= 60;
  textAt(rb, 72, 145, 1, rainy ? C565(120, 190, 255) : WHITE);

  // rain chance bar
  canvas.fillRect(10, 154, 108, 2, C565(50, 60, 80));
  if (v.valid) canvas.fillRect(10, 154, (108 * v.rain) / 100, 2, rainy ? C565(80, 170, 255) : C565(120, 200, 120));
}

// =====================================================
//                    ROBOT FACE
// =====================================================
void smile(int cx, int cy, int hw, int depth, uint16_t col) {
  for (int x = -hw; x <= hw; x++) {
    float k = (float)x / hw;
    int y = cy + (int)(depth * (1 - k * k));
    canvas.fillCircle(cx + x, y, 1, col);
  }
}

void heart(int cx, int cy, int s, uint16_t col) {
  int r = s / 2;
  canvas.fillCircle(cx - r, cy - r, r, col);
  canvas.fillCircle(cx + r, cy - r, r, col);
  canvas.fillTriangle(cx - s + 1, cy - r + 2, cx + s - 1, cy - r + 2, cx, cy + s, col);
}

void spiral(int cx, int cy, float phase, uint16_t col) {
  float px = cx, py = cy;
  for (float a = 0; a < 12.5f; a += 0.25f) {
    float r = a * 1.5f;
    float x = cx + r * cosf(a + phase);
    float y = cy + r * sinf(a + phase);
    canvas.drawLine((int)px, (int)py, (int)x, (int)y, col);
    px = x; py = y;
  }
}

void eyeRect(int cx, int cy, int w, int h, uint16_t col) {
  int r = h / 2 < 12 ? h / 2 : 12;
  canvas.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, col);
}

void drawFace(uint32_t now) {
  const uint16_t BG = 0x0000;
  canvas.fillScreen(BG);

  // ---- blink + look around ----
  static uint32_t nextBlink = 2000, blinkStart = 0, nextLook = 1500;
  static float lookX = 0, lookY = 0, tgtX = 0, tgtY = 0;
  if (now > nextBlink) { blinkStart = now; nextBlink = now + random(2500, 5200); }
  float open = 1.0f;
  uint32_t bt = now - blinkStart;
  if (bt < 180) open = fabsf((float)bt - 90.0f) / 90.0f;
  if (now > nextLook) { tgtX = random(-6, 7); tgtY = random(-3, 4); nextLook = now + random(1200, 3200); }
  lookX += (tgtX - lookX) * 0.2f;
  lookY += (tgtY - lookY) * 0.2f;
  int lkx = (int)lookX, lky = (int)lookY;

  uint16_t col;
  switch (emo) {
    case E_HAPPY: col = C565(0, 255, 160); break;
    case E_LOVE:  col = C565(255, 70, 150); break;
    case E_ANGRY: col = C565(255, 40, 30); break;
    case E_DIZZY: col = C565(255, 220, 0); break;
    default:      col = C565(0, 220, 255); break;
  }
  uint16_t dim = (col >> 2) & 0x39E7;
  uint16_t pink = C565(255, 100, 140);

  int ox = 0;
  if (emo == E_ANGRY) ox = ((now / 45) % 2) ? 2 : -2;   // shaking

  // frame + antenna
  canvas.drawRoundRect(1, 1, 126, 158, 16, col);
  canvas.drawRoundRect(3, 3, 122, 154, 14, dim);
  canvas.drawFastVLine(64, 15, 8, col);
  canvas.fillCircle(64, 12, 3, ((now / 400) % 2) ? col : dim);

  int cy = 70, lx = 36 + ox, rx = 92 + ox;

  switch (emo) {
    case E_NEUTRAL: {
      int h = (int)(46 * open); if (h < 4) h = 4;
      eyeRect(lx + lkx, cy + lky, 38, h, col);
      eyeRect(rx + lkx, cy + lky, 38, h, col);
      if (open > 0.7f) {
        canvas.fillCircle(lx + lkx - 8, cy + lky - 10, 4, WHITE);
        canvas.fillCircle(rx + lkx - 8, cy + lky - 10, 4, WHITE);
      }
      smile(64, 118, 12, 3, col);
      smile(64, 119, 12, 3, col);
      break;
    }
    case E_HAPPY: {
      for (int e = 0; e < 2; e++) {
        int ex = (e ? rx : lx);
        for (int dx = -16; dx <= 16; dx++) {
          int y = cy + lky - 8 + (int)(10.0f * dx * dx / 256.0f);
          canvas.fillCircle(ex + dx, y, 3, col);
        }
      }
      canvas.fillCircle(16, 98, 6, pink);
      canvas.fillCircle(112, 98, 6, pink);
      canvas.fillCircle(64, 104, 15, col);
      canvas.fillRect(46, 86, 36, 18, BG);
      break;
    }
    case E_LOVE: {
      int s = 13 + (int)(2.0f * sinf(now / 150.0f));
      heart(lx + lkx / 2, cy + 2, s, col);
      heart(rx + lkx / 2, cy + 2, s, col);
      canvas.fillCircle(16, 100, 6, C565(255, 120, 160));
      canvas.fillCircle(112, 100, 6, C565(255, 120, 160));
      smile(64, 112, 14, 7, col);
      smile(64, 113, 14, 7, col);
      for (int i = 0; i < 2; i++) {
        int y = 150 - (int)((now / 20 + i * 60) % 130);
        int x = (i ? 114 : 12) + (int)(3 * sinf(now / 300.0f + i));
        heart(x, y, 3, pink);
      }
      break;
    }
    case E_SURPRISED: {
      for (int e = 0; e < 2; e++) {
        int ex = e ? rx : lx;
        canvas.fillCircle(ex, cy, 19, col);
        canvas.fillCircle(ex, cy, 13, BG);
        canvas.fillCircle(ex + lkx / 2, cy + lky / 2, 7, WHITE);
        for (int o = 0; o < 3; o++)
          canvas.drawLine(ex - 14, cy - 28 - (e ? -o : o) / 2 + o, ex + 14, cy - 32 + o, col);
      }
      canvas.fillCircle(64, 120, 9, col);
      canvas.fillCircle(64, 120, 5, BG);
      uint16_t blue = C565(80, 170, 255);
      canvas.fillTriangle(116, 30, 111, 40, 121, 40, blue);
      canvas.fillCircle(116, 42, 5, blue);
      break;
    }
    case E_ANGRY: {
      int top = cy + 4 - 20;
      eyeRect(lx, cy + 4, 38, 40, col);
      eyeRect(rx, cy + 4, 38, 40, col);
      canvas.fillTriangle(lx - 20, top - 1, lx + 20, top - 1, lx + 20, top + 22, BG);
      canvas.fillTriangle(rx + 20, top - 1, rx - 20, top - 1, rx - 20, top + 22, BG);
      smile(64 + ox, 124, 13, -6, col);
      smile(64 + ox, 125, 13, -6, col);
      break;
    }
    case E_DIZZY: {
      float ph = now / 150.0f;
      spiral(lx, cy, ph, col);
      spiral(rx, cy, -ph, col);
      for (int x = -14; x <= 14; x++) {
        int y = 120 + (int)(3 * sinf(x * 0.45f + now / 120.0f));
        canvas.fillCircle(64 + x, y, 1, col);
      }
      break;
    }
  }
}

// =====================================================
//                    DEMO + INPUT
// =====================================================
void applyDemoStep(uint32_t now) {
  if (demoStep < DEMO_FACE_STEPS) {
    state = ST_FACE; emo = demoEmo[demoStep]; demoNext = now + 2500;
  } else {
    state = ST_INFO; emo = E_NEUTRAL; demoNext = now + 4000;
  }
}

void toggleDemo(uint32_t now) {
  demoMode = !demoMode;
  if (demoMode) { demoStep = 0; applyDemoStep(now); }
  else { state = ST_FACE; emo = E_NEUTRAL; lastActivity = now; }
  tapCount = 0;
}

void setEmotionFromTaps(int n, uint32_t now) {
  if (n == 1) emo = E_HAPPY;
  else if (n == 2) emo = E_LOVE;
  else if (n == 3) emo = E_SURPRISED;
  else if (n == 4) emo = E_ANGRY;
  else emo = E_DIZZY;
  state = ST_FACE;
  lastActivity = now;
}

void handleTouch(uint32_t now) {
  static bool raw = false, stable = false, longDone = false;
  static uint32_t chg = 0, pressStart = 0;
  bool r = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE;
  if (r != raw) { raw = r; chg = now; }
  if (now - chg > 30 && stable != raw) {
    stable = raw;
    if (stable) {                       // new press
      pressStart = now;
      longDone = false;
      tapCount++;
      lastTapMs = now;
      lastActivity = now;
      if (!demoMode && state == ST_INFO) { state = ST_FACE; emo = E_NEUTRAL; }
    }
  }
  touching = stable;
  if (touching) lastActivity = now;

  // long press = demo on/off
  if (stable && !longDone && now - pressStart > LONG_PRESS) {
    longDone = true;
    toggleDemo(now);
  }

  // short taps
  if (tapCount > 0 && !touching && now - lastTapMs > TAP_WINDOW) {
    if (demoMode) {                     // tap in demo = next step
      demoStep = (demoStep + 1) % DEMO_STEPS;
      applyDemoStep(now);
    } else {
      setEmotionFromTaps(tapCount, now);
    }
    tapCount = 0;
  }
}

void readDHT(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 3000) return;
  last = now;
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) inTemp = t;
  if (!isnan(h)) inHum = h;
}

// =====================================================
//                    SETUP / LOOP
// =====================================================
void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);   // if colours/position look wrong, try INITR_GREENTAB or INITR_REDTAB
  tft.setSPISpeed(TFT_SPI_HZ); // slower SPI = no random white/black glitches
  tft.setRotation(0);          // portrait 128x160
  tft.fillScreen(0);

  canvas.setTextWrap(false);
  dht.begin();
  randomSeed(esp_random());
  initParticles();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setTxPower(WIFI_POWER_11dBm);   // lower WiFi power = fewer voltage dips
  configTime(GMT_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
  xTaskCreate(weatherTask, "wx", 12288, NULL, 1, NULL);

  lastActivity = millis();
}

void loop() {
  uint32_t now = millis();
  handleTouch(now);
  readDHT(now);

  if (demoMode) {
    if (now >= demoNext) {
      demoStep = (demoStep + 1) % DEMO_STEPS;
      applyDemoStep(now);
    }
  } else if (state == ST_FACE && tapCount == 0 && !touching && now - lastActivity > IDLE_MS) {
    state = ST_INFO;
    emo = E_NEUTRAL;
  }

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    if (state == ST_FACE) drawFace(now); else drawInfo(now);
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCR_W, SCR_H);
  }
}
