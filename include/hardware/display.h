#pragma once

#include <cstdint>

#include "hardware/lgfx_cyd.hpp"

extern LGFX tft;

namespace hw {

/** Panel, backlight and RGB status LED. Call once at boot. */
void init();

/** Apply rotation (flip) and colour inversion from settings. */
void applyOrientation(bool flip, bool invert);

/** Backlight 0-100 %. */
void setBacklightPercent(uint8_t pct);

/** RGB LED 0-255 per channel (scaled by a global dim factor). */
void led(uint8_t r, uint8_t g, uint8_t b);

}  // namespace hw
