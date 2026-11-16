#pragma once
// Simulator HTTP client: serves Range requests from local copies of the
// OurAirports CSVs (sim_data/airports.csv, sim_data/runways.csv) so the real
// lookup code (binary search over ranges) runs unchanged.
#include <cstdio>
#include <string>

#include "WiFiClientSecure.h"

#define HTTP_CODE_OK 200
#define HTTPC_ERROR_CONNECTION_REFUSED -1
#define HTTPC_ERROR_NOT_CONNECTED -4
#define HTTPC_ERROR_READ_TIMEOUT -11

extern int g_sim_http_requests;
extern bool g_sim_http_offline;
inline void sim_set_http_offline(bool v) { g_sim_http_offline = v; }

class HTTPClient {
 public:
  bool begin(WiFiClient&, const String& url) {
    url_ = url.c_str();
    range_a_ = range_b_ = -1;
    return true;
  }
  void setReuse(bool) {}
  void setConnectTimeout(int) {}
  void setTimeout(unsigned long) {}
  void collectHeaders(const char**, size_t) {}
  void addHeader(const String& k, const String& v) {
    if (k == "Range") {
      long a = -1, b = -1;
      sscanf(v.c_str(), "bytes=%ld-%ld", &a, &b);
      range_a_ = a;
      range_b_ = b;
    }
  }
  int GET() {
    ++g_sim_http_requests;
    if (g_sim_http_offline) return HTTPC_ERROR_CONNECTION_REFUSED;
    std::string file;
    if (url_.find("airports.csv") != std::string::npos) file = "sim_data/airports.csv";
    else if (url_.find("runways.csv") != std::string::npos) file = "sim_data/runways.csv";
    else return 404;
    FILE* f = fopen(file.c_str(), "rb");
    if (!f) return 404;
    fseek(f, 0, SEEK_END);
    total_ = ftell(f);
    long a = range_a_ < 0 ? 0 : range_a_;
    long b = range_b_ < 0 || range_b_ >= total_ ? total_ - 1 : range_b_;
    body_.assign(static_cast<size_t>(b - a + 1), '\0');
    fseek(f, a, SEEK_SET);
    const size_t got = fread(&body_[0], 1, body_.size(), f);
    body_.resize(got);
    fclose(f);
    char cr[64];
    snprintf(cr, sizeof(cr), "bytes %ld-%ld/%ld", a, b, total_);
    content_range_ = cr;
    return range_a_ >= 0 ? 206 : 200;
  }
  String getString() { return String(body_); }
  String header(const char* k) {
    if (std::string(k) == "Content-Range") return String(content_range_);
    return String("");
  }
  int getSize() { return static_cast<int>(body_.size()); }
  WiFiClient* getStreamPtr() { return nullptr; }
  bool connected() { return false; }
  void end() {}

 private:
  std::string url_, body_, content_range_;
  long range_a_ = -1, range_b_ = -1, total_ = 0;
};
