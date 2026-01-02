#include "config.h"
#include "types.h"
#include <U8g2lib.h>

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