#include "display.h"

#if DISPLAY_TYPE == 1

// --- OLED SSD1306 via U8g2 ---
// Monochrome adaptation of the color theme:
//   - Inverted header bar (white box, black text) emulates navy blue header
//   - 2px status accent bar on left edge (solid=TX, dashed=idle, etc.)
//   - State line uses inverted text for TX/LOWBAT emphasis
#include <U8g2lib.h>
#include <Wire.h>
#include "display_colors.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
static bool displayOn = true;

// Draw the inverted header bar (emulates navy blue header on TFT).
// Fills a black box and draws text in white-on-black by inverting: we draw
// a filled box, then use setDrawColor(0) to erase text pixels inside it.
static void drawHeader(const char *text) {
  u8g2.setFont(u8g2_font_8x13B_tr);
  int w = u8g2.getStrWidth(text);
  u8g2.drawBox(0, 0, w + 4, 15);
  u8g2.setDrawColor(0);
  u8g2.drawStr(2, 12, text);
  u8g2.setDrawColor(1);
}

// Draw a 2px-wide status accent bar on the left edge.
// Pattern encodes state: solid=TX/BEACON, dashed=IDLE, double=LOWBAT, dotted=STARTUP
static void drawAccentBar(const char *stateStr) {
  String s = stateStr;
  s.toUpperCase();
  if (s == "TX" || s == "BEACON") {
    u8g2.drawBox(0, 0, 2, 64);              // solid
  } else if (s == "LOWBAT") {
    for (int y = 0; y < 64; y += 6)         // double dashes
      u8g2.drawBox(0, y, 2, 3);
  } else if (s == "STARTUP") {
    for (int y = 0; y < 64; y += 4)         // dotted
      u8g2.drawBox(0, y, 2, 2);
  } else {
    for (int y = 0; y < 64; y += 8)         // dashed (IDLE)
      u8g2.drawBox(0, y, 2, 4);
  }
}

void displayInit(const char *callsign, const char *version) {
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  displayStartupScreen(callsign, version);
}

void displayStartupScreen(const char *callsign, const char *version) {
  u8g2.clearBuffer();
  char header[24];
  snprintf(header, sizeof(header), "%s Fox", callsign);
  drawHeader(header);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, 34);
  u8g2.print("v");
  u8g2.print(version);
  u8g2.setCursor(0, 50);
  u8g2.print("Starting...");
  u8g2.sendBuffer();
}

void displayUpdate(const char *callsign, const char *foxId,
                   const char *modeStr, const char *stateStr,
                   const char *timingStr, const char *batteryStr,
                   const char *ipStr) {
  if (!displayOn) return;
  u8g2.clearBuffer();

  // Status accent bar (left edge)
  drawAccentBar(stateStr);

  // Inverted header bar with callsign + fox ID
  char header[24];
  snprintf(header, sizeof(header), "%s %s", callsign, foxId);
  drawHeader(header);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, 26);
  u8g2.print(modeStr);

  // State line: inverted (highlighted) for TX/BEACON/LOWBAT, plain for others
  String state = stateStr;
  state.toUpperCase();
  bool highlight = (state == "TX" || state == "BEACON" || state == "LOWBAT");
  if (highlight) {
    int w = u8g2.getStrWidth(stateStr);
    u8g2.drawBox(0, 36, w + 4, 11);
    u8g2.setDrawColor(0);
    u8g2.drawStr(2, 45, stateStr);
    u8g2.setDrawColor(1);
  } else {
    u8g2.setCursor(0, 45);
    u8g2.print(stateStr);
  }

  u8g2.setCursor(0, 56);
  u8g2.print(timingStr);
  u8g2.setCursor(0, 63);
  if (batteryStr[0]) {
    u8g2.print(batteryStr);
    u8g2.print("  ");
  }
  u8g2.print(ipStr);
  u8g2.sendBuffer();
}

