#include "DisplayUI.h"

void DisplayUI::init() {
  _tft.init();
  _tft.setRotation(0); // 240 x 320
  _tft.fillScreen(TFT_BLACK);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
}

void DisplayUI::handleEncoder() {
  // Антидребезг без ожидания отпускания: кнопка не останавливает климатический цикл.
  static bool lastRaw = HIGH;
  static bool stable = HIGH;
  static uint32_t changedAt = 0;
  const bool raw = digitalRead(PIN_ENC_SW);
  const uint32_t now = millis();
  if (raw != lastRaw) { lastRaw = raw; changedAt = now; }
  if (raw != stable && uint32_t(now - changedAt) >= 40) {
    stable = raw;
    if (stable == LOW) {
      _page = (_page + 1) % 3;
      _tft.fillScreen(TFT_BLACK);
    }
  }
}

void DisplayUI::update(const ClimateEngine &engine, ClimateConfig &cfg, const char* ipStr, bool wifiOk) {
  handleEncoder();
  uint32_t now = millis();
  if (now - _lastRender < 400) return;
  _lastRender = now;

  drawScreen(engine, cfg, ipStr, wifiOk);
}

void DisplayUI::drawScreen(const ClimateEngine &engine, const ClimateConfig &cfg, const char* ipStr, bool wifiOk) {
  _tft.startWrite();

  // 1. Верхний колонтитул (0..32)
  _tft.fillRect(0, 0, 240, 32, 0x18E3);
  _tft.setTextColor(TFT_CYAN, 0x18E3);
  _tft.drawString("КЛИМАТ v" FW_VERSION, 8, 8, 2);

  if (wifiOk) {
    _tft.setTextColor(TFT_GREEN, 0x18E3);
    _tft.drawString("WiFi OK", 180, 8, 2);
  } else {
    _tft.setTextColor(TFT_YELLOW, 0x18E3);
    _tft.drawString("AP MODE", 175, 8, 2);
  }

  char buf[48];

  if (_page == 0) {
    // ================= ЭКРАН 0: ГЛАВНЫЙ МОНИТОР =================
    // Карточка ТЕМПЕРАТУРЫ (36..116)
    _tft.fillRoundRect(8, 36, 224, 80, 8, 0x0841);
    _tft.setTextColor(TFT_LIGHTGREY, 0x0841);
    _tft.drawString("ТЕМПЕРАТУРА", 18, 42, 2);

    if (engine.isSensorValid()) {
      _tft.setTextColor(TFT_YELLOW, 0x0841);
      snprintf(buf, sizeof(buf), "%.1f C", engine.getTemp());
      _tft.drawString(buf, 18, 62, 4);
    } else {
      _tft.setTextColor(TFT_RED, 0x0841);
      _tft.drawString("СБОЙ ДАТЧИКА", 18, 62, 4);
    }

    _tft.setTextColor(TFT_SILVER, 0x0841);
    snprintf(buf, sizeof(buf), "Цель: %.1f +-%.1f C", cfg.tempTarget, cfg.tempHyst);
    _tft.drawString(buf, 18, 94, 2);

    // Карточка ВЛАЖНОСТИ (122..202)
    _tft.fillRoundRect(8, 122, 224, 80, 8, 0x0841);
    _tft.setTextColor(TFT_LIGHTGREY, 0x0841);
    _tft.drawString("ВЛАЖНОСТЬ RH", 18, 128, 2);

    if (engine.isSensorValid()) {
      _tft.setTextColor(TFT_GREEN, 0x0841);
      snprintf(buf, sizeof(buf), "%.1f %%", engine.getHum());
      _tft.drawString(buf, 18, 148, 4);
    } else {
      _tft.setTextColor(TFT_RED, 0x0841);
      _tft.drawString("DS18B20 ERROR", 18, 148, 4);
    }

    _tft.setTextColor(TFT_SILVER, 0x0841);
    snprintf(buf, sizeof(buf), "Цель: %.1f +-%.1f %%", cfg.humTarget, cfg.humHyst);
    _tft.drawString(buf, 18, 180, 2);

    // Карточка ОБДУВА И СТАТУСОВ (208..280)
    _tft.fillRoundRect(8, 208, 224, 76, 8, 0x1082);
    _tft.setTextColor(TFT_WHITE, 0x1082);
    snprintf(buf, sizeof(buf), "ВЕНТИЛЯТОР: %u RPM", engine.getFanRPM());
    _tft.drawString(buf, 16, 214, 2);

    // Статус холодильника
    _tft.drawString("ХОЛОД:", 16, 236, 2);
    if (engine.isCoolOn()) {
      _tft.setTextColor(TFT_CYAN, 0x1082);
      _tft.drawString("РАБОТА ", 85, 236, 2);
    } else {
      uint32_t wait = engine.getCompressorProtectionRemaining();
      if (wait > 0) {
        _tft.setTextColor(TFT_ORANGE, 0x1082);
        snprintf(buf, sizeof(buf), "ПАУЗА %us", wait);
        _tft.drawString(buf, 85, 236, 2);
      } else {
        _tft.setTextColor(TFT_DARKGREY, 0x1082);
        _tft.drawString("ОТКЛ   ", 85, 236, 2);
      }
    }

    // Статус увлажнителя
    _tft.setTextColor(TFT_WHITE, 0x1082);
    _tft.drawString("УВЛАЖН:", 16, 258, 2);
    if (engine.isHumidOn()) {
      _tft.setTextColor(TFT_GREEN, 0x1082);
      _tft.drawString("АКТИВЕН", 90, 258, 2);
    } else {
      _tft.setTextColor(TFT_DARKGREY, 0x1082);
      _tft.drawString("ПАУЗА  ", 90, 258, 2);
    }
    _tft.fillRect(12, 278, 216, 14, TFT_BLACK);
    _tft.setTextColor(engine.isSensorValid() ? TFT_CYAN : TFT_RED, TFT_BLACK);
    if (engine.isSensorValid()) snprintf(buf, sizeof(buf), "WET %.2f C | DS18B20 pair", engine.getWetTemp());
    else snprintf(buf, sizeof(buf), "%.32s", engine.getSensorError());
    _tft.drawString(buf, 12, 279, 1);
  } else if (_page == 1) {
    // ================= ЭКРАН 1: УСТАВКИ =================
    _tft.drawString("НАСТРОЙКИ КЛИМАТА", 10, 42, 2);
    _tft.drawFastHLine(10, 62, 220, TFT_NAVY);

    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Целевая темп: %.1f C", cfg.tempTarget);
    _tft.drawString(buf, 15, 75, 2);
    snprintf(buf, sizeof(buf), "Гистерезис T: %.1f C", cfg.tempHyst);
    _tft.drawString(buf, 15, 105, 2);

    snprintf(buf, sizeof(buf), "Целевая влажн: %.1f %%", cfg.humTarget);
    _tft.drawString(buf, 15, 140, 2);
    snprintf(buf, sizeof(buf), "Гистерезис H: %.1f %%", cfg.humHyst);
    _tft.drawString(buf, 15, 170, 2);

    snprintf(buf, sizeof(buf), "Целевые RPM: %u", cfg.fanTargetRPM);
    _tft.drawString(buf, 15, 205, 2);

    snprintf(buf, sizeof(buf), "Регулирование: %s", cfg.enabled ? "ВКЛ" : "ОТКЛ");
    _tft.drawString(buf, 15, 240, 2);
  } else {
    // ================= ЭКРАН 2: СЕТЬ И IP =================
    _tft.drawString("СЕТЬ И ВЕБ-ИНТЕРФЕЙС", 10, 42, 2);
    _tft.drawFastHLine(10, 62, 220, TFT_NAVY);

    _tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft.drawString("Статус подключения:", 15, 75, 2);
    _tft.setTextColor(wifiOk ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    _tft.drawString(wifiOk ? "Подключено к роутеру" : "Режим точки доступа", 15, 95, 2);

    _tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft.drawString("Сеть (SSID):", 15, 130, 2);
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.drawString(wifiOk ? DEFAULT_WIFI_SSID : AP_FALLBACK_SSID, 15, 150, 2);

    _tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft.drawString("IP-адрес в браузере:", 15, 185, 2);
    _tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "http://%s", ipStr);
    _tft.drawString(buf, 15, 205, 2);

    _tft.setTextColor(TFT_SILVER, TFT_BLACK);
    _tft.drawString("mDNS: http://climate-camera.local", 15, 235, 1);
  }

  // Нижний колонтитул (288..320)
  _tft.fillRect(0, 292, 240, 28, 0x1082);
  _tft.setTextColor(TFT_WHITE, 0x1082);
  snprintf(buf, sizeof(buf), "Экран %u/3 | IP: %s", _page + 1, ipStr);
  _tft.drawString(buf, 10, 298, 2);

  _tft.endWrite();
}
