#pragma once

#include <LovyanGFX.hpp>

/**
 * Embedded anti-aliased UI fonts (Noto Sans, VLW), one per UI size and each
 * rasterised at its native pixel size with hinting (scripts/build_fonts.py).
 * Index: 0 = XS 11px, 1 = S 13px, 2 = M 15px, 3 = L 22px, 4 = XL 32px.
 */
constexpr int kUiFontCount = 5;

bool displayFontInit();
bool displayFontIsSmooth();
/** Font for a UI size (falls back to a built-in bitmap font). */
const lgfx::IFont* uiFont(int index);
/** Select the default (M) UI font on `gfx`. */
bool displayFontEnsureLoaded(lgfx::LovyanGFX& gfx);

/** Raw VLW data for a size (platform-specific: firmware embeds, simulator links). */
const uint8_t* uiFontBlob(int index, size_t* len);