void displayMenu(int selectedIndex, int itemCount,
                 const char *const labels[], const char *const values[]) {
  if (!displayOn) return;
  u8g2.clearBuffer();

  // Inverted header bar
  drawHeader("Settings");
  u8g2.setFont(u8g2_font_6x10_tr);

  // Show up to 5 items, scrolling if needed
  int visibleStart = 0;
  if (itemCount > 5 && selectedIndex > 3) {
    visibleStart = selectedIndex - 3;
    if (visibleStart > itemCount - 5) visibleStart = itemCount - 5;
  }
  int visibleCount = (itemCount < 5) ? itemCount : 5;

  for (int i = 0; i < visibleCount; i++) {
    int idx = visibleStart + i;
    if (idx >= itemCount) break;
    int y = 26 + i * 10;
    if (idx == selectedIndex) {
      // Inverted highlight for selected item
      char line[24];
      snprintf(line, sizeof(line), "%-10s %s", labels[idx], values[idx]);
      int w = u8g2.getStrWidth(line);
      u8g2.drawBox(0, y - 8, w + 4, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, line);
      u8g2.setDrawColor(1);
    } else {
      char line[24];
      snprintf(line, sizeof(line), "%-10s %s", labels[idx], values[idx]);
      u8g2.drawStr(0, y, line);
    }
  }

  // Footer hint
  u8g2.drawStr(0, 63, "1=Next 2=Toggle Hold=Exit");
  u8g2.sendBuffer();
}

void displayAPMode(const char *ssid, const char *ip) {
  if (!displayOn) return;
  u8g2.clearBuffer();
  drawHeader("AP MODE");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, 28);
  u8g2.print("SSID:");
  u8g2.setCursor(0, 40);
  u8g2.print(ssid);
  u8g2.setCursor(0, 52);
  u8g2.print(ip);
  u8g2.setCursor(0, 63);
  u8g2.print("Web admin ready");
  u8g2.sendBuffer();
}

void displayPower(bool on) {
  displayOn = on;
  if (on) {
    u8g2.setPowerSave(0);
  } else {
    u8g2.setPowerSave(1);
  }
}

