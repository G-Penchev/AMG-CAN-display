#pragma once
#include <stdint.h>
#include <config.h>
#include <Arduino.h>

enum Prnd : uint8_t
{
  PRND_P,
  PRND_R,
  PRND_N,
  PRND_D
};

enum UiMode
{
  UI_SPLASH,
  UI_PAGES,
  UI_MODE_ANNOUNCE
};

enum DriveMode : uint8_t
{
  MODE_COMFORT,
  MODE_SPORT,
  MODE_SPORTP,
  MODE_MANUAL
};

// -------- Menu pages --------
enum PageId : uint8_t
{
  PAGE_MAIN = 0,
  PAGE_SENSORS,
  PAGE_FUEL,
  PAGE_DEBUG,
  PAGE_COUNT
};

struct VehicleState
{
  int16_t rpm = 0;
  int16_t map_kpa = 0;
  float lambda = 1.0f;
  int tps = 0;    // %
  int clt = 0;    // coolant C
  int iat = 0;    // intake C
  float oilp = 0; // bar/psi
  uint32_t odo = 423911;
  int8_t gear = 0; // -1=R, 0=N, 1..n
  Prnd prnd = PRND_P;
  DriveMode driveMode = MODE_COMFORT;
};


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