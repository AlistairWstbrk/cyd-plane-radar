#include "hardware/display_font.h"

#include <Arduino.h>

namespace {

lgfx::PointerWrapper s_data[kUiFontCount];
lgfx::VLWfont s_font[kUiFontCount];
bool s_ok = false;

const lgfx::IFont* fallback(int index) {
  switch (index) {
    case 0: return &fonts::Font0;
    case 1:
    case 2: return &fonts::DejaVu12;
    case 3: return &fonts::DejaVu18;
    default: return &fonts::DejaVu24;
  }
}

}  // namespace

bool displayFontInit() {
  if (s_ok) return true;
  bool ok = true;
  for (int i = 0; i < kUiFontCount; ++i) {
    size_t len = 0;
    const uint8_t* blob = uiFontBlob(i, &len);
    if (blob == nullptr || len < 24) { ok = false; break; }
    s_data[i].set(blob, len);
    if (!s_font[i].loadFont(&s_data[i])) { ok = false; break; }
  }
  s_ok = ok;
  if (!ok) Serial.println("Smooth font load failed - using bitmap fallback");
  return ok;
}

bool displayFontIsSmooth() { return s_ok; }

const lgfx::IFont* uiFont(int index) {
  if (index < 0) index = 0;
  if (index >= kUiFontCount) index = kUiFontCount - 1;
  return s_ok ? static_cast<const lgfx::IFont*>(&s_font[index]) : fallback(index);
}

bool displayFontEnsureLoaded(lgfx::LovyanGFX& gfx) {
  gfx.setFont(uiFont(2));
  gfx.setTextSize(1);
  return s_ok;
}