void displayClear() {
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

#elif DISPLAY_TYPE == 2

// --- TFT ST7789/ST7735 via TFT_eSPI ---
#define TFT_PIN_CONFLICT(pin) \
  ((pin) == TFT_MOSI || (pin) == TFT_SCLK || (pin) == TFT_CS || \
   (pin) == TFT_DC || (pin) == TFT_RST || ((TFT_BL) >= 0 && (pin) == TFT_BL))
#if TFT_PIN_CONFLICT(PTT_PIN) || TFT_PIN_CONFLICT(AUDIO_PIN) || \
    TFT_PIN_CONFLICT(LED_PIN) || TFT_PIN_CONFLICT(BUTTON_PIN) || \
    TFT_PIN_CONFLICT(BATTERY_PIN)
  #error "Beacon GPIO assignments conflict with the TFT display pins"
#endif
#undef TFT_PIN_CONFLICT

// Color theme adapted from LoRa_APRS_Tracker (CA2RXU):
//   - Navy blue header bar with yellow text
//   - Content-based body coloring (TX=green, LOWBAT=red, battery=orange)
//   - 2px status accent bar on left edge (green=TX, yellow=idle, red=lowbat)
#include <TFT_eSPI.h>
#include "display_colors.h"

TFT_eSPI tft = TFT_eSPI();
static bool displayOn = true;

// Last content drawn. A full-screen redraw flickers, so skip it when nothing
// changed. Cleared whenever the screen is wiped for another reason.
static String lastContent;

// Layout scales with panel height: small panels (Heltec Wireless Tracker,
// 160x80) use the small font and tighter rows so every line stays visible.
struct TftLayout {
  int headerH;
  int headerFont;
  int bodyFont;
  int bodyTop;
  int lineH;
  int menuRowH;
  int footerFont;
};

static TftLayout tftLayout() {
  if (tft.height() < 120) {
    return {18, 2, 1, 20, 10, 10, 1};
  }
  return {30, 4, 2, 36, 20, 22, 1};
}

static void tftPowerOn() {
#ifdef TFT_VEXT_1_PIN
  pinMode(TFT_VEXT_1_PIN, OUTPUT);
  digitalWrite(TFT_VEXT_1_PIN, TFT_VEXT_1_ACTIVE_LOW ? LOW : HIGH);
#endif
#ifdef TFT_VEXT_2_PIN
  pinMode(TFT_VEXT_2_PIN, OUTPUT);
  digitalWrite(TFT_VEXT_2_PIN, TFT_VEXT_2_ACTIVE_LOW ? LOW : HIGH);
#endif
#ifdef TFT_VEXT_3_PIN
  pinMode(TFT_VEXT_3_PIN, OUTPUT);
  digitalWrite(TFT_VEXT_3_PIN, TFT_VEXT_3_ACTIVE_LOW ? LOW : HIGH);
#endif
#ifdef TFT_VEXT_4_PIN
  pinMode(TFT_VEXT_4_PIN, OUTPUT);
  digitalWrite(TFT_VEXT_4_PIN, TFT_VEXT_4_ACTIVE_LOW ? LOW : HIGH);
#endif
#if TFT_BL >= 0
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif
  delay(10);
}

// Pick a status accent bar color from the beacon state string.
static uint16_t stateAccentColor(const char *stateStr) {
  String s = stateStr;
  s.toUpperCase();
  if (s == "TX" || s == "BEACON")      return FOX_GREEN;
  if (s == "LOWBAT")                    return FOX_RED;
  if (s == "STARTUP")                   return FOX_ORANGE;
  return FOX_GREY;  // IDLE
}

// Pick a body text color for the state line.
static uint16_t stateTextColor(const char *stateStr) {
  String s = stateStr;
  s.toUpperCase();
  if (s == "TX" || s == "BEACON")      return FOX_GREEN;
  if (s == "LOWBAT")                    return FOX_RED;
  if (s == "STARTUP")                   return FOX_ORANGE;
  return FOX_CYAN;  // IDLE
}

void displayInit(const char *callsign, const char *version) {
  tftPowerOn();
  tft.init();
  tft.setRotation(1);
  displayStartupScreen(callsign, version);
}

void displayStartupScreen(const char *callsign, const char *version) {
  const TftLayout l = tftLayout();
  lastContent = "";
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);

  // Navy blue header bar with yellow text
  tft.fillRect(0, 0, tft.width(), l.headerH, COLOR_HEADER_BG);
  tft.setTextColor(COLOR_HEADER_FG, COLOR_HEADER_BG);
  String header = String(callsign) + " Fox";
  tft.drawString(header, 4, 2, l.headerFont);

  // Body
  tft.setTextColor(FOX_CYAN, COLOR_BG);
  String ver = "v" + String(version);
  tft.drawString(ver, 4, l.bodyTop, l.bodyFont);
  tft.setTextColor(FOX_ORANGE, COLOR_BG);
  tft.drawString("Starting...", 4, l.bodyTop + l.lineH, l.bodyFont);
}

