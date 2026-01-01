#include <SPI.h>
#include <U8g2lib.h>
#include <mcp_can.h>
// -------- Splash bitmap --------
#include "amg_logo.h" // defines AMG_W, AMG_H, amg_bits[]

// -------- Pins --------
static const uint8_t OLED_CS = 10;
static const uint8_t OLED_DC = 9;
static const uint8_t OLED_RST = 8;

static const uint8_t CAN_CS = 7;

static const uint8_t BTN_MODE = 4;
static const uint8_t BTN_PAGE = 5;

static const uint32_t BOTH_HOLD_MS = 1000;
static const uint32_t SELECT_WINDOW_MS = 3000;

static const uint8_t PADDLE_L_PIN = 2;
static const uint8_t PADDLE_R_PIN = 3;

static const uint32_t SPLASH_MS = 2000;
static const uint32_t MODE_ANNOUNCE_MS = 1500;

uint32_t bootMs = 0;
uint32_t modeAnnounceStartMs = 0;

enum Prnd : uint8_t
{
  PRND_P,
  PRND_R,
  PRND_N,
  PRND_D
};

Prnd prnd = PRND_P;

enum UiMode
{
  UI_SPLASH,
  UI_PAGES,
  UI_MODE_ANNOUNCE
};
UiMode uiMode = UI_SPLASH;

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
      Serial.print("button ");
      Serial.print(pin);
      Serial.println(" pressed");
    }
    return false;
  }

  // call every loop; returns true once on press edge (HIGH->LOW)
  bool pressedEdge()
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
        return true;
    }
    return false;
  }

  bool isPressedNow() const { return stable == LOW; }
};

Button btnMode, btnPage, paddleL, paddleR;

// combo / window tracking
bool bothHoldArmed = false;
uint32_t bothPressedSinceMs = 0;

bool selectWindowActive = false;
uint32_t selectWindowStartMs = 0;

const char *prndToStr(Prnd p)
{
  switch (p)
  {
  case PRND_P:
    return "P";
  case PRND_R:
    return "R";
  case PRND_N:
    return "N";
  case PRND_D:
    return "D";
  default:
    return "?";
  }
}

Prnd lastPrintedPrnd = PRND_P;

void onPrndChanged(Prnd newState)
{
  if (newState == lastPrintedPrnd)
    return;

  lastPrintedPrnd = newState;
}

// -------- CAN decoded values (fill these from EMU Black frames) --------
volatile uint32_t lastCanMs = 0;

// Examples you said you want:
int tps = 0;    // %
int clt = 0;    // coolant C
int iat = 0;    // intake C
float oilp = 0; // bar/psi
float lambda = 1.00;
uint32_t odo = 423911;

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
    return "COMFORT";
  case MODE_SPORT:
    return "SPORT";
  case MODE_SPORTP:
    return "SPORT+";
  case MODE_MANUAL:
    return "MANUAL";
  default:
    return "COMFORT";
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
  u8g2.drawRFrame(x, y, w, h, 3);
}

void drawLambdaLine(
    int x, int y, int w, int h,
    float lambda,
    float minL, float maxL)
{
  // Frame
  u8g2.drawFrame(x, y, w, h);

  // Clamp
  if (lambda < minL)
    lambda = minL;
  if (lambda > maxL)
    lambda = maxL;

  // Inner dimensions
  int innerW = w - 2;
  int innerH = h - 2;

  // Position of lambda line
  int lineX = x + 1 + (int)((lambda - minL) * innerW / (maxL - minL));

  // Draw center reference line (optional, stoich = 1.00)
  if (minL < 1.0f && maxL > 1.0f)
  {
    int stoichX = x + 1 + (int)((1.0f - minL) * innerW / (maxL - minL));
    u8g2.drawVLine(stoichX, y + 1, innerH);
  }

  // Draw lambda marker (thicker for visibility)
  u8g2.drawVLine(lineX, y + 1, innerH);
  u8g2.drawVLine(lineX + 1, y + 1, innerH); // thickness = 2px
}

void drawDriveMode(int boxX, int boxY, boolean isShort)
{
  // Mode square to the right of gear (small)
  int boxSize = 15;
  const char *ms;
  if (isShort)
  {
    u8g2.setFont(u8g2_font_t0_15b_tf);
    ms = driveModeToShort(driveMode);
  }
  else
  {
    u8g2.setFont(u8g2_font_5x7_tf);
    ms = driveModeToText(driveMode);
  }
  int msw = u8g2.getStrWidth(ms);
  u8g2.setCursor(boxX + (boxSize - msw) / 2, boxY + 10);
  u8g2.print(ms);
}

void drawSelectionActive(int x, int y, int size)
{
  // y is top of triangle
  u8g2.drawTriangle(
      x + size / 2, y,
      x + size / 2, y + size,
      x, y + size / 2);

  int offset = size / 2 + 2;

  u8g2.drawTriangle(
      x + offset, y,
      x + offset, y + size,
      x + size / 2 + offset, y + size / 2);
}

