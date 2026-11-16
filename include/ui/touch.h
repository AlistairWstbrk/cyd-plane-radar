#pragma once

#include <cstdint>

namespace ui::touch {

enum class Type : uint8_t { None, Down, Drag, Tap, LongPress, Release };

struct Event {
  Type type;
  int16_t x, y;      // current point (screen coords)
  int16_t sx, sy;    // where the press started
  int16_t dx, dy;    // movement since last event (Drag)
};

/** Load calibration (or run it on first boot). */
void init(bool force_calibration);
/** Run the 4-corner calibration and save it. */
void calibrate();
/** Poll the panel; returns at most one event per call. */
Event poll();
/** Inject a synthetic tap (serial test harness). */
void injectTap(int16_t x, int16_t y);
/** True while a finger is on the panel. */
bool pressed();
/** millis() of the last touch activity. */
unsigned long lastActivityMs();

}  // namespace ui::touch
