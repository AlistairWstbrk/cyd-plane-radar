#pragma once
#include "Arduino.h"

class WiFiClient {
 public:
  int available() { return 0; }
  int readBytes(char*, int) { return 0; }
  void stop() {}
};

class WiFiClientSecure : public WiFiClient {
 public:
  void setInsecure() {}
};
