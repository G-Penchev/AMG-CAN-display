#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"
#include "types.h"
#include "pages/page_main.h"
#include "ui.h"
#include "pins.h"
#include "amg_logo.h"
#include "common/ui_common.h"
#include "prnd/prnd.h"

static U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(
  U8G2_R0,
  OLED_CS,
  OLED_DC,
  OLED_RST
);

uint8_t currentPage = PAGE_MAIN;

uint32_t modeAnnounceStartMs = 0;
uint32_t bootMs = 0;
UiMode uiMode = UI_SPLASH;

void ui_init() {
  u8g2.begin();
  u8g2.setContrast(120);
}

void cycleDriveMode(VehicleState& vehicle)
{
  vehicle.driveMode = (DriveMode)((vehicle.driveMode + 1) % 4);
  uiMode = UI_MODE_ANNOUNCE;
  modeAnnounceStartMs = millis();
}


void draw_main_page(VehicleState& vehicle)
{
  u8g2.clearBuffer();

  // AMG logo
  u8g2.drawXBMP(77, 56, AMG_SMALL_W, AMG_SMALL_H, amg_bits_small);

  drawOdometerCentered(u8g2, vehicle.odo, 18);
  drawOdometerCentered(u8g2,839, 30);

  drawDriveMode(u8g2, vehicle, 96, 44, false);
  drawPRND(u8g2, vehicle.prnd, 5, 48, getSelectWindowActive());
  drawActualGear(u8g2,vehicle.gear, 41);

  switch (currentPage)
  {
  case PAGE_MAIN:
    break;
  }

  u8g2.sendBuffer();
}


// ----------------- Helpers -----------------
void draw_splash()
{
  u8g2.clearBuffer();
  int x = (128 - AMG_W) / 2;
  int y = (64 - AMG_H) / 2;
  u8g2.drawXBMP(x, y, AMG_W, AMG_H, amg_bits);
  u8g2.sendBuffer();
}

// ----------------- Page drawing -----------------

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

void draw_sensors_page(VehicleState vehicle)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Sensors");

  char mapTxt[20];
  snprintf(mapTxt, sizeof(mapTxt), "MAP %d kpa", vehicle.map_kpa);
  drawProgressBarWithInvertedText(0, 39, 128, 11, vehicle.map_kpa, 0, 250, mapTxt);

  char rpmTxt[20];
  snprintf(rpmTxt, sizeof(rpmTxt), "RPM %d", vehicle.rpm);
  drawProgressBarWithInvertedText(0, 52, 128, 11, vehicle.rpm, 0, 9000, rpmTxt);

  drawLambdaLine(0, 15, 128, 11, 0.94, 0.70, 1.24);

  u8g2.sendBuffer();
}

void draw_fuel_page(VehicleState vehicle)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Fuel / Lambda");

  u8g2.setCursor(0, 28);
  u8g2.print("Lambda: ");
  u8g2.print(vehicle.lambda, 2);
  u8g2.setCursor(0, 44);
  u8g2.print("OilP: ");
  u8g2.print(vehicle.oilp, 1);

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

  u8g2.sendBuffer();
}

void draw_current_page(VehicleState& vehicle)
{
  switch (currentPage)
  {
  case PAGE_MAIN:
    draw_main_page(vehicle);
    break;
  case PAGE_SENSORS:
    draw_sensors_page(vehicle);
    break;
  case PAGE_FUEL:
    draw_fuel_page(vehicle);
    break;
  case PAGE_DEBUG:
    draw_debug_page();
    break;
  default:
    draw_main_page(vehicle);
    break;
  }
}

// ----------------- Page navigation -----------------
void next_page()
{
  currentPage = (currentPage + 1) % PAGE_COUNT;
}

void draw_mode_announcement(const VehicleState& vehicle)
{
  u8g2.clearBuffer();

  // AMG logo
  int logoX = (128 - AMG_W) / 2;
  int logoY = 10;
  u8g2.drawXBMP(logoX, logoY, AMG_W, AMG_H, amg_bits);

  // Drive mode text (wide + bold)
  u8g2.setFont(u8g2_font_helvB14_tf);
  const char *txt = driveModeToText(vehicle.driveMode);

  int tw = u8g2.getStrWidth(txt);
  int tx = (128 - tw) / 2;
  int ty = logoY + AMG_H + 16;

  u8g2.setCursor(tx, ty);
  u8g2.print(txt);

  u8g2.sendBuffer();
}

void draw_ui(VehicleState& vehicle)
{
    // Throttle UI redraw (e.g. 10 fps)
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
      draw_current_page(vehicle);
    }
    return; // don't redraw splash continuously (assuming drawn once in setup)
  }

  if (uiMode == UI_MODE_ANNOUNCE)
  {
    draw_mode_announcement(vehicle);
    if (now - modeAnnounceStartMs >= MODE_ANNOUNCE_MS)
    {
      uiMode = UI_PAGES;
      draw_current_page(vehicle);
    }
    return;
  }

  // Normal pages mode
  draw_current_page(vehicle);
}