void displayUpdate(const char *callsign, const char *foxId,
                   const char *modeStr, const char *stateStr,
                   const char *timingStr, const char *batteryStr,
                   const char *ipStr) {
  if (!displayOn) return;
  const String content = String("S|") + callsign + "|" + foxId + "|" + modeStr + "|" +
                         stateStr + "|" + timingStr + "|" + batteryStr + "|" + ipStr;
  if (content == lastContent) return;
  lastContent = content;

  const TftLayout l = tftLayout();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);

  // 2px status accent bar on left edge
  tft.fillRect(0, 0, 2, tft.height(), stateAccentColor(stateStr));

  // Navy blue header bar with yellow callsign + fox ID
  tft.fillRect(2, 0, tft.width() - 2, l.headerH, COLOR_HEADER_BG);
  tft.setTextColor(COLOR_HEADER_FG, COLOR_HEADER_BG);
  tft.drawString(String(callsign) + " " + foxId, 6, 2, l.headerFont);

  int y = l.bodyTop;

  // Mode line (yellow accent)
  tft.setTextColor(FOX_YELLOW, COLOR_BG);
  tft.drawString(String(modeStr), 4, y, l.bodyFont);
  y += l.lineH;

  // State line (color-coded by state)
  tft.setTextColor(stateTextColor(stateStr), COLOR_BG);
  tft.drawString(String(stateStr), 4, y, l.bodyFont);
  y += l.lineH;

  // Timing line (cyan)
  tft.setTextColor(FOX_CYAN, COLOR_BG);
  tft.drawString(String(timingStr), 4, y, l.bodyFont);
  y += l.lineH;

  // Battery line (orange if present)
  if (batteryStr[0]) {
    tft.setTextColor(FOX_ORANGE, COLOR_BG);
    tft.drawString(String(batteryStr), 4, y, l.bodyFont);
    y += l.lineH;
  }

  // IP line (green)
  if (ipStr[0]) {
    tft.setTextColor(FOX_GREEN, COLOR_BG);
    tft.drawString(String(ipStr), 4, y, l.bodyFont);
  }
}

void displayMenu(int selectedIndex, int itemCount,
                 const char *const labels[], const char *const values[]) {
  if (!displayOn) return;
  String content = String("M|") + selectedIndex;
  for (int i = 0; i < itemCount; i++) {
    content += "|";
    content += values[i];
  }
  if (content == lastContent) return;
  lastContent = content;

  const TftLayout l = tftLayout();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);

  // Navy blue header bar
  tft.fillRect(0, 0, tft.width(), l.headerH, COLOR_HEADER_BG);
  tft.setTextColor(COLOR_HEADER_FG, COLOR_HEADER_BG);
  tft.drawString("Settings", 4, 2, l.headerFont);

  // Fit as many rows as the panel allows between header and footer, and
  // scroll so the selected row is always visible.
  const int footerY = tft.height() - (l.footerFont == 1 ? 10 : 16);
  const int firstRowY = l.headerH + 6;
  int visibleCount = (footerY - firstRowY) / l.menuRowH;
  if (visibleCount < 1) visibleCount = 1;
  if (visibleCount > itemCount) visibleCount = itemCount;
  int visibleStart = 0;
  if (selectedIndex >= visibleCount) {
    visibleStart = selectedIndex - visibleCount + 1;
  }

  for (int i = 0; i < visibleCount; i++) {
    int idx = visibleStart + i;
    if (idx >= itemCount) break;
    int y = firstRowY + i * l.menuRowH;
    if (idx == selectedIndex) {
      tft.setTextColor(FOX_YELLOW, COLOR_BG);
      tft.drawString(">", 4, y, l.bodyFont);
    } else {
      tft.setTextColor(FOX_WHITE, COLOR_BG);
    }
    char line[32];
    snprintf(line, sizeof(line), "%-12s %s", labels[idx], values[idx]);
    tft.drawString(line, 18, y, l.bodyFont);
    tft.setTextColor(FOX_WHITE, COLOR_BG);
  }

  // Footer hint (orange)
  tft.setTextColor(FOX_ORANGE, COLOR_BG);
  tft.drawString("1=Next 2=Toggle Hold=Exit", 4, footerY, l.footerFont);
}

void displayAPMode(const char *ssid, const char *ip) {
  if (!displayOn) return;
  lastContent = "";
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);

  // Navy blue header bar
  tft.fillRect(0, 0, tft.width(), 30, COLOR_HEADER_BG);
  tft.setTextColor(COLOR_HEADER_FG, COLOR_HEADER_BG);
  tft.drawString("AP MODE", 4, 4, 4);

  tft.setTextColor(FOX_CYAN, COLOR_BG);
  tft.drawString("SSID:", 4, 40, 2);
  tft.setTextColor(FOX_WHITE, COLOR_BG);
  tft.drawString(String(ssid), 4, 58, 2);
  tft.setTextColor(FOX_GREEN, COLOR_BG);
  tft.drawString(String(ip), 4, 78, 2);
  tft.setTextColor(FOX_ORANGE, COLOR_BG);
  tft.drawString("Web admin ready", 4, 98, 2);
}

