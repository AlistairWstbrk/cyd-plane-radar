#pragma once
// In-memory NVS for the simulator.
#include <map>
#include <string>
#include <vector>

#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char* ns, bool = false) { ns_ = ns; return true; }
  void end() {}
  bool isKey(const char* k) { return store()[ns_].count(k) > 0; }
  bool remove(const char* k) { return store()[ns_].erase(k) > 0; }
  size_t getBytesLength(const char* k) { return isKey(k) ? store()[ns_][k].size() : 0; }
  size_t getBytes(const char* k, void* out, size_t n) {
    if (!isKey(k)) return 0;
    auto& v = store()[ns_][k];
    const size_t m = v.size() < n ? v.size() : n;
    memcpy(out, v.data(), m);
    return m;
  }
  size_t putBytes(const char* k, const void* in, size_t n) {
    auto& v = store()[ns_][k];
    v.assign(static_cast<const uint8_t*>(in), static_cast<const uint8_t*>(in) + n);
    return n;
  }
  bool getBool(const char* k, bool d = false) { bool v = d; getBytes(k, &v, sizeof(v)); return v; }
  size_t putBool(const char* k, bool v) { return putBytes(k, &v, sizeof(v)); }
  double getDouble(const char* k, double d = 0) { double v = d; getBytes(k, &v, sizeof(v)); return v; }
  size_t putDouble(const char* k, double v) { return putBytes(k, &v, sizeof(v)); }
  uint8_t getUChar(const char* k, uint8_t d = 0) { uint8_t v = d; getBytes(k, &v, 1); return v; }
  size_t putUChar(const char* k, uint8_t v) { return putBytes(k, &v, 1); }

 private:
  static std::map<std::string, std::map<std::string, std::vector<uint8_t>>>& store() {
    static std::map<std::string, std::map<std::string, std::vector<uint8_t>>> s;
    return s;
  }
  std::string ns_;
};
