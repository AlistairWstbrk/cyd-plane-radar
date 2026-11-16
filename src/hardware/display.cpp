#include "hardware/display.h"

#include <Arduino.h>

#include "config.h"

LGFX tft;

namespace hw {
namespace {

constexpr uint8_t kLedChR = 1;
constexpr uint8_t kLedChG = 2;
constexpr uint8_t kLedChB = 3;
uint8_t s_last_pct = 255;

}  // namespace

void init() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  setBacklightPercent(80);

  // RGB LED is active LOW: duty 255 = off.
  ledcSetup(kLedChR, 5000, 8);
  ledcSetup(kLedChG, 5000, 8);
  ledcSetup(kLedChB, 5000, 8);
  ledcAttachPin(config::kPinLedR, kLedChR);
  ledcAttachPin(config::kPinLedG, kLedChG);
  ledcAttachPin(config::kPinLedB, kLedChB);
  led(0, 0, 0);
}

void applyOrientation(bool flip, bool invert) {
  tft.setRotation(flip ? 3 : 1);
#if defined(CYD_PANEL_ST7789)
  tft.invertDisplay(!invert);
#else
  tft.invertDisplay(invert);
#endif
}

void setBacklightPercent(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct < 3) pct = 3;
  if (pct == s_last_pct) return;
  s_last_pct = pct;
  tft.setBrightness(static_cast<uint8_t>(pct * 255 / 100));
}

void led(uint8_t r, uint8_t g, uint8_t b) {
  // The on-board LED is very bright; scale to ~1/4.
  ledcWrite(kLedChR, 255 - r / 4);
  ledcWrite(kLedChG, 255 - g / 4);
  ledcWrite(kLedChB, 255 - b / 4);
}

}  // namespace hw