void displayPower(bool on) {
  displayOn = on;
#if TFT_BL >= 0
  digitalWrite(TFT_BL, on ? HIGH : LOW);
#endif
  if (on) {
    tft.fillScreen(COLOR_BG);
    lastContent = "";  // force a full redraw on wake
  }
}

void displayClear() {
  tft.fillScreen(COLOR_BG);
  lastContent = "";
}

#elif DISPLAY_TYPE == 3

// --- E-Ink SSD1680 via GxEPD2 ---
// Monochrome adaptation of the color theme (same approach as OLED):
//   - Inverted header bar (black box, white text) emulates navy blue header
//   - 2px status accent bar on left edge (pattern-coded by state)
//   - State line uses inverted text for TX/LOWBAT emphasis
#include <GxEPD2_BW.h>
#include <SPI.h>
#include "display_colors.h"

// Select the correct panel class based on resolution
#define EINK_PIN_CONFLICT(pin) \
  ((pin) == EINK_SCLK || (pin) == EINK_MOSI || (pin) == EINK_CS || \
   (pin) == EINK_DC || (pin) == EINK_RST || (pin) == EINK_BUSY || \
   (pin) == EINK_VEXT)
#ifdef EINK_VEXT_2
  #define EINK_PIN_CONFLICT_2(pin) ((pin) == EINK_VEXT_2)
#else
  #define EINK_PIN_CONFLICT_2(pin) 0
#endif
#if EINK_PIN_CONFLICT(PTT_PIN) || EINK_PIN_CONFLICT(AUDIO_PIN) || \
    EINK_PIN_CONFLICT(LED_PIN) || EINK_PIN_CONFLICT(BUTTON_PIN) || \
    EINK_PIN_CONFLICT(BATTERY_PIN) || EINK_PIN_CONFLICT_2(PTT_PIN) || \
    EINK_PIN_CONFLICT_2(AUDIO_PIN) || EINK_PIN_CONFLICT_2(LED_PIN) || \
    EINK_PIN_CONFLICT_2(BUTTON_PIN) || EINK_PIN_CONFLICT_2(BATTERY_PIN)
  #error "Beacon GPIO assignments conflict with the E-Ink display pins"
#endif
#undef EINK_PIN_CONFLICT_2
#undef EINK_PIN_CONFLICT

#if EINK_WIDTH == 250 && EINK_HEIGHT == 122
  // 2.13" BW (250x122) — DEPG0213BN / SSD1680
  static GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> eink(
      GxEPD2_213_BN(EINK_CS, EINK_DC, EINK_RST, EINK_BUSY));
#elif EINK_WIDTH == 296 && EINK_HEIGHT == 128
  // 2.9" BW (296x128) — DEPG0290BS / SSD1680
  static GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> eink(
      GxEPD2_290_BS(EINK_CS, EINK_DC, EINK_RST, EINK_BUSY));
#else
  #error "Unsupported E-Ink resolution. Add a panel class for this size."
#endif

static bool displayOn = true;
static bool firstRefresh = true;

// Last content drawn. E-Ink refreshes are slow (hundreds of ms) and wear the
// panel, so only refresh when the text actually changes.
static String lastContent;

// Draw inverted header bar (black box with white text) — emulates navy blue
// header on TFT. GxEPD2 uses BLACK as foreground; we fill a rect then write
// text in WHITE over it.
static void einkDrawHeader(const char *text) {
  int16_t x1, y1;
  uint16_t w, h;
  eink.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  eink.fillRect(0, 0, w + 4, h + 2, GxEPD_BLACK);
  eink.setTextColor(GxEPD_WHITE);
  eink.setCursor(2, 0);
  eink.print(text);
  eink.setTextColor(GxEPD_BLACK);
}

