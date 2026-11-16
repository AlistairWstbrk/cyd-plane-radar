#pragma once

#include <cstdint>

#include "ui/canvas.h"
#include "ui/touch.h"

namespace ui {

enum class ScreenId : uint8_t { Radar, List, Menu, Airports, Search, Lookup, Info, Confirm, Detail, Guide };

/** Initialise UI state (call after display + settings). */
void appInit();
/** Run one UI iteration: touch, alerts, LED, redraw when due. */
void appLoop(bool online);
/** Force the current screen to redraw on the next loop. */
void requestRedraw();
void navigate(ScreenId id);
ScreenId currentScreen();
/** Range tap from BOOT button (original control). */
void onBootTap();

// ---- shared widgets (used by the screen modules) ----
struct Button {
  int16_t x, y, w, h;
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class BtnStyle : uint8_t { Normal, Primary, Danger, Ghost };
void drawButton(Canvas& c, const Button& b, const char* label, BtnStyle style = BtnStyle::Normal,
                Font f = Font::S);
/** 30 px header with optional back arrow and right-side text. */
void drawHeader(Canvas& c, const char* title, bool back, const char* right = nullptr);
constexpr Button kBackBtn{0, 0, 44, 30};
void drawIconList(Canvas& c, int cx, int cy, uint16_t col);
void drawIconSliders(Canvas& c, int cx, int cy, uint16_t col);

// ---- screen modules ----
void radarDraw(Canvas& c, void*);
void radarTouch(const touch::Event& e);

void listEnter();
void listDraw(Canvas& c, void*);
void listTouch(const touch::Event& e);

void menuEnter();
void menuDraw(Canvas& c, void*);
void menuTouch(const touch::Event& e);

void airportsEnter();
void airportsDraw(Canvas& c, void*);
void airportsTouch(const touch::Event& e);
/** Airport search (keyboard + live results from the built-in index). */
void searchEnter(bool clear);
/** Open the search screen with `q` (upper-cased); `typing` shows the keyboard. */
void searchSetQuery(const char* q, bool typing);
void searchDraw(Canvas& c, void*);
void searchTouch(const touch::Event& e);
/** Where the lookup screen returns to; `index_idx` >= 0 is the search index entry it came from. */
void lookupSetOrigin(ScreenId back, int index_idx);
void lookupDraw(Canvas& c, void*);
void lookupTouch(const touch::Event& e);
bool lookupTick();

/** Aircraft detail (selected, else nearest) with closest-approach prediction. */
void detailOpen();
void detailDraw(Canvas& c, void*);
void detailTouch(const touch::Event& e);

/** True while the night schedule is dimming the screen. */
bool nightActive();

void infoDraw(Canvas& c, void*);
void infoTouch(const touch::Event& e);
void guideDraw(Canvas& c, void*);
void guideTouch(const touch::Event& e);

/** Modal confirmation; `yes` runs on confirm, then returns to `back`. */
void confirmEnter(const char* title, const char* body, void (*yes)(), ScreenId back);
void confirmDraw(Canvas& c, void*);
void confirmTouch(const touch::Event& e);

}  // namespace ui
