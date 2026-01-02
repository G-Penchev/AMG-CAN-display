#include <Arduino.h>
#include <types.h>
#include <pins.h>
#include <config.h>

bool selectWindowActive = false;
uint32_t selectWindowStartMs = 0;

// combo / window tracking
bool bothHoldArmed = false;
uint32_t bothPressedSinceMs = 0;

void openDriveSelectWindow()
{
  selectWindowActive = true;
  selectWindowStartMs = millis();
}

void updatePaddles(VehicleState& vehicle, Button& paddleL, Button& paddleR)
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
        vehicle.prnd = PRND_D;
        selectWindowActive = false;
      }
      else if (leftPressedEdge)
      {
        vehicle.prnd = PRND_R;
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

boolean getSelectWindowActive()
{
  return selectWindowActive;
}
