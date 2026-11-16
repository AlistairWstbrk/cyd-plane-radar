#pragma once

namespace ui::status {

void boot();
void connectingBegin(const char* ssid);
void connectingTick();
/** Setup portal: QR to join the AP + QR to open the page. */
void portal();
void connectFailed();
void wifiReset();
/** Firmware upload: 0 = started, 1 = done (restarting), -1 = failed. */
void firmwareUpdate(int state);

}  // namespace ui::status
