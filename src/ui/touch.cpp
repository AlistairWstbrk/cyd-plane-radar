#include "ui/touch.h"

#include <Arduino.h>

#include <cstdlib>

#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/settings.h"
#include "ui/theme.h"

namespace ui::touch {
namespace {

constexpr int kTapSlopPx = 12;
constexpr unsigned long kLongPressMs = 700;
constexpr unsigned long kDebounceMs = 30;

bool s_down = false;
bool s_dragging = false;
bool s_long_fired = false;
int16_t s_sx = 0, s_sy = 0, s_lx = 0, s_ly = 0;
unsigned long s_down_ms = 0;
unsigned long s_up_ms = 0;
unsigned long s_activity_ms = 0;
bool s_inject = false;
int16_t s_inject_x = 0, s_inject_y = 0;
uint8_t s_miss = 0;

}  // namespace

void calibrate() {
  uint16_t cal[8];
  tft.fillScreen(TFT_BLACK);
  displayFontEnsureLoaded(tft);
  tft.setTextSize(1.0f);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("TOUCH CALIBRATION", tft.width() / 2, tft.height() / 2 - 22);
  tft.setTextSize(0.8f);
  tft.drawString("Tap the tip of each arrow", tft.width() / 2, tft.height() / 2 + 2);
  tft.drawString("(use a stylus or fingernail)", tft.width() / 2, tft.height() / 2 + 20);
  tft.calibrateTouch(cal, TFT_YELLOW, TFT_BLACK, 18);
  settings::saveTouchCal(cal);
  tft.setTouchCalibrate(cal);
  tft.fillScreen(TFT_BLACK);
  Serial.printf("Touch calibrated: %u %u %u %u %u %u %u %u\n", cal[0], cal[1], cal[2],
                cal[3], cal[4], cal[5], cal[6], cal[7]);
}

void init(bool force) {
  uint16_t cal[8];
  if (!force && settings::loadTouchCal(cal)) {
    tft.setTouchCalibrate(cal);
    return;
  }
  calibrate();
}

bool pressed() { return s_down; }
unsigned long lastActivityMs() { return s_activity_ms; }

void injectTap(int16_t x, int16_t y) {
  s_inject = true;
  s_inject_x = x;
  s_inject_y = y;
}

Event poll() {
  Event e{Type::None, 0, 0, 0, 0, 0, 0};
  const unsigned long now = millis();
  if (s_inject) {
    s_inject = false;
    s_activity_ms = now;
    e.type = Type::Tap;
    e.x = e.sx = s_inject_x;
    e.y = e.sy = s_inject_y;
    return e;
  }

  int32_t x = 0, y = 0;
  const bool touching = tft.getTouch(&x, &y) > 0;
  if (touching) {
    s_miss = 0;
    s_activity_ms = now;
    if (!s_down) {
      if (now - s_up_ms < kDebounceMs) return e;
      s_down = true;
      s_dragging = false;
      s_long_fired = false;
      s_sx = s_lx = static_cast<int16_t>(x);
      s_sy = s_ly = static_cast<int16_t>(y);
      s_down_ms = now;
      e.type = Type::Down;
      e.x = e.sx = s_sx;
      e.y = e.sy = s_sy;
      return e;
    }
    if (!s_dragging && (abs(x - s_sx) > kTapSlopPx || abs(y - s_sy) > kTapSlopPx)) {
      s_dragging = true;
    }
    if (s_dragging && (x != s_lx || y != s_ly)) {
      e.type = Type::Drag;
      e.x = static_cast<int16_t>(x);
      e.y = static_cast<int16_t>(y);
      e.sx = s_sx;
      e.sy = s_sy;
      e.dx = static_cast<int16_t>(x - s_lx);
      e.dy = static_cast<int16_t>(y - s_ly);
      s_lx = e.x;
      s_ly = e.y;
      return e;
    }
    if (!s_dragging && !s_long_fired && now - s_down_ms >= kLongPressMs) {
      s_long_fired = true;
      e.type = Type::LongPress;
      e.x = e.sx = s_sx;
      e.y = e.sy = s_sy;
      return e;
    }
    return e;
  }

  // Resistive panels drop out for a sample or two mid-press; require 2 misses.
  if (s_down && ++s_miss >= 2) {
    s_down = false;
    s_up_ms = now;
    e.x = s_lx;
    e.y = s_ly;
    e.sx = s_sx;
    e.sy = s_sy;
    e.type = (!s_dragging && !s_long_fired) ? Type::Tap : Type::Release;
  }
  return e;
}

}  // namespace ui::touch
