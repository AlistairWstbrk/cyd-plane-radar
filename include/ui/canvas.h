#pragma once

#include <cstdint>

#include "hardware/display.h"

namespace ui {

enum class Font : uint8_t { XS, S, M, L, XL };

/**
 * Drawing surface for one horizontal band of the screen.
 *
 * The CYD has no PSRAM, so a full 320x240x16 frame buffer (150 KB) does not
 * fit in one heap block. Frames are composed band by band into a 320xN sprite
 * and pushed; every draw call takes screen coordinates and the canvas shifts
 * them by the band offset. The result is flicker-free full-frame updates.
 */
struct Canvas {
  lgfx::LovyanGFX* g;
  int oy;      // screen y of the band's first row
  int h;       // band height
  int band;    // band index (0 = top)

  bool rows(int y0, int y1) const { return y1 >= oy && y0 < oy + h; }

  void fill(uint16_t c) { g->fillScreen(c); }
  void fillRect(int x, int y, int w, int hh, uint16_t c) {
    if (rows(y, y + hh)) g->fillRect(x, y - oy, w, hh, c);
  }
  void rect(int x, int y, int w, int hh, uint16_t c) {
    if (rows(y, y + hh)) g->drawRect(x, y - oy, w, hh, c);
  }
  void roundRect(int x, int y, int w, int hh, int r, uint16_t c) {
    if (rows(y, y + hh)) g->drawRoundRect(x, y - oy, w, hh, r, c);
  }
  void fillRoundRect(int x, int y, int w, int hh, int r, uint16_t c) {
    if (rows(y, y + hh)) g->fillSmoothRoundRect(x, y - oy, w, hh, r, c);
  }
  void hline(int x, int y, int w, uint16_t c) {
    if (rows(y, y)) g->drawFastHLine(x, y - oy, w, c);
  }
  void vline(int x, int y, int hh, uint16_t c) {
    if (rows(y, y + hh)) g->drawFastVLine(x, y - oy, hh, c);
  }
  void line(int x0, int y0, int x1, int y1, uint16_t c) {
    if (rows(y0 < y1 ? y0 : y1, y0 < y1 ? y1 : y0)) g->drawLine(x0, y0 - oy, x1, y1 - oy, c);
  }
  void wideLine(int x0, int y0, int x1, int y1, float hw, uint16_t c) {
    const int pad = static_cast<int>(hw) + 1;
    if (rows((y0 < y1 ? y0 : y1) - pad, (y0 < y1 ? y1 : y0) + pad))
      g->drawWideLine(x0, y0 - oy, x1, y1 - oy, hw, c);
  }
  void circle(int x, int y, int r, uint16_t c) {
    if (rows(y - r, y + r)) g->drawCircle(x, y - oy, r, c);
  }
  void fillCircle(int x, int y, int r, uint16_t c) {
    if (rows(y - r, y + r)) g->fillCircle(x, y - oy, r, c);
  }
  void smoothCircle(int x, int y, int r, uint16_t c) {
    if (rows(y - r, y + r)) g->fillSmoothCircle(x, y - oy, r, c);
  }
  void ring(int x, int y, int r0, int r1, uint16_t c) {  // r1 outer
    if (rows(y - r1, y + r1)) g->fillArc(x, y - oy, r0, r1, 0, 360, c);
  }
  void arc(int x, int y, int r0, int r1, float a0, float a1, uint16_t c) {
    if (rows(y - r1, y + r1)) g->fillArc(x, y - oy, r0, r1, a0, a1, c);
  }
  void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int lo = y0, hi = y0;
    if (y1 < lo) lo = y1; if (y1 > hi) hi = y1;
    if (y2 < lo) lo = y2; if (y2 > hi) hi = y2;
    if (rows(lo, hi)) g->fillTriangle(x0, y0 - oy, x1, y1 - oy, x2, y2 - oy, c);
  }
  void pixel(int x, int y, uint16_t c) {
    if (rows(y, y)) g->drawPixel(x, y - oy, c);
  }
  void qr(const char* text, int x, int y, int w) {
    if (rows(y, y + w)) g->qrcode(text, x, y - oy, w, 4);
  }

  /** Text helpers (smooth VLW font, scaled). */
  void font(Font f);
  int fontHeight(Font f);
  int textWidth(const char* s, Font f);
  void text(const char* s, int x, int y, uint16_t color, Font f,
            textdatum_t datum = textdatum_t::top_left);
};

/** Allocate the band sprite (call once after display init). */
bool frameInit();
/** Render a full frame with `draw` and push it to the panel. */
void renderFrame(void (*draw)(Canvas&, void*), void* ctx);
/** Render only rows [y0, y1) (rounded out to bands). */
void renderRows(int y0, int y1, void (*draw)(Canvas&, void*), void* ctx);
/** Canvas that targets the panel directly (static screens, no sprite). */
Canvas directCanvas();

/** Stream the next frame over Serial as a screenshot (see tools/screenshot.py). */
void requestScreenshot();
bool screenshotPending();

}  // namespace ui
