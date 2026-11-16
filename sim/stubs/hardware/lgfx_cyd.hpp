#pragma once
// Simulator replacement for the CYD LovyanGFX device: the "panel" is a
// 320x240 sprite that the firmware's band renderer pushes into, and that the
// simulator saves as PNG.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Sprite {
 public:
  LGFX() : lgfx::LGFX_Sprite() {}
  bool init() {
    setColorDepth(16);
    return createSprite(320, 240) != nullptr;
  }
  void setRotation(uint8_t) {}
  void invertDisplay(bool) {}
  void setBrightness(uint8_t b) { brightness_ = b; }
  uint_fast8_t getTouch(int32_t*, int32_t*) { return 0; }
  template <typename T>
  void calibrateTouch(uint16_t* p, const T&, const T&, uint8_t = 10) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint16_t>(i);
  }
  void setTouchCalibrate(uint16_t*) {}
  uint8_t brightness_ = 80;
};
