#pragma once

#include <cstdint>

#include "services/airport_store.h"

/**
 * Online airport lookup by ident (ICAO / local code), done on the device.
 *
 * OurAirports' airports.csv and runways.csv are sorted by ident, so the device
 * binary-searches them with HTTP Range requests (~25 small requests) instead of
 * downloading 17 MB. Runs on a background task; poll state().
 */
namespace services::lookup {

enum class State : uint8_t { kIdle, kRunning, kDone, kError };

void init();
/** Start a lookup. False if one is already running or ident is invalid. */
bool start(const char* ident);
State state();
uint8_t progress();          // 0..100
const char* message();       // human readable status / error
const char* ident();
/** Valid when state() == kDone. */
const airports::Airport& result();
/** Acknowledge a finished lookup (back to idle). */
void reset();

}  // namespace services::lookup
