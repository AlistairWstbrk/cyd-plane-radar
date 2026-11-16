#include "ui/canvas.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/display_font.h"

namespace ui {
namespace {

LGFX_Sprite s_band(&tft);
int s_band_h = 0;
bool s_font_loaded = false;
volatile bool s_shot_requested = false;


constexpr unsigned long kShotBaud = 921600;

void shotBegin() {
  Serial.printf("\n#BAUD %lu\n", kShotBaud);
  Serial.flush();
  delay(50);
  Serial.updateBaudRate(kShotBaud);
  delay(300);  // host reopens the port at the new rate
  Serial.printf("\n#SCREENSHOT %d %d\n", config::kScreenW, config::kScreenH);
}

/** Dump band rows as raw big-endian RGB565. */
void shotBand(int rows) {
  uint8_t line[config::kScreenW * 2];
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < config::kScreenW; ++x) {
      const uint16_t c = s_band.readPixel(x, y);
      line[x * 2] = static_cast<uint8_t>(c >> 8);
      line[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
    }
    Serial.write(line, sizeof(line));
  }
}

void shotEnd() {
  Serial.print("\n#END\n");
  Serial.flush();
  delay(50);
  Serial.updateBaudRate(115200);
}

}  // namespace

void Canvas::font(Font f) {
  g->setFont(uiFont(static_cast<int>(f)));
  g->setTextSize(1);
}

int Canvas::fontHeight(Font f) {
  font(f);
  return g->fontHeight();
}

int Canvas::textWidth(const char* s, Font f) {
  font(f);
  return g->textWidth(s);
}

void Canvas::text(const char* s, int x, int y, uint16_t color, Font f, textdatum_t datum) {
  font(f);
  const int fh = g->fontHeight();
  // Cheap vertical reject (datum may centre or bottom-align the text).
  if (!rows(y - fh, y + fh)) return;
  g->setTextDatum(datum);
  g->setTextColor(color);
  g->drawString(s, x, y - oy);
}

bool frameInit() {
  s_band.setColorDepth(16);
  for (int h : {48, 40, 30, 24, 16}) {
    if (s_band.createSprite(config::kScreenW, h) != nullptr) {
      s_band_h = h;
      break;
    }
  }
  if (s_band_h == 0) return false;
  s_font_loaded = displayFontEnsureLoaded(s_band);
  Serial.printf("Frame band %dx%d, heap %u\n", config::kScreenW, s_band_h, ESP.getFreeHeap());
  return true;
}

void renderRows(int y0, int y1, void (*draw)(Canvas&, void*), void* ctx) {
  if (s_band_h == 0) return;
  const bool shot = s_shot_requested && y0 <= 0 && y1 >= config::kScreenH;
  if (shot) shotBegin();
  const int first = (y0 / s_band_h) * s_band_h;
  int band = first / s_band_h;
  for (int y = first; y < y1 && y < config::kScreenH; y += s_band_h, ++band) {
    const int rows = (y + s_band_h > config::kScreenH) ? config::kScreenH - y : s_band_h;
    Canvas c{&s_band, y, rows, band};
    draw(c, ctx);
    if (shot) shotBand(rows);
    s_band.pushSprite(&tft, 0, y);
  }
  if (shot) {
    shotEnd();
    s_shot_requested = false;
  }
}

void renderFrame(void (*draw)(Canvas&, void*), void* ctx) {
  renderRows(0, config::kScreenH, draw, ctx);
}

Canvas directCanvas() {
  displayFontEnsureLoaded(tft);
  return Canvas{&tft, 0, config::kScreenH, 0};
}

void requestScreenshot() { s_shot_requested = true; }
bool screenshotPending() { return s_shot_requested; }

}  // namespace ui
