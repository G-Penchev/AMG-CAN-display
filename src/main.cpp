#include <SPI.h>
#include <U8g2lib.h>
#include <mcp_can.h>

// -------- Pins --------
static const uint8_t OLED_CS = 10;
static const uint8_t OLED_DC = 9;
static const uint8_t OLED_RST = 8;

static const uint8_t CAN_CS = 7;

static const uint8_t BTN_MODE = 4;
static const uint8_t BTN_PAGE = 5;

enum UiMode
{
  UI_SPLASH,
  UI_PAGES,
  UI_MODE_ANNOUNCE
};
UiMode uiMode = UI_SPLASH;

static const uint32_t SPLASH_MS = 2000;
static const uint32_t MODE_ANNOUNCE_MS = 1500;

uint32_t bootMs = 0;
uint32_t modeAnnounceStartMs = 0;

enum DriveMode : uint8_t
{
  MODE_COMFORT,
  MODE_SPORT,
  MODE_SPORTP,
  MODE_MANUAL
};
DriveMode driveMode = MODE_COMFORT;

// Example CAN values (replace with your real decode)
int gear = 0;      // -1 R, 0 N, 1..n
int rpm = 0;       // 0..9000
int map_kpa = 100; // e.g. 0..250 kPa (adjust to your sensor)

// -------- Display --------
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);

// -------- CAN --------
MCP_CAN CAN0(CAN_CS);

// -------- Splash bitmap --------
#include "amg_logo.h" // defines AMG_W, AMG_H, amg_bits[]

// -------- UI timing --------
static const uint32_t UI_PERIOD_MS = 100; // 10 FPS
static const uint32_t DEBOUNCE_MS = 40;

uint32_t lastUiMs = 0;

// -------- Menu pages --------
enum PageId : uint8_t
{
  PAGE_MAIN = 0,
  PAGE_SENSORS,
  PAGE_FUEL,
  PAGE_DEBUG,
  PAGE_COUNT
};

uint8_t currentPage = PAGE_MAIN;

void cycleDriveMode()
{
  driveMode = (DriveMode)((driveMode + 1) % 4);
  uiMode = UI_MODE_ANNOUNCE;
  modeAnnounceStartMs = millis();
}

// -------- Button state --------
struct Button
{
  uint8_t pin;
  bool stable = true; // stable logical level (true = HIGH because pullup)
  bool lastRead = true;
  uint32_t lastChangeMs = 0;

  void begin(uint8_t p)
  {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    stable = digitalRead(pin);
    lastRead = stable;
    lastChangeMs = millis();
  }

  // Returns true once per press (on stable transition HIGH->LOW)
  bool pressed()
  {
    bool r = digitalRead(pin);
    uint32_t now = millis();

    if (r != lastRead)
    {
      lastRead = r;
      lastChangeMs = now;
    }

    if ((now - lastChangeMs) >= DEBOUNCE_MS && r != stable)
    {
      bool prev = stable;
      stable = r;
      if (prev == HIGH && stable == LOW)
        return true; // pressed
    }
    return false;
  }
};

Button btnMode, btnPage;

// -------- CAN decoded values (fill these from EMU Black frames) --------
volatile uint32_t lastCanMs = 0;

// Examples you said you want:
int tps = 0;    // %
int clt = 0;    // coolant C
int iat = 0;    // intake C
float oilp = 0; // bar/psi
float lambda = 1.00;

// ----------------- Helpers -----------------
void draw_splash()
{
  u8g2.clearBuffer();
  int x = (128 - AMG_W) / 2;
  int y = (64 - AMG_H) / 2;
  u8g2.drawXBMP(x, y, AMG_W, AMG_H, amg_bits);
  u8g2.sendBuffer();
}

const char *gearToStr(int g)
{
  if (g == -1)
    return "R";
  if (g == 0)
    return "N";
  static char buf[4];
  snprintf(buf, sizeof(buf), "%d", g);
  return buf;
}

// ----------------- Page drawing -----------------

const char *driveModeToShort(DriveMode m)
{
  switch (m)
  {
  case MODE_COMFORT:
    return "C";
  case MODE_SPORT:
    return "S";
  case MODE_SPORTP:
    return "S+";
  case MODE_MANUAL:
    return "M";
  default:
    return "C";
  }
}

const char *driveModeToText(DriveMode m)
{
  switch (m)
  {
  case MODE_COMFORT:
    return "Comfort";
  case MODE_SPORT:
    return "Sport";
  case MODE_SPORTP:
    return "Sport+";
  case MODE_MANUAL:
    return "Manual";
  default:
    return "Comfort";
  }
}

