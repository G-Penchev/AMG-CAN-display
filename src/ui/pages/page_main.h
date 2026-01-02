// src/ui/ui.h
#pragma once
#include "types.h"

void drawDriveMode(U8G2& d, VehicleState vehicle, int boxX, int boxY, boolean isShort);
void drawSelectionActive(U8G2& d,int x, int y, int size);
void drawPRND(U8G2& d, Prnd prnd, int x, int y, boolean selectWindowActive);
void drawActualGear(U8G2& d, int gear, int y);
void drawOdometerCentered(U8G2& d, uint32_t odometer_km, int baselineY);

