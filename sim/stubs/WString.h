#pragma once
// Minimal Arduino String for the host simulator.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(double v, unsigned decimals = 2) {
    char b[48];
    snprintf(b, sizeof(b), "%.*f", static_cast<int>(decimals), v);
    s_ = b;
  }
  String(float v, unsigned decimals = 2) : String(static_cast<double>(v), decimals) {}

  const char* c_str() const { return s_.c_str(); }
  unsigned length() const { return static_cast<unsigned>(s_.size()); }
  bool concat(const char* s) { s_ += s; return true; }
  bool concat(const char* s, unsigned n) { s_.append(s, n); return true; }
  bool concat(char c) { s_ += c; return true; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == o; }
  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  int indexOf(char c, unsigned from = 0) const {
    auto p = s_.find(c, from);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int lastIndexOf(char c) const {
    auto p = s_.rfind(c);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  String substring(unsigned a) const { return a >= s_.size() ? String() : String(s_.substr(a)); }
  String substring(unsigned a, unsigned b) const { return a >= s_.size() ? String() : String(s_.substr(a, b - a)); }
  bool startsWith(const char* p) const { return s_.rfind(p, 0) == 0; }
  bool equalsIgnoreCase(const char* o) const {
#ifdef _WIN32
    return _stricmp(s_.c_str(), o) == 0;
#else
    return strcasecmp(s_.c_str(), o) == 0;
#endif
  }
  long toInt() const { return atol(s_.c_str()); }
  // ArduinoJson writer support
  size_t write(uint8_t c) { s_ += static_cast<char>(c); return 1; }

 private:
  std::string s_;
};
