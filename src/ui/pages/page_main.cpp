#include "amg_logo.h"
#include "config.h"
#include "types.h"
#include "../common/ui_common.h"
#include "prnd/prnd.h"
#include <U8g2lib.h>



void drawDriveMode(U8G2& d, VehicleState vehicle, int boxX, int boxY, boolean isShort)
{
  // Mode square to the right of gear (small)
  int boxSize = 15;
  const char *ms;
  if (isShort)
  {
    d.setFont(u8g2_font_t0_15b_tf);
    ms = driveModeToShort(vehicle.driveMode);
  }
  else
  {
    d.setFont(u8g2_font_5x7_tf);
    ms = driveModeToText(vehicle.driveMode);
  }
  int msw = d.getStrWidth(ms);
  d.setCursor(boxX + (boxSize - msw) / 2, boxY + 10);
  d.print(ms);
}

void drawSelectionActive(U8G2& d,int x, int y, int size)
{
  // y is top of triangle
  d.drawTriangle(
      x + size / 2, y,
      x + size / 2, y + size,
      x, y + size / 2);

  int offset = size / 2 + 2;

  d.drawTriangle(
      x + offset, y,
      x + offset, y + size,
      x + size / 2 + offset, y + size / 2);
}

void drawPRND(U8G2& d, Prnd prnd, int x, int y, boolean selectWindowActive)
{
  if (selectWindowActive)
  {
    drawSelectionActive(d, x + 16, y - 7, 6);
  }

  d.drawBox(x + 16, y, 9, 13);
  d.setDrawColor(0);
  d.setCursor(x + 16, y + 12);
  d.setFont(u8g2_font_t0_18b_tr);
  d.print(prndToStr(prnd));
  d.setDrawColor(1);

  switch (prnd)
  {
  case PRND_P:
    d.setFont(u8g2_font_5x7_tf);
    d.setCursor(x + 26, y + 10);
    d.print("RND");
    break;

  case PRND_R:
    d.setCursor(x + 11, y + 10);
    d.setFont(u8g2_font_5x7_tf);
    d.print("P");
    d.setCursor(x + 26, y + 10);
    d.setFont(u8g2_font_5x7_tf);
    d.print("ND");
    break;

  case PRND_N:
    d.setFont(u8g2_font_5x7_tf);
    d.print("PR");
    d.setCursor(x + 26, y + 10);
    d.setFont(u8g2_font_5x7_tf);
    d.print("D");
    break;

  case PRND_D:
    d.setFont(u8g2_font_5x7_tf);
    d.setCursor(x + 1, y + 10);
    d.print("PRN");
    break;

  default:
    break;
  }
}

void drawActualGear(U8G2& d, int gear, int y)
{
  d.drawRBox(53, y, 22, 23, 3);
  d.setFont(u8g2_font_luBS19_te);
  const char *g = gearToStr(gear);

  // Measure text width to truly center
  int gw = d.getStrWidth(g);
  int gx = (128 - gw) / 2;
  int gy = y + 21; // baseline near top row
  d.setDrawColor(0);
  d.setFontMode(1);
  d.setCursor(gx, gy);
  d.print(g);
  d.setDrawColor(1);
}

void drawOdometerCentered(U8G2& d, uint32_t odometer_km, int baselineY)
{
  char num[12];
  snprintf(num, sizeof(num), "%lu", (unsigned long)odometer_km);

  const char *unit = "km";
  const int gap = 3;

  // Measure widths in their respective fonts
  d.setFont(u8g2_font_6x13B_mf);
  int wNum = d.getStrWidth(num);

  d.setFont(u8g2_font_6x10_tf);
  int wUnit = d.getStrWidth(unit);

  int totalW = wNum + gap + wUnit;
  int x = (128 - totalW) / 2;

  // Draw number
  d.setFont(u8g2_font_6x13B_mf);
  d.setCursor(x, baselineY);
  d.print(num);

  // Draw unit
  d.setFont(u8g2_font_5x8_mf);
  d.setCursor(x + wNum + gap, baselineY);
  d.print(unit);
}


