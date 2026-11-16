#pragma once
#include "Arduino.h"

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

class WiFiSim {
 public:
  bool connected = true;
  int status() { return connected ? WL_CONNECTED : WL_DISCONNECTED; }
  IPAddress localIP() { return connected ? IPAddress(192, 168, 1, 57) : IPAddress(); }
  String SSID() { return String("HomeNetwork"); }
  int RSSI() { return -58; }
  void reconnect() {}
};
extern WiFiSim WiFi;
