#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  int32_t alt_ft;
  int16_t vrate_fpm;
  uint32_t pos_age_ms;
  uint8_t category;   // 0 unknown, else ADS-B emitter category nibble (A1=0x11...)
  bool on_ground;
  bool alt_valid;
  bool military;
  char hex[7];
  char callsign[9];
  char type[5];
  char reg[9];
  char squawk[5];
};

constexpr size_t kMaxAircraft = 80;

struct FeedStatus {
  unsigned long last_ok_ms;     // millis() of last successful fetch (0 = never)
  unsigned long last_try_ms;
  uint32_t ok_count;
  uint32_t err_count;
  int last_http;                // last HTTP code / error
  uint16_t total_in_range;      // aircraft returned by the feed before capping
  uint16_t fetch_ms;            // duration of the last fetch
};

void init();

/** Copy current aircraft (nearest first is NOT guaranteed). Thread-safe. */
size_t snapshotAircraft(Aircraft* out, size_t max_out, unsigned long* out_last_update_ms);

FeedStatus status();

/** Blocking fetch from adsb.fi; keeps the kMaxAircraft nearest. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km,
                 bool include_ground);

/** Simulation (stress test / demo): 0 = off, else number of synthetic aircraft. */
void setSimulation(uint16_t count);
uint16_t simulationCount();
/** Advance the synthetic traffic; called by the fetch task instead of HTTP. */
void simulateUpdate(double center_lat, double center_lon, float radius_km);

/** Publish a fixed aircraft list (tests / placement verification). */
void injectForTest(const Aircraft* list, size_t count);

/** Serialises TLS sessions (ADS-B, airport lookup) to bound heap use. */
bool netLock(uint32_t timeout_ms);
void netUnlock();

}  // namespace services::adsb
