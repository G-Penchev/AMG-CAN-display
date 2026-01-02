#include <SPI.h>
#include <U8g2lib.h>
#include <mcp_can.h>
#include "amg_logo.h" // defines AMG_W, AMG_H, amg_bits[]
#include "pins.h"
#include "types.h"
#include "config.h"
#include "ui/ui.h"
#include "prnd/prnd.h"

// -------- CAN --------
MCP_CAN CAN0(CAN_CS);

uint32_t lastUiMs = 0;

VehicleState vehicle;

Button btnMode, btnPage, paddleL, paddleR;

// -------- CAN decoded values (fill these from EMU Black frames) --------
volatile uint32_t lastCanMs = 0;

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
      vehicle.rpm = (int)(buf[0] | (buf[1] << 8));
    if (canId == 0x101 && len >= 1)
      vehicle.gear = (int8_t)buf[0]; // maybe signed
    if (canId == 0x102 && len >= 1)
      vehicle.tps = buf[0];
    if (canId == 0x103 && len >= 1)
      vehicle.clt = (int8_t)buf[0];
    if (canId == 0x104 && len >= 1)
      vehicle.iat = (int8_t)buf[0];

    // etc...
  }
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
  ui_init();
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

  draw_splash(); // draw once
}

void loop()
{

  read_can(); // keep CAN serviced always
  updatePaddles(vehicle, paddleL, paddleR);

  // Buttons
  if (btnMode.pressed())
    cycleDriveMode(vehicle);
  if (btnPage.pressed())
    next_page();

  draw_ui(vehicle);
}
