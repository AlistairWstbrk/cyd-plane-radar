#include "ui/theme.h"

#include "services/settings.h"

namespace ui {

Palette pal;

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {
  const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int r = ar + ((br - ar) * t) / 255;
  const int g = ag + ((bg - ag) * t) / 255;
  const int bl = ab + ((bb - ab) * t) / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

void themeApply(uint8_t theme) {
  Palette& p = pal;
  switch (theme) {
    case settings::kThemeAmber:
      p.bg = rgb(14, 8, 0);
      p.grid = rgb(150, 90, 0);
      p.label = rgb(255, 200, 90);
      p.center = rgb(255, 210, 120);
      p.aircraft = rgb(255, 176, 0);
      p.track = rgb(255, 120, 40);
      p.tag_call = rgb(255, 220, 140);
      p.tag_type = rgb(220, 150, 40);
      p.tag_alt = rgb(255, 190, 80);
      p.runway = rgb(120, 80, 20);
      p.runway_label = rgb(200, 140, 50);
      p.custom_runway = rgb(170, 120, 40);
      p.panel_bg = rgb(20, 12, 0);
      p.text = rgb(255, 210, 120);
      p.text_dim = rgb(150, 100, 30);
      p.accent = rgb(255, 176, 0);
      p.button = rgb(55, 34, 0);
      p.select = rgb(255, 255, 200);
      break;
    case settings::kThemeIce:
      p.bg = rgb(0, 8, 18);
      p.grid = rgb(30, 90, 130);
      p.label = rgb(210, 240, 255);
      p.center = rgb(220, 245, 255);
      p.aircraft = rgb(255, 255, 255);
      p.track = rgb(0, 200, 255);
      p.tag_call = rgb(235, 250, 255);
      p.tag_type = rgb(130, 200, 255);
      p.tag_alt = rgb(120, 255, 210);
      p.runway = rgb(70, 110, 140);
      p.runway_label = rgb(150, 200, 230);
      p.custom_runway = rgb(110, 170, 210);
      p.panel_bg = rgb(4, 16, 30);
      p.text = rgb(220, 240, 255);
      p.text_dim = rgb(100, 140, 170);
      p.accent = rgb(0, 200, 255);
      p.button = rgb(14, 44, 70);
      p.select = rgb(255, 230, 80);
      break;
    case settings::kThemeNight:
      p.bg = rgb(0, 0, 0);
      p.grid = rgb(70, 0, 0);
      p.label = rgb(170, 30, 20);
      p.center = rgb(170, 30, 20);
      p.aircraft = rgb(230, 40, 20);
      p.track = rgb(140, 20, 10);
      p.tag_call = rgb(200, 40, 25);
      p.tag_type = rgb(150, 30, 20);
      p.tag_alt = rgb(170, 50, 30);
      p.runway = rgb(80, 15, 10);
      p.runway_label = rgb(130, 25, 15);
      p.custom_runway = rgb(110, 20, 10);
      p.panel_bg = rgb(8, 0, 0);
      p.text = rgb(200, 40, 25);
      p.text_dim = rgb(110, 20, 12);
      p.accent = rgb(230, 50, 25);
      p.button = rgb(40, 0, 0);
      p.select = rgb(255, 90, 40);
      break;
    default:  // Classic: the original Plane Radar look
      p.bg = rgb(4, 10, 28);
      p.grid = rgb(16, 100, 32);
      p.label = rgb(255, 255, 255);
      p.center = rgb(255, 255, 255);
      p.aircraft = rgb(255, 0, 0);
      p.track = rgb(255, 0, 255);
      p.tag_call = rgb(255, 255, 255);
      p.tag_type = rgb(255, 200, 0);
      p.tag_alt = rgb(90, 200, 255);
      p.runway = rgb(56, 150, 170);
      p.runway_label = rgb(110, 210, 230);
      p.custom_runway = rgb(200, 160, 255);
      p.panel_bg = rgb(8, 16, 38);
      p.text = rgb(235, 240, 250);
      p.text_dim = rgb(120, 140, 170);
      p.accent = rgb(60, 220, 110);
      p.button = rgb(22, 40, 78);
      p.select = rgb(255, 230, 0);
      break;
  }
  p.grid_dim = blend(p.bg, p.grid, 110);
  p.trail = blend(p.bg, p.aircraft, 120);
  p.sweep = p.grid;
  p.panel_line = blend(p.panel_bg, p.text_dim, 120);
  p.button_text = p.text;
  p.alert = theme == settings::kThemeNight ? rgb(255, 60, 20) : rgb(255, 40, 40);
  p.ok = theme == settings::kThemeNight ? rgb(150, 30, 20) : rgb(60, 220, 90);
  p.warn = theme == settings::kThemeNight ? rgb(200, 60, 20) : rgb(255, 180, 0);
}

uint16_t altitudeColor(int32_t alt_ft, bool on_ground) {
  if (settings::get().theme == settings::kThemeNight) return pal.aircraft;
  if (on_ground) return rgb(150, 150, 150);
  // Stops: 0 ft orange, 4k yellow, 10k green, 20k cyan, 30k blue, 40k magenta.
  struct Stop { int32_t ft; uint8_t r, g, b; };
  static const Stop kStops[] = {{0, 255, 110, 0},     {4000, 255, 220, 0},
                                {10000, 60, 230, 60}, {20000, 0, 220, 230},
                                {30000, 60, 110, 255}, {40000, 220, 60, 255}};
  if (alt_ft <= 0) return rgb(kStops[0].r, kStops[0].g, kStops[0].b);
  for (size_t i = 1; i < sizeof(kStops) / sizeof(kStops[0]); ++i) {
    if (alt_ft <= kStops[i].ft) {
      const Stop& a = kStops[i - 1];
      const Stop& b = kStops[i];
      const int t = (alt_ft - a.ft) * 255 / (b.ft - a.ft);
      return rgb(a.r + (b.r - a.r) * t / 255, a.g + (b.g - a.g) * t / 255,
                 a.b + (b.b - a.b) * t / 255);
    }
  }
  return rgb(220, 60, 255);
}

}  // namespace ui