// Draw 2px status accent bar on left edge, pattern-coded by state.
static void einkDrawAccentBar(const char *stateStr) {
  String s = stateStr;
  s.toUpperCase();
  const int h = EINK_HEIGHT;
  if (s == "TX" || s == "BEACON") {
    eink.fillRect(0, 0, 2, h, GxEPD_BLACK);           // solid
  } else if (s == "LOWBAT") {
    for (int y = 0; y < h; y += 6)                     // double dashes
      eink.fillRect(0, y, 2, 3, GxEPD_BLACK);
  } else if (s == "STARTUP") {
    for (int y = 0; y < h; y += 4)                     // dotted
      eink.fillRect(0, y, 2, 2, GxEPD_BLACK);
  } else {
    for (int y = 0; y < h; y += 8)                     // dashed (IDLE)
      eink.fillRect(0, y, 2, 4, GxEPD_BLACK);
  }
}

static void einkRefresh() {
  if (firstRefresh) {
    eink.display(false);
    firstRefresh = false;
  } else {
    eink.display(true);  // partial refresh for speed
  }
}

static void einkPowerOn() {
#ifdef EINK_VEXT
  #ifdef EINK_VEXT_ACTIVE_LOW
    pinMode(EINK_VEXT, OUTPUT);
    digitalWrite(EINK_VEXT, LOW);  // active LOW: LOW = ON
  #else
    pinMode(EINK_VEXT, OUTPUT);
    digitalWrite(EINK_VEXT, HIGH);  // active HIGH: HIGH = ON
  #endif
  delay(10);
#endif
#ifdef EINK_VEXT_2
  pinMode(EINK_VEXT_2, OUTPUT);
  digitalWrite(EINK_VEXT_2, HIGH);
  delay(10);
#endif
}

static void einkInit() {
  einkPowerOn();
  eink.init();
  eink.setRotation(1);
  eink.setTextColor(GxEPD_BLACK);
  eink.setTextSize(1);
  firstRefresh = true;
  lastContent = "";
}

void displayInit(const char *callsign, const char *version) {
  SPI.begin(EINK_SCLK, -1, EINK_MOSI, EINK_CS);
  einkInit();
  displayStartupScreen(callsign, version);
}

void displayStartupScreen(const char *callsign, const char *version) {
  lastContent = "";
  eink.setFullWindow();
  eink.fillScreen(GxEPD_WHITE);
  char header[24];
  snprintf(header, sizeof(header), "%s Fox", callsign);
  einkDrawHeader(header);
  eink.setCursor(0, 16);
  eink.print("v");
  eink.print(version);
  eink.setCursor(0, 32);
  eink.print("Starting...");
  einkRefresh();
}

void displayUpdate(const char *callsign, const char *foxId,
                   const char *modeStr, const char *stateStr,
                   const char *timingStr, const char *batteryStr,
                   const char *ipStr) {
  if (!displayOn) return;
  const String content = String("S|") + callsign + "|" + foxId + "|" + modeStr + "|" +
                         stateStr + "|" + timingStr + "|" + batteryStr + "|" + ipStr;
  if (content == lastContent) return;
  lastContent = content;
  eink.setFullWindow();
  eink.fillScreen(GxEPD_WHITE);

  // Status accent bar (left edge)
  einkDrawAccentBar(stateStr);

  // Inverted header bar
  char header[24];
  snprintf(header, sizeof(header), "%s %s", callsign, foxId);
  einkDrawHeader(header);

  eink.setCursor(0, 16);
  eink.print(modeStr);

  // State line: inverted for TX/BEACON/LOWBAT
  String state = stateStr;
  state.toUpperCase();
  if (state == "TX" || state == "BEACON" || state == "LOWBAT") {
    int16_t x1, y1;
    uint16_t w, h;
    eink.getTextBounds(stateStr, 0, 0, &x1, &y1, &w, &h);
    eink.fillRect(0, 26, w + 4, h + 2, GxEPD_BLACK);
    eink.setTextColor(GxEPD_WHITE);
    eink.setCursor(2, 26);
    eink.print(stateStr);
    eink.setTextColor(GxEPD_BLACK);
  } else {
    eink.setCursor(0, 28);
    eink.print(stateStr);
  }

  eink.setCursor(0, 40);
  eink.print(timingStr);
  eink.setCursor(0, 52);
  if (batteryStr[0]) {
    eink.print(batteryStr);
    eink.print("  ");
  }
  eink.print(ipStr);
  einkRefresh();
}

