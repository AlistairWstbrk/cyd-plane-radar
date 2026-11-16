#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/**
 * LovyanGFX device for the ESP32-2432S028R ("Cheap Yellow Display").
 * Panel on HSPI, XPT2046 touch on its own VSPI bus, PWM backlight on GPIO 21.
 * Build env selects ILI9341 (classic CYD) or ST7789 (CYD2USB).
 */
class LGFX : public lgfx::LGFX_Device {
#if defined(CYD_PANEL_ST7789)
  lgfx::Panel_ST7789 _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;  // HSPI
      cfg.spi_mode = 0;
      cfg.freq_write = config::kTftSpiWriteHz;
      cfg.freq_read = config::kTftSpiReadHz;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = config::kPinTftSclk;
      cfg.pin_mosi = config::kPinTftMosi;
      cfg.pin_miso = config::kPinTftMiso;
      cfg.pin_dc = config::kPinTftDc;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = config::kPinTftCs;
      cfg.pin_rst = config::kPinTftRst;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.bus_shared = false;
#if defined(CYD_PANEL_ST7789)
      cfg.invert = true;
      cfg.rgb_order = false;
#else
      cfg.invert = false;
      cfg.rgb_order = false;
#endif
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = config::kPinTftBacklight;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 200;
      cfg.y_max = 3700;
      cfg.pin_int = config::kPinTouchIrq;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI3_HOST;  // VSPI
      cfg.freq = 1000000;
      cfg.pin_sclk = config::kPinTouchSclk;
      cfg.pin_mosi = config::kPinTouchMosi;
      cfg.pin_miso = config::kPinTouchMiso;
      cfg.pin_cs = config::kPinTouchCs;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