void drawProgressBar(int x, int y, int w, int h, int value, int minV, int maxV)
{
  // frame
  u8g2.drawFrame(x, y, w, h);

  if (maxV <= minV)
    return;
  if (value < minV)
    value = minV;
  if (value > maxV)
    value = maxV;

  int fill = (int)((long)(value - minV) * (w - 2) / (maxV - minV));
  if (fill < 0)
    fill = 0;
  if (fill > (w - 2))
    fill = w - 2;

  u8g2.drawBox(x + 1, y + 1, fill, h - 2);
}

void drawProgressBarWithInvertedText(
    int x, int y, int w, int h,
    int value, int minV, int maxV,
    const char *text)
{
  // Compute fill width (inside frame)
  int innerW = w - 2;
  int innerH = h - 2;

  if (maxV <= minV)
    return;
  if (value < minV)
    value = minV;
  if (value > maxV)
    value = maxV;

  int fillW = (int)((long)(value - minV) * innerW / (maxV - minV));
  if (fillW < 0)
    fillW = 0;
  if (fillW > innerW)
    fillW = innerW;

  // Fill (white)
  if (fillW > 0)
  {
    u8g2.setDrawColor(1);
    u8g2.drawBox(x + 1, y + 1, fillW, innerH);
  }

  u8g2.setFont(u8g2_font_6x10_tf);
  // Center text
  int tw = u8g2.getStrWidth(text);
  int ascent = u8g2.getAscent();
  int descent = u8g2.getDescent(); // usually <= 0
  int th = ascent - descent;

  int tx = x + (w - tw) / 2;
  int ty = y + (h - th) / 2 + ascent + 1;

  // 1) Draw text in WHITE everywhere
  u8g2.setDrawColor(1);
  u8g2.setCursor(tx, ty);
  u8g2.print(text);

  // 2) Clip to filled region and draw text in BLACK there
  if (fillW > 0)
  {
    u8g2.setClipWindow(x + 1, y + 1, x + 1 + fillW, y + 1 + innerH);
    u8g2.setDrawColor(0);
    u8g2.setCursor(tx, ty);
    u8g2.print(text);
    u8g2.setMaxClipWindow(); // reset clipping
  }

  u8g2.setDrawColor(1); // restore default

  // Frame
  u8g2.drawFrame(x, y, w, h);
}

void draw_main_page()
{
  u8g2.clearBuffer();

  // --- Top row: Gear centered + mode square to the right ---
  // Big gear text centered at top
  u8g2.setFont(u8g2_font_helvB18_tf);
  const char *g = gearToStr(gear);
  u8g2.drawRFrame(50, 2, 27, 27, 3);

  // Measure text width to truly center
  int gw = u8g2.getStrWidth(g);
  int gx = (128 - gw) / 2;
  int gy = 25; // baseline near top row
  u8g2.setCursor(gx, gy);
  u8g2.print(g);

  // Mode square to the right of gear (small)
  int boxSize = 20;
  int boxX = min(128 - boxSize - 2, gx + gw + 24);
  int boxY = 11;
  u8g2.drawFrame(boxX, boxY, boxSize, boxSize-2);

  u8g2.setFont(u8g2_font_t0_15b_tf);
  const char *ms = driveModeToShort(driveMode);
  int msw = u8g2.getStrWidth(ms);
  u8g2.setCursor(boxX + (boxSize - msw) / 2, boxY + 14);
  u8g2.print(ms);

  char mapTxt[20];
  snprintf(mapTxt, sizeof(mapTxt), "MAP %d kpa", map_kpa);
  drawProgressBarWithInvertedText(0, 34, 128, 11, map_kpa, 0, 250, mapTxt);

  char rpmTxt[20];
  snprintf(rpmTxt, sizeof(rpmTxt), "RPM %d", rpm);
  drawProgressBarWithInvertedText(0, 50, 128, 11, rpm, 0, 9000, rpmTxt);
  u8g2.sendBuffer();
}

void draw_sensors_page()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Sensors");

  u8g2.setCursor(0, 24);
  u8g2.print("CLT: ");
  u8g2.print(clt);
  u8g2.print(" C");
  u8g2.setCursor(0, 36);
  u8g2.print("IAT: ");
  u8g2.print(iat);
  u8g2.print(" C");
  u8g2.setCursor(0, 48);
  u8g2.print("TPS: ");
  u8g2.print(tps);
  u8g2.print(" %");
  u8g2.setCursor(0, 60);
  u8g2.print("RPM: ");
  u8g2.print(rpm);

  u8g2.sendBuffer();
}

void draw_fuel_page()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Fuel / Lambda");

  u8g2.setCursor(0, 28);
  u8g2.print("Lambda: ");
  u8g2.print(lambda, 2);
  u8g2.setCursor(0, 44);
  u8g2.print("OilP: ");
  u8g2.print(oilp, 1);

  u8g2.sendBuffer();
}