void drawPRND(int x, int y)
{
  if (selectWindowActive)
  {
    drawSelectionActive(x + 16, y - 7, 6);
  }

  u8g2.drawBox(x + 16, y, 9, 13);
  u8g2.setDrawColor(0);
  u8g2.setCursor(x + 16, y + 12);
  u8g2.setFont(u8g2_font_t0_18b_tr);
  u8g2.print(prndToStr(prnd));
  u8g2.setDrawColor(1);

  switch (prnd)
  {
  case PRND_P:
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(x + 26, y + 10);
    u8g2.print("RND");
    break;

  case PRND_R:
    u8g2.setCursor(x + 11, y + 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.print("P");
    u8g2.setCursor(x + 26, y + 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.print("ND");
    break;

  case PRND_N:
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.print("PR");
    u8g2.setCursor(x + 26, y + 10);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.print("D");
    break;

  case PRND_D:
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(x + 1, y + 10);
    u8g2.print("PRN");
    break;

  default:
    break;
  }
}

void drawActualGear(int y)
{
  u8g2.drawRBox(53, y, 22, 23, 3);
  u8g2.setFont(u8g2_font_luBS19_te);
  const char *g = gearToStr(gear);

  // Measure text width to truly center
  int gw = u8g2.getStrWidth(g);
  int gx = (128 - gw) / 2;
  int gy = y + 21; // baseline near top row
  u8g2.setDrawColor(0);
  u8g2.setFontMode(1);
  u8g2.setCursor(gx, gy);
  u8g2.print(g);
  u8g2.setDrawColor(1);
}

void drawOdometerCentered(uint32_t odometer_km, int baselineY)
{
  char num[12];
  snprintf(num, sizeof(num), "%lu", (unsigned long)odometer_km);

  const char *unit = "km";
  const int gap = 3;

  // Measure widths in their respective fonts
  u8g2.setFont(u8g2_font_6x13B_mf);
  int wNum = u8g2.getStrWidth(num);

  u8g2.setFont(u8g2_font_6x10_tf);
  int wUnit = u8g2.getStrWidth(unit);

  int totalW = wNum + gap + wUnit;
  int x = (128 - totalW) / 2;

  // Draw number
  u8g2.setFont(u8g2_font_6x13B_mf);
  u8g2.setCursor(x, baselineY);
  u8g2.print(num);

  // Draw unit
  u8g2.setFont(u8g2_font_5x8_mf);
  u8g2.setCursor(x + wNum + gap, baselineY);
  u8g2.print(unit);
}

void draw_main_page()
{
  u8g2.clearBuffer();

  // AMG logo
  u8g2.drawXBMP(77, 56, AMG_SMALL_W, AMG_SMALL_H, amg_bits_small);

  drawOdometerCentered(odo, 18);
  drawOdometerCentered(839, 30);

  drawDriveMode(96, 44, false);
  drawPRND(5, 48);
  drawActualGear(41);

  switch (currentPage)
  {
  case PAGE_MAIN:
    break;
  }

  u8g2.sendBuffer();
}

void draw_sensors_page()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Sensors");

  char mapTxt[20];
  snprintf(mapTxt, sizeof(mapTxt), "MAP %d kpa", map_kpa);
  drawProgressBarWithInvertedText(0, 39, 128, 11, map_kpa, 0, 250, mapTxt);

  char rpmTxt[20];
  snprintf(rpmTxt, sizeof(rpmTxt), "RPM %d", rpm);
  drawProgressBarWithInvertedText(0, 52, 128, 11, rpm, 0, 9000, rpmTxt);

  drawLambdaLine(0, 15, 128, 11, 0.94, 0.70, 1.24);

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

void openDriveSelectWindow()
{
  selectWindowActive = true;
  selectWindowStartMs = millis();
}

void updatePaddles()
{
  // Update debouncers and capture press edges
  bool leftPressedEdge = paddleL.pressedEdge();
  bool rightPressedEdge = paddleR.pressedEdge();

  bool leftHeld = paddleL.isPressedNow();
  bool rightHeld = paddleR.isPressedNow();
  uint32_t now = millis();

  // --- 1) BOTH-HOLD gesture (1s) triggers P -> N and opens 500ms window ---
  if (leftHeld && rightHeld)
  {
    if (bothPressedSinceMs == 0)
      bothPressedSinceMs = now;

    // Only trigger once per hold
    if (!bothHoldArmed && (now - bothPressedSinceMs >= BOTH_HOLD_MS))
    {
      bothHoldArmed = true;
      openDriveSelectWindow();
      // If you later want "both hold to go to N from anywhere", do it here.
    }
  }
  else
  {
    // combo released -> reset
    bothPressedSinceMs = 0;
    bothHoldArmed = false;
  }

  // --- 2) Selection window: N -> D/R within 500ms after P->N ---
  if (selectWindowActive)
  {
    if (now - selectWindowStartMs > SELECT_WINDOW_MS)
    {
      selectWindowActive = false; // window expired, stay in N
    }
    else
    {
      // Only accept a single decision during the window
      if (rightPressedEdge)
      {
        prnd = PRND_D;
        selectWindowActive = false;
      }
      else if (leftPressedEdge)
      {
        prnd = PRND_R;
        selectWindowActive = false;
      }
    }
    return; // while in window, ignore other paddle functions
  }

  // --- 3) Outside the window: paddles can do other actions ---
  // Example: if in D, use paddles for +/- shifting (optional)
  // if (prnd == PRND_D) { if (rightPressedEdge) upshift(); if (leftPressedEdge) downshift(); }

  // Or allow N -> D/R anytime by a single press (optional, NOT what you asked)
  // if (prnd == PRND_N) { if (rightPressedEdge) prnd=PRND_D; if (leftPressedEdge) prnd=PRND_R; }
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

  paddleL.begin(PADDLE_L_PIN);
  paddleR.begin(PADDLE_R_PIN);

  SPI.begin();
  u8g2.begin();
  Serial.begin(115200);

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
  u8g2.setContrast(30);
  read_can(); // keep CAN serviced always
  updatePaddles();

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