void displayMenu(int selectedIndex, int itemCount,
                 const char *const labels[], const char *const values[]) {
  if (!displayOn) return;
  String content = String("M|") + selectedIndex;
  for (int i = 0; i < itemCount; i++) {
    content += "|";
    content += values[i];
  }
  if (content == lastContent) return;
  lastContent = content;
  eink.setFullWindow();
  eink.fillScreen(GxEPD_WHITE);
  einkDrawHeader("Settings");

  int visibleStart = 0;
  if (itemCount > 5 && selectedIndex > 3) {
    visibleStart = selectedIndex - 3;
    if (visibleStart > itemCount - 5) visibleStart = itemCount - 5;
  }
  int visibleCount = (itemCount < 5) ? itemCount : 5;

  for (int i = 0; i < visibleCount; i++) {
    int idx = visibleStart + i;
    if (idx >= itemCount) break;
    int y = 24 + i * 10;
    if (idx == selectedIndex) {
      // Inverted highlight for selected item
      char line[24];
      snprintf(line, sizeof(line), "%s %s", labels[idx], values[idx]);
      int16_t x1, y1;
      uint16_t w, h;
      eink.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
      eink.fillRect(0, y - 8, w + 4, h + 2, GxEPD_BLACK);
      eink.setTextColor(GxEPD_WHITE);
      eink.setCursor(2, y - 8);
      eink.print(line);
      eink.setTextColor(GxEPD_BLACK);
    } else {
      eink.setCursor(4, y);
      eink.print(labels[idx]);
      eink.print(" ");
      eink.print(values[idx]);
    }
  }

  eink.setCursor(0, EINK_HEIGHT - 8);
  eink.print("1=Next 2=Toggle Hold=Exit");
  einkRefresh();
}

void displayAPMode(const char *ssid, const char *ip) {
  if (!displayOn) return;
  lastContent = "";
  eink.setFullWindow();
  eink.fillScreen(GxEPD_WHITE);
  einkDrawHeader("AP MODE");
  eink.setCursor(0, 24);
  eink.print("SSID:");
  eink.setCursor(0, 36);
  eink.print(ssid);
  eink.setCursor(0, 48);
  eink.print(ip);
  eink.setCursor(0, 60);
  eink.print("Web admin ready");
  einkRefresh();
}

void displayPower(bool on) {
  displayOn = on;
#ifdef EINK_VEXT
  if (!on) {
    #ifdef EINK_VEXT_ACTIVE_LOW
      digitalWrite(EINK_VEXT, HIGH);  // active LOW: HIGH = OFF
    #else
      digitalWrite(EINK_VEXT, LOW);   // active HIGH: LOW = OFF
    #endif
  } else {
    einkInit();
  }
#endif
#ifdef EINK_VEXT_2
  digitalWrite(EINK_VEXT_2, on ? HIGH : LOW);
#endif
}

void displayClear() {
  eink.setFullWindow();
  eink.fillScreen(GxEPD_WHITE);
  eink.display(false);
  lastContent = "";
}

#else

// --- No display ---
void displayInit(const char *, const char *) {}
void displayStartupScreen(const char *, const char *) {}
void displayUpdate(const char *, const char *, const char *, const char *,
                   const char *, const char *, const char *) {}
void displayMenu(int, int, const char *const[], const char *const[]) {}
void displayAPMode(const char *, const char *) {}
void displayPower(bool) {}
void displayClear() {}

#endif