void draw_debug_page()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Debug");

  u8g2.setCursor(0, 24);
  u8g2.print("Page: ");
  u8g2.print(currentPage);
  u8g2.setCursor(0, 36);
  u8g2.print("CAN age: ");
  u8g2.print(millis() - lastCanMs);
  u8g2.print("ms");

  u8g2.sendBuffer();
}

void draw_current_page()
{
  switch (currentPage)
  {
  case PAGE_MAIN:
    draw_main_page();
    break;
  case PAGE_SENSORS:
    draw_sensors_page();
    break;
  case PAGE_FUEL:
    draw_fuel_page();
    break;
  case PAGE_DEBUG:
    draw_debug_page();
    break;
  default:
    draw_main_page();
    break;
  }
}

// ----------------- Page navigation -----------------
void next_page()
{
  currentPage = (currentPage + 1) % PAGE_COUNT;
}
void prev_page()
{
  currentPage = (currentPage == 0) ? (PAGE_COUNT - 1) : (currentPage - 1);
}

// ----------------- CAN reading -----------------
void read_can()
{
  // Safety: ensure OLED not selected while talking to CAN (shared SPI bus)
  digitalWrite(OLED_CS, HIGH);

  while (CAN0.checkReceive() == CAN_MSGAVAIL)
  {
    unsigned long canId = 0;
    uint8_t len = 0;
    uint8_t buf[8];
    CAN0.readMsgBuf(&canId, &len, buf);
    lastCanMs = millis();

    // TODO: Replace these with *real* EMU Black IDs and scaling.
    // Example placeholders:
    if (canId == 0x100 && len >= 2)
      rpm = (int)(buf[0] | (buf[1] << 8));
    if (canId == 0x101 && len >= 1)
      gear = (int8_t)buf[0]; // maybe signed
    if (canId == 0x102 && len >= 1)
      tps = buf[0];
    if (canId == 0x103 && len >= 1)
      clt = (int8_t)buf[0];
    if (canId == 0x104 && len >= 1)
      iat = (int8_t)buf[0];

    // etc...
  }
}

void draw_mode_announcement()
{
  u8g2.clearBuffer();

  // AMG logo
  int logoX = (128 - AMG_W) / 2;
  int logoY = 10;
  u8g2.drawXBMP(logoX, logoY, AMG_W, AMG_H, amg_bits);

  // Drive mode text (wide + bold)
  u8g2.setFont(u8g2_font_helvB14_tf);
  const char *txt = driveModeToText(driveMode);

  int tw = u8g2.getStrWidth(txt);
  int tx = (128 - tw) / 2;
  int ty = logoY + AMG_H + 16;

  u8g2.setCursor(tx, ty);
  u8g2.print(txt);

  u8g2.sendBuffer();
}

void setup()
{
  // CS pins idle high
  pinMode(OLED_CS, OUTPUT);
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(OLED_CS, HIGH);
  digitalWrite(CAN_CS, HIGH);

  btnMode.begin(BTN_MODE);
  btnPage.begin(BTN_PAGE);

  SPI.begin();
  u8g2.begin();

  // // Init CAN: adjust bitrate + oscillator for your MCP2515 module
  // // Common: MCP_8MHZ or MCP_16MHZ. EMU Black often 500kbps depending on config.
  // if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
  //   u8g2.clearBuffer();
  //   u8g2.setFont(u8g2_font_6x10_tf);
  //   u8g2.drawStr(0, 12, "CAN init FAIL");
  //   u8g2.sendBuffer();
  //   while (true) {}
  // }
  // CAN0.setMode(MCP_NORMAL);

  bootMs = millis();
  uiMode = UI_SPLASH;
  draw_splash(); // draw once
}

void loop()
{
  read_can(); // keep CAN serviced always

  // Buttons
  if (btnMode.pressed())
    cycleDriveMode();
  if (btnPage.pressed())
    next_page();

  // Throttle UI redraw (e.g. 10 fps)
  static const uint32_t UI_PERIOD_MS = 100;
  static uint32_t lastUiMs = 0;
  uint32_t now = millis();
  if (now - lastUiMs < UI_PERIOD_MS)
    return;
  lastUiMs = now;

  // State machine
  if (uiMode == UI_SPLASH)
  {
    if (now - bootMs >= SPLASH_MS)
    {
      uiMode = UI_PAGES;
      currentPage = PAGE_MAIN;
      draw_current_page();
    }
    return; // don't redraw splash continuously (assuming drawn once in setup)
  }

  if (uiMode == UI_MODE_ANNOUNCE)
  {
    draw_mode_announcement();
    if (now - modeAnnounceStartMs >= MODE_ANNOUNCE_MS)
    {
      uiMode = UI_PAGES;
      draw_current_page();
    }
    return;
  }

  // Normal pages mode
  draw_current_page();
}
