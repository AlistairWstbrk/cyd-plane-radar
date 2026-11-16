#pragma once

#include <cstdint>

namespace ui {

/** RGB565 palette for the active theme (rebuilt when settings change). */
struct Palette {
  uint16_t bg;
  uint16_t grid;
  uint16_t grid_dim;
  uint16_t label;
  uint16_t center;
  uint16_t aircraft;
  uint16_t track;
  uint16_t tag_call;
  uint16_t tag_type;
  uint16_t tag_alt;
  uint16_t runway;
  uint16_t runway_label;
  uint16_t custom_runway;
  uint16_t trail;
  uint16_t sweep;
  uint16_t panel_bg;
  uint16_t panel_line;
  uint16_t text;
  uint16_t text_dim;
  uint16_t accent;
  uint16_t button;
  uint16_t button_text;
  uint16_t select;
  uint16_t alert;
  uint16_t ok;
  uint16_t warn;
};

extern Palette pal;

void themeApply(uint8_t theme);
/** Colour by altitude (tar1090-like ramp). */
uint16_t altitudeColor(int32_t alt_ft, bool on_ground);
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint16_t blend(uint16_t a, uint16_t b, uint8_t t);  // t=0 -> a, 255 -> b

}  // namespace ui
