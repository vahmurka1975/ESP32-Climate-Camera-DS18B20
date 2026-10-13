#pragma once
#include <TFT_eSPI.h>
#include "Config.h"
#include "ClimateEngine.h"

class DisplayUI {
public:
  void init();
  void update(const ClimateEngine &engine, ClimateConfig &cfg, const char* ipStr, bool wifiOk);

private:
  TFT_eSPI _tft;
  uint8_t  _page = 0;
  uint32_t _lastRender = 0;

  void drawScreen(const ClimateEngine &engine, const ClimateConfig &cfg, const char* ipStr, bool wifiOk);
  void handleEncoder();
};